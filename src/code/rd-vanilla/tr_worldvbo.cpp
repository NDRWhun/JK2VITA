/*
===========================================================================
Copyright (C) 2026, JK2VITA contributors

This file is part of JK2VITA, a PS Vita port built on the OpenJK
source code.

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

// tr_worldvbo.cpp -- static world VBO (Vita, r_worldVBO). Eligible opaque world
// surfaces bake into a resident vertex buffer; indices stage per shader run.
// Ineligible/dlit/fogged fall back to tess. Grids bake at full LOD.

#include "../server/exe_headers.h"

#include "tr_local.h"

#ifdef VITA

// interleaved vertex: xyz f3 @0, diffuse st f2 @12, lightmap st f2 @20, rgba u8 @28 -> 32 bytes.
typedef struct {
	float	xyz[3];
	float	st0[2];
	float	st1[2];
	byte	rgba[4];
} wvboVert_t;
#define WVBO_STRIDE		((GLsizei)sizeof(wvboVert_t))
#define WVBO_OFS_XYZ	((const GLvoid *)0)
#define WVBO_OFS_ST0	((const GLvoid *)12)
#define WVBO_OFS_ST1	((const GLvoid *)20)
#define WVBO_OFS_RGBA	((const GLvoid *)28)

typedef struct {
	const void	*surfData;	// == msurface_t.data; render-time lookup key
	shader_t	*shader;
	int			firstIndex;	// into the baked index array
	int			numIndexes;
} wvboSurf_t;

// Not stored in tr/world_t: both are memset every map load, which would leak the GL buffer.
static struct {
	GLuint		vbo;
	glIndex_t	*idx;		// malloc; baked indices stay CPU-side, staged per shader run
	wvboSurf_t	*surfs;		// malloc
	int			numSurfs;
	int			*hash;		// malloc; surfData -> surfs index, -1 empty
	int			hashSize;	// power of two
	qboolean	ready;
} s_wvbo;

// backend-thread-only batch state; a run stages indices and flushes as one draw
// (per-surface draws drown in vitaGL's per-draw shader re-patch cost)
#define WVBO_STAGE_MAX 32768
static glIndex_t	s_wvboStage[WVBO_STAGE_MAX];
static int			s_wvboStaged;
static qboolean		s_wvboBatch;
static shader_t		*s_wvboBatchShader;

static void WorldVbo_Flush( void )
{
	if ( !s_wvboStaged ) {
		return;
	}
	qglDrawElements( GL_TRIANGLES, s_wvboStaged, GL_INDEX_TYPE, s_wvboStage );
	backEnd.pc.c_wvboDraws++;
	s_wvboStaged = 0;
}

extern bool g_bRenderGlowingObjects;	// glow prepass draws per-stage subsets; VBO can't

static qboolean WorldVbo_Eligible( const shader_t *sh, int surfaceType )
{
	if ( surfaceType != SF_FACE && surfaceType != SF_GRID ) return qfalse;
	if ( !sh || sh->defaultShader || !sh->stages ) return qfalse;
	if ( sh->sky || ( sh->surfaceFlags & SURF_SKY ) ) return qfalse;
	if ( sh->polygonOffset || sh->numDeforms != 0 ) return qfalse;
	if ( sh->sort != SS_OPAQUE || sh->numUnfoggedPasses != 1 ) return qfalse;

	const shaderStage_t *st = &sh->stages[0];
	if ( !st->active ) return qfalse;
	if ( st->ss && st->ss->surfaceSpriteType ) return qfalse;
	if ( st->stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_ATEST_BITS ) ) return qfalse;

	switch ( st->rgbGen ) {
	case CGEN_IDENTITY: case CGEN_IDENTITY_LIGHTING: case CGEN_CONST:
		break;
	case CGEN_VERTEX: case CGEN_EXACT_VERTEX:
		if ( sh->lightmapIndex[0] == LIGHTMAP_BY_VERTEX ) return qfalse;	// per-frame styleColors
		break;
	default:
		return qfalse;	// waveform/entity/lightingDiffuse/fog/lightmapstyle are per-frame
	}
	switch ( st->alphaGen ) {
	case AGEN_IDENTITY: case AGEN_SKIP: case AGEN_CONST: case AGEN_VERTEX:
		break;
	default:
		return qfalse;
	}
	// The bake hardwires st0=diffuse/st1=lightmap; admit only that exact layout
	// (CollapseMultitexture also merges non-lightmap stage pairs -> reject those).
	const textureBundle_t *b0 = &st->bundle[0];
	if ( !b0->image || b0->isVideoMap || b0->numImageAnimations > 1 ) return qfalse;
	if ( b0->tcGen != TCGEN_TEXTURE || b0->isLightmap ) return qfalse;
	if ( b0->numTexMods != 0 ) return qfalse;	// scroll/turb/etc are per-frame
	const textureBundle_t *b1 = &st->bundle[1];
	if ( b1->image ) {
		if ( b1->isVideoMap || b1->numImageAnimations > 1 ) return qfalse;
		if ( b1->tcGen != TCGEN_LIGHTMAP || !b1->isLightmap ) return qfalse;
		if ( b1->numTexMods != 0 ) return qfalse;
	}
	return qtrue;
}

// Reproduce ComputeColors for the eligible cgens. Eligible surfaces are opaque and
// unblended, so alpha is never sampled -> bake 255.
static void WorldVbo_BakeColor( const shaderStage_t *st, const byte *raw, byte *out )
{
	switch ( st->rgbGen ) {
	default:
	case CGEN_IDENTITY:
		out[0] = out[1] = out[2] = 255;
		break;
	case CGEN_IDENTITY_LIGHTING:
		out[0] = out[1] = out[2] = tr.identityLightByte;
		break;
	case CGEN_CONST:
		out[0] = st->constantColor[0]; out[1] = st->constantColor[1]; out[2] = st->constantColor[2];
		break;
	case CGEN_EXACT_VERTEX:
		out[0] = raw[0]; out[1] = raw[1]; out[2] = raw[2];
		break;
	case CGEN_VERTEX:
		if ( tr.identityLight == 1.0f ) {
			out[0] = raw[0]; out[1] = raw[1]; out[2] = raw[2];
		} else {
			out[0] = (byte)( raw[0] * tr.identityLight );
			out[1] = (byte)( raw[1] * tr.identityLight );
			out[2] = (byte)( raw[2] * tr.identityLight );
		}
		break;
	}
	out[3] = 255;
}

void R_FreeWorldVBO( void )
{
	if ( s_wvbo.vbo ) {
		// vitaGL keeps per-array vbo references (written through at draw); re-point
		// the arrays we used before deleting so no dangling buffer pointer survives.
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		qglVertexPointer( 3, GL_FLOAT, 16, NULL );
		qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, NULL );
		GL_SelectTexture( 1 );
		qglTexCoordPointer( 2, GL_FLOAT, 0, NULL );
		GL_SelectTexture( 0 );
		qglTexCoordPointer( 2, GL_FLOAT, 0, NULL );
	}
	if ( s_wvbo.vbo ) { glDeleteBuffers( 1, &s_wvbo.vbo ); s_wvbo.vbo = 0; }
	if ( s_wvbo.idx ) { free( s_wvbo.idx ); s_wvbo.idx = NULL; }
	if ( s_wvbo.surfs ) { free( s_wvbo.surfs ); s_wvbo.surfs = NULL; }
	if ( s_wvbo.hash )  { free( s_wvbo.hash );  s_wvbo.hash  = NULL; }
	s_wvbo.numSurfs = 0;
	s_wvbo.hashSize = 0;
	s_wvbo.ready = qfalse;
	s_wvboStaged = 0;
	s_wvboBatch = qfalse;
	s_wvboBatchShader = NULL;
}

// Two passes (count, then fill) select the identical surface set. Caller must have
// parked the render thread (the GL upload runs on the main thread).
void R_BuildWorldVBO( world_t *w )
{
	R_FreeWorldVBO();
	if ( !r_worldVBO || !r_worldVBO->integer || !w || w->numsurfaces <= 0 ) return;

	// world model (bmodels[0]) only: inline bmodel surfaces render as entities and
	// would be dead weight in the VBO (the entityNum guard never draws them).
	const int surfFirst = w->bmodels ? (int)( w->bmodels[0].firstSurface - w->surfaces ) : 0;
	const int surfLast  = w->bmodels ? surfFirst + w->bmodels[0].numSurfaces : w->numsurfaces;

	int totalVerts = 0, totalIdx = 0, numEl = 0;
	for ( int i = surfFirst; i < surfLast; i++ ) {
		msurface_t *s = &w->surfaces[i];
		if ( s->fogIndex != 0 || !s->data ) continue;
		surfaceType_t t = *(surfaceType_t *)s->data;
		if ( !WorldVbo_Eligible( s->shader, t ) ) continue;
		if ( t == SF_FACE ) {
			srfSurfaceFace_t *f = (srfSurfaceFace_t *)s->data;
			if ( f->numPoints <= 0 || f->numIndices <= 0 ) continue;
			totalVerts += f->numPoints;
			totalIdx   += f->numIndices;
		} else {
			srfGridMesh_t *g = (srfGridMesh_t *)s->data;
			if ( g->width < 2 || g->height < 2 ) continue;
			totalVerts += g->width * g->height;
			totalIdx   += ( g->width - 1 ) * ( g->height - 1 ) * 6;
		}
		numEl++;
	}
	if ( numEl == 0 || totalVerts == 0 || totalIdx == 0 ) return;

	wvboVert_t *verts = (wvboVert_t *)malloc( (size_t)totalVerts * sizeof(wvboVert_t) );
	glIndex_t  *idx   = (glIndex_t *)malloc( (size_t)totalIdx * sizeof(glIndex_t) );
	s_wvbo.surfs      = (wvboSurf_t *)malloc( (size_t)numEl * sizeof(wvboSurf_t) );
	if ( !verts || !idx || !s_wvbo.surfs ) {
		free( verts ); free( idx ); free( s_wvbo.surfs ); s_wvbo.surfs = NULL;
		return;
	}

	int vCount = 0, iCount = 0, rCount = 0;
	for ( int i = surfFirst; i < surfLast; i++ ) {
		msurface_t *s = &w->surfaces[i];
		if ( s->fogIndex != 0 || !s->data ) continue;
		surfaceType_t t = *(surfaceType_t *)s->data;
		if ( !WorldVbo_Eligible( s->shader, t ) ) continue;

		const shaderStage_t *st = &s->shader->stages[0];
		int baseVertex = vCount;
		int firstIndex = iCount;

		if ( t == SF_FACE ) {
			srfSurfaceFace_t *f = (srfSurfaceFace_t *)s->data;
			if ( f->numPoints <= 0 || f->numIndices <= 0 ) continue;
			const float *pv = f->points[0];
			for ( int p = 0; p < f->numPoints; p++, pv += VERTEXSIZE ) {
				wvboVert_t *o = &verts[vCount++];
				o->xyz[0] = pv[0]; o->xyz[1] = pv[1]; o->xyz[2] = pv[2];
				o->st0[0] = pv[3]; o->st0[1] = pv[4];
				o->st1[0] = pv[VERTEX_LM]; o->st1[1] = pv[VERTEX_LM + 1];
				WorldVbo_BakeColor( st, (const byte *)&pv[VERTEX_COLOR], o->rgba );
			}
			const int *si = (const int *)( (const byte *)f + f->ofsIndices );
			for ( int k = 0; k < f->numIndices; k++ ) idx[iCount++] = (glIndex_t)( si[k] + baseVertex );
		} else {
			srfGridMesh_t *g = (srfGridMesh_t *)s->data;
			if ( g->width < 2 || g->height < 2 ) continue;
			const int W = g->width, H = g->height;
			for ( int n = 0; n < W * H; n++ ) {
				const drawVert_t *dv = &g->verts[n];
				wvboVert_t *o = &verts[vCount++];
				o->xyz[0] = dv->xyz[0]; o->xyz[1] = dv->xyz[1]; o->xyz[2] = dv->xyz[2];
				o->st0[0] = dv->st[0]; o->st0[1] = dv->st[1];
				o->st1[0] = dv->lightmap[0][0]; o->st1[1] = dv->lightmap[0][1];
				WorldVbo_BakeColor( st, dv->color[0], o->rgba );
			}
			for ( int r = 0; r < H - 1; r++ ) {
				for ( int c = 0; c < W - 1; c++ ) {
					int v1 = baseVertex + r * W + c + 1;
					int v2 = v1 - 1;
					int v3 = v2 + W;
					int v4 = v3 + 1;
					idx[iCount++] = v2; idx[iCount++] = v3; idx[iCount++] = v1;
					idx[iCount++] = v1; idx[iCount++] = v3; idx[iCount++] = v4;
				}
			}
		}

		wvboSurf_t *rec = &s_wvbo.surfs[rCount++];
		rec->surfData   = s->data;
		rec->shader     = s->shader;
		rec->firstIndex = firstIndex;
		rec->numIndexes = iCount - firstIndex;
	}
	s_wvbo.numSurfs = rCount;

	int hs = 16;
	while ( hs < rCount * 2 ) hs <<= 1;
	s_wvbo.hash = (int *)malloc( (size_t)hs * sizeof(int) );
	if ( !s_wvbo.hash ) {	// low-memory map load: bail rather than deref NULL
		free( verts ); free( idx );
		R_FreeWorldVBO();
		return;
	}
	s_wvbo.hashSize = hs;
	for ( int h = 0; h < hs; h++ ) s_wvbo.hash[h] = -1;
	for ( int r = 0; r < rCount; r++ ) {
		unsigned h = (unsigned)( ( (uintptr_t)s_wvbo.surfs[r].surfData >> 4 ) & ( hs - 1 ) );
		while ( s_wvbo.hash[h] != -1 ) h = ( h + 1 ) & ( hs - 1 );
		s_wvbo.hash[h] = r;
	}

	size_t vramBefore = vglMemFree( VGL_MEM_VRAM );
	size_t ramBefore  = vglMemFree( VGL_MEM_RAM );

	glGenBuffers( 1, &s_wvbo.vbo );
	glBindBuffer( GL_ARRAY_BUFFER, s_wvbo.vbo );
	// DYNAMIC_DRAW: vitaGL then allocates RAM-first instead of VRAM-first, leaving
	// CDRAM for textures (a failed texture upload is silent and binds stale data).
	glBufferData( GL_ARRAY_BUFFER, (GLsizei)( vCount * sizeof(wvboVert_t) ), verts, GL_DYNAMIC_DRAW );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	free( verts );
	s_wvbo.idx = idx;	// indices stay CPU-side; staged per shader run at draw time
	s_wvbo.ready = qtrue;
	ri.Printf( PRINT_ALL, "r_worldVBO: baked %d surfaces (%d verts, %d indices, %d KB VBO)\n",
		rCount, vCount, iCount, (int)( ( vCount * sizeof(wvboVert_t) ) >> 10 ) );
	ri.Printf( PRINT_ALL, "r_worldVBO: VRAM free %u -> %u KB, RAM pool free %u -> %u KB\n",
		(unsigned)( vramBefore >> 10 ), (unsigned)( vglMemFree( VGL_MEM_VRAM ) >> 10 ),
		(unsigned)( ramBefore >> 10 ), (unsigned)( vglMemFree( VGL_MEM_RAM ) >> 10 ) );
}

static wvboSurf_t *WorldVbo_Lookup( const void *surfData )
{
	if ( !s_wvbo.ready || !s_wvbo.hash ) return NULL;
	unsigned h = (unsigned)( ( (uintptr_t)surfData >> 4 ) & ( s_wvbo.hashSize - 1 ) );
	for ( int probe = 0; probe < s_wvbo.hashSize; probe++ ) {
		int r = s_wvbo.hash[h];
		if ( r == -1 ) return NULL;
		if ( s_wvbo.surfs[r].surfData == surfData ) return &s_wvbo.surfs[r];
		h = ( h + 1 ) & ( s_wvbo.hashSize - 1 );
	}
	return NULL;
}

// Mirror DrawMultitextured's per-surface GL, but with the vertex VBO bound and byte
// offsets. Indices stay client-side (staged), so ELEMENT_ARRAY is never bound.
static void WorldVbo_SetupBatch( shader_t *sh )
{
	const shaderStage_t *st = &sh->stages[0];

	WorldVbo_Flush();	// stale staged indices must not draw with the new shader's state
	GL_Cull( sh->cullType );
	glBindBuffer( GL_ARRAY_BUFFER, s_wvbo.vbo );

	qglEnableClientState( GL_VERTEX_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, WVBO_STRIDE, WVBO_OFS_XYZ );
	qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, WVBO_STRIDE, WVBO_OFS_RGBA );

	GL_State( st->stateBits );

	GL_SelectTexture( 0 );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, WVBO_STRIDE, WVBO_OFS_ST0 );
	R_BindAnimatedImage( &st->bundle[0] );

	if ( st->bundle[1].image ) {
		GL_SelectTexture( 1 );
		qglEnable( GL_TEXTURE_2D );
		qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
		qglTexCoordPointer( 2, GL_FLOAT, WVBO_STRIDE, WVBO_OFS_ST1 );
		GL_TexEnv( sh->multitextureEnv );
		R_BindAnimatedImage( &st->bundle[1] );
	}

	s_wvboBatch = qtrue;
	s_wvboBatchShader = sh;
}

// End the current VBO batch: flush staged indices, undo the tmu1 enable and
// (critically) unbind the buffer, so tess client-pointer arrays aren't read as offsets.
void RB_EndWorldVBO( void )
{
	if ( !s_wvboBatch ) return;
	WorldVbo_Flush();
	if ( s_wvboBatchShader && s_wvboBatchShader->stages[0].bundle[1].image ) {
		GL_SelectTexture( 1 );
		qglDisable( GL_TEXTURE_2D );
		GL_SelectTexture( 0 );
	}
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	s_wvboBatch = qfalse;
	s_wvboBatchShader = NULL;
}

// Returns qtrue if the surface was staged for the VBO; qfalse -> caller draws via tess.
// A same-shader run accumulates indices and flushes as one draw at run end.
qboolean RB_TryWorldVBO( void *surface, shader_t *shader, int fogNum, int dlighted, int entityNum )
{
	if ( !s_wvbo.ready || !r_worldVBO->integer
		|| entityNum != REFENTITYNUM_WORLD || dlighted || fogNum || g_bRenderGlowingObjects
		|| r_lightmap->integer || r_showtris->integer || r_shownormals->integer || r_fullbright->integer ) {
		RB_EndWorldVBO();
		return qfalse;
	}
	wvboSurf_t *rec = WorldVbo_Lookup( surface );
	if ( !rec || rec->shader != shader ) {
		RB_EndWorldVBO();
		return qfalse;
	}
	if ( !s_wvboBatch || s_wvboBatchShader != shader ) {
		WorldVbo_SetupBatch( shader );
	}
	if ( s_wvboStaged + rec->numIndexes > WVBO_STAGE_MAX ) {
		WorldVbo_Flush();
		if ( rec->numIndexes > WVBO_STAGE_MAX ) {	// oversized surface: draw straight from the bake
			qglDrawElements( GL_TRIANGLES, rec->numIndexes, GL_INDEX_TYPE, s_wvbo.idx + rec->firstIndex );
			backEnd.pc.c_wvboDraws++;
			backEnd.pc.c_wvboSurfaces++;
			return qtrue;
		}
	}
	memcpy( s_wvboStage + s_wvboStaged, s_wvbo.idx + rec->firstIndex, (size_t)rec->numIndexes * sizeof(glIndex_t) );
	s_wvboStaged += rec->numIndexes;
	backEnd.pc.c_wvboSurfaces++;
	return qtrue;
}

#endif // VITA
