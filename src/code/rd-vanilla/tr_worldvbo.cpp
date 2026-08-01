/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// World geometry is static, so its vertices live in GPU buffers built at load and
// only indices are written per frame. Eligible planar surfaces draw straight from
// there; everything else falls through to the unchanged tess path.

#include "../server/exe_headers.h"

#include "tr_local.h"
#ifdef USE_GXM_NATIVE
#include "../rd-gxm/gxm_device.h"
#endif

#ifdef VITA

cvar_t	*r_worldVBO = NULL;
#ifdef USE_GXM_NATIVE
cvar_t	*r_gxmStats = NULL;
cvar_t	*r_gxmCullFlip = NULL;
#endif

#define WVBO_MAXGROUPS		64
#define WVBO_GROUPVERTS		65000					// u16 indices address one group
#define WVBO_STRIDE			32						// xyz(12) st(8) lm(8) rgba(4)
#define WVBO_OFS_ST			12
#define WVBO_OFS_LM			20
#define WVBO_OFS_RGBA		28

typedef struct {
#ifdef USE_GXM_NATIVE
	void	*data;					// GPU-mapped, read straight by sceGxmSetVertexStream
	SceUID	uid;
#else
	GLuint	vbo;
#endif
	int		numVerts;
} wvboGroup_t;

static wvboGroup_t	wvbo_groups[WVBO_MAXGROUPS];
static int			wvbo_numGroups;
static int			wvbo_bytes;
static qboolean		wvbo_failed;

// per-frame index scratch; one draw flushes it
#define WVBO_MAXIDX	(1 << 16)
static glIndex_t	wvbo_idx[WVBO_MAXIDX];
static int			wvbo_numIdx;
static int			wvbo_curGroup = -1;

/*
===============
R_WorldVBO_Clear
===============
*/
void R_WorldVBO_Clear( void )
{
	for ( int i = 0; i < wvbo_numGroups; i++ ) {
#ifdef USE_GXM_NATIVE
		if ( wvbo_groups[i].data ) {
			GXM_Free( wvbo_groups[i].uid );
		}
#else
		if ( wvbo_groups[i].vbo ) {
			glDeleteBuffers( 1, &wvbo_groups[i].vbo );
		}
#endif
	}
	memset( wvbo_groups, 0, sizeof( wvbo_groups ) );
	wvbo_numGroups = 0;
	wvbo_bytes = 0;
	wvbo_failed = qfalse;
	wvbo_numIdx = 0;
	wvbo_curGroup = -1;
}

// A fresh GL context invalidates every buffer name. GXM memblocks are owned by the
// process rather than a context, so there is nothing to forget there.
void R_WorldVBO_ContextReset( void )
{
#ifndef USE_GXM_NATIVE
	memset( wvbo_groups, 0, sizeof( wvbo_groups ) );
	wvbo_numGroups = 0;
	wvbo_bytes = 0;
#endif
}

// Only the static per-vertex data the GL actually reads can come from the buffer;
// anything view- or time-dependent has to keep running through tess.
static qboolean R_WorldVBO_ShaderEligible( const shader_t *shader )
{
	if ( shader->sky ) {
		return qfalse;					// sky runs its own stage iterator, not the generic one
	}
	if ( shader->polygonOffset ) {
		return qfalse;					// this path never sets GL_POLYGON_OFFSET_FILL
	}
	if ( shader->sort > SS_OPAQUE ) {
		return qfalse;					// blended surfaces need their draw order kept
	}
	if ( shader->numDeforms ) {
		return qfalse;					// deformVertexes rewrites xyz every frame
	}
	if ( shader->numUnfoggedPasses != 1 ) {
		return qfalse;
	}
	if ( shader->lightmapIndex[0] < 0 ) {
		return qfalse;					// vertex-lit colours fold animating light styles
	}

	const shaderStage_t *st = &shader->stages[0];
	if ( !st->active ) {
		return qfalse;
	}
	if ( st->rgbGen != CGEN_IDENTITY && st->rgbGen != CGEN_IDENTITY_LIGHTING ) {
		return qfalse;					// only the constant-colour cases need no array
	}
	// ParseStage rewrites identity to skip on script-authored stages
	if ( st->alphaGen != AGEN_IDENTITY && st->alphaGen != AGEN_SKIP ) {
		return qfalse;
	}
	if ( st->bundle[0].tcGen != TCGEN_TEXTURE || st->bundle[0].numTexMods ) {
		return qfalse;
	}
	if ( !st->bundle[1].image || st->bundle[1].tcGen != TCGEN_LIGHTMAP || st->bundle[1].numTexMods ) {
		return qfalse;					// the collapsed diffuse+lightmap pass
	}
	return qtrue;
}

/*
===============
R_BuildWorldVBO

Packs eligible planar surfaces into GPU vertex buffers once per map.
===============
*/
void R_BuildWorldVBO( world_t &worldData )
{
	if ( !r_worldVBO || !r_worldVBO->integer || wvbo_failed ) {
		return;
	}

	const int numSurfaces = worldData.numsurfaces;
	if ( numSurfaces <= 0 ) {
		return;
	}

	// the buffers below are GL work issued from the loading thread
	R_IssuePendingRenderCommands();
	R_WorldVBO_Clear();

	byte *verts = (byte *)R_Malloc( WVBO_GROUPVERTS * WVBO_STRIDE, TAG_TEMP_WORKSPACE, qfalse );
	int groupVerts = 0;
	int resident = 0, totalVerts = 0, totalIdx = 0;

	for ( int i = 0; i <= numSurfaces; i++ )
	{
		msurface_t	*surf = ( i < numSurfaces ) ? &worldData.surfaces[i] : NULL;
		srfSurfaceFace_t *face = NULL;

		if ( surf && *surf->data == SF_FACE && R_WorldVBO_ShaderEligible( surf->shader ) ) {
			face = (srfSurfaceFace_t *)surf->data;
		}

		// close only when this surface cannot fit, or the list ran out; an ineligible
		// surface is simply skipped, or the groups fragment to one surface each
		if ( groupVerts && ( i == numSurfaces
			|| ( face && groupVerts + face->numPoints > WVBO_GROUPVERTS ) ) )
		{
			if ( wvbo_numGroups >= WVBO_MAXGROUPS ) {
				break;
			}
#ifdef USE_GXM_NATIVE
			SceUID uid = 0;
			void *gpu = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
				groupVerts * WVBO_STRIDE, 4, SCE_GXM_MEMORY_ATTRIB_READ, &uid );
			if ( !gpu ) {
				wvbo_failed = qtrue;
				break;
			}
			memcpy( gpu, verts, groupVerts * WVBO_STRIDE );

			wvbo_groups[wvbo_numGroups].data = gpu;
			wvbo_groups[wvbo_numGroups].uid  = uid;
#else
			GLuint id = 0;
			glGenBuffers( 1, &id );
			if ( !id ) {
				wvbo_failed = qtrue;
				break;
			}
			glBindBuffer( GL_ARRAY_BUFFER, id );
			glBufferData( GL_ARRAY_BUFFER, groupVerts * WVBO_STRIDE, verts, GL_STATIC_DRAW );
			glBindBuffer( GL_ARRAY_BUFFER, 0 );

			wvbo_groups[wvbo_numGroups].vbo = id;
#endif
			wvbo_groups[wvbo_numGroups].numVerts = groupVerts;
			wvbo_bytes += groupVerts * WVBO_STRIDE;
			wvbo_numGroups++;
			groupVerts = 0;
		}

		// re-test against the group that is current now, which may be a fresh one
		if ( !face || groupVerts + face->numPoints > WVBO_GROUPVERTS ) {
			continue;
		}

		const int base = groupVerts;
		for ( int v = 0; v < face->numPoints; v++ )
		{
			const float	*p = face->points[v];
			byte		*dst = verts + ( base + v ) * WVBO_STRIDE;

			memcpy( dst, p, 3 * sizeof( float ) );					// xyz
			memcpy( dst + WVBO_OFS_ST, &p[3], 2 * sizeof( float ) );	// diffuse st
			memcpy( dst + WVBO_OFS_LM, &p[VERTEX_LM], 2 * sizeof( float ) );
			*(unsigned int *)( dst + WVBO_OFS_RGBA ) = *(const unsigned int *)&p[VERTEX_COLOR];
		}
		groupVerts += face->numPoints;

		// pre-offset the indices so a frame only memcpys them
		const int *src = (const int *)( (byte *)face + face->ofsIndices );
		glIndex_t *dst = (glIndex_t *)R_Hunk_Alloc( face->numIndices * sizeof( glIndex_t ), qfalse );
		for ( int n = 0; n < face->numIndices; n++ ) {
			dst[n] = (glIndex_t)( base + src[n] );
		}

		face->vboGroup = wvbo_numGroups;	// the group being filled
		face->vboIndexes = dst;
		resident++;
		totalVerts += face->numPoints;
		totalIdx += face->numIndices;
	}

	R_Free( verts );

	ri.Printf( PRINT_ALL, "world VBO: %i/%i surfaces resident, %i verts %i tris, %i groups, %.2f MB\n",
		resident, numSurfaces, totalVerts, totalIdx / 3, wvbo_numGroups,
		wvbo_bytes / (1024.0f * 1024.0f) );
}

/*
===============
R_WorldVBO_Surface

Appends a resident surface's indices. False means it has to go through tess.
===============
*/
qboolean R_WorldVBO_Surface( const srfSurfaceFace_t *face, int fogNum, int dlighted )
{
	if ( !r_worldVBO || !r_worldVBO->integer || wvbo_failed || !face->vboIndexes ) {
		return qfalse;
	}
	if ( fogNum || dlighted || face->dlightBits[backEnd.smpFrame] ) {
		return qfalse;					// the fog and dlight passes only exist on the tess path
	}
	if ( wvbo_numIdx + face->numIndices > WVBO_MAXIDX ) {
		return qfalse;						// scratch full; tess takes the remainder
	}
	if ( wvbo_curGroup >= 0 && wvbo_curGroup != face->vboGroup ) {
		return qfalse;						// a different buffer, so a different draw
	}

	wvbo_curGroup = face->vboGroup;
	memcpy( wvbo_idx + wvbo_numIdx, face->vboIndexes, face->numIndices * sizeof( glIndex_t ) );
	wvbo_numIdx += face->numIndices;
	return qtrue;
}

/*
===============
R_WorldVBO_Flush

Draws whatever the current batch accumulated.
===============
*/
void R_WorldVBO_Flush( shader_t *shader )
{
	if ( !wvbo_numIdx || wvbo_curGroup < 0 || wvbo_curGroup >= wvbo_numGroups ) {
		wvbo_numIdx = 0;
		wvbo_curGroup = -1;
		return;
	}

	const shaderStage_t *st = &shader->stages[0];

	GL_State( st->stateBits );
	GL_Cull( shader->cullType );

#ifdef USE_GXM_NATIVE
	GL_SelectTexture( 0 );
	R_BindAnimatedImage( &st->bundle[0] );
	GL_SelectTexture( 1 );
	GL_TexEnv( r_lightmap->integer ? GL_REPLACE : shader->multitextureEnv );
	R_BindAnimatedImage( &st->bundle[1] );

	// the gate only lets constant-colour stages through, so the tint is a uniform
	const float lit = ( st->rgbGen == CGEN_IDENTITY_LIGHTING ) ? tr.identityLight : 1.0f;
	GXM_SetConstantColor( lit, lit, lit, 1.0f );
	GXM_SetTexUnitCount( 2 );
	GXM_SetStateBits( glState.glStateBits );
	GXM_DrawStaticBuffer( wvbo_groups[wvbo_curGroup].data, wvbo_idx, wvbo_numIdx );
	GXM_SetTexUnitCount( 1 );
	GXM_SetConstantColor( 1.0f, 1.0f, 1.0f, 1.0f );

	GL_SelectTexture( 0 );
	wvbo_numIdx = 0;
	wvbo_curGroup = -1;
	return;
#else
	const GLuint vbo = wvbo_groups[wvbo_curGroup].vbo;

	glBindBuffer( GL_ARRAY_BUFFER, vbo );

	qglEnableClientState( GL_VERTEX_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, WVBO_STRIDE, (void *)0 );

	GL_SelectTexture( 0 );
	qglEnable( GL_TEXTURE_2D );
	R_BindAnimatedImage( &st->bundle[0] );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, WVBO_STRIDE, (void *)WVBO_OFS_ST );

	GL_SelectTexture( 1 );
	qglEnable( GL_TEXTURE_2D );
	GL_TexEnv( r_lightmap->integer ? GL_REPLACE : shader->multitextureEnv );
	R_BindAnimatedImage( &st->bundle[1] );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, WVBO_STRIDE, (void *)WVBO_OFS_LM );

	// the gate only lets constant-colour stages through
	qglDisableClientState( GL_COLOR_ARRAY );
	if ( st->rgbGen == CGEN_IDENTITY_LIGHTING ) {
		qglColor4f( tr.identityLight, tr.identityLight, tr.identityLight, 1.0f );
	} else {
		qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	}

	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	qglDrawRangeElements( GL_TRIANGLES, 0, wvbo_groups[wvbo_curGroup].numVerts - 1,
		wvbo_numIdx, GL_INDEX_TYPE, wvbo_idx );

	// unit 1 has to go back off, or the next single-texture pass (the sky) draws
	// through the lightmap still bound here
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglDisable( GL_TEXTURE_2D );

	GL_SelectTexture( 0 );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	wvbo_numIdx = 0;
	wvbo_curGroup = -1;
#endif
}

#endif	// VITA
