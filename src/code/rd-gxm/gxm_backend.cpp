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

// gxm_backend.cpp -- the renderer's draw path on sceGxm.
// A draw may start as soon as sceGxmDraw returns, so its data is complete before
// the call and stays untouched in the frame ring until the fence retires it.

#include "gxm_backend.h"
#include "gxm_device.h"
#include "gxm_texture.h"
#include "gxm_state.h"
#include "shaders/gxm_shaders.h"

#include <string.h>
#include <stdio.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

// rd-gxm cannot see tess, so the owner installs an accessor
static gxmTessArraysFn_t gxm_tessArrays;

void GXM_SetTessArraysHook( gxmTessArraysFn_t fn )
{
	gxm_tessArrays = fn;
}

// keyed by live texture, not by texnum: the engine's texnums start at 2048 and
// climb without reuse, so any fixed ceiling is only a question of when
#define GXM_MAX_TEXTURES	4096
#define GXM_TEXMAP_SIZE		8192		// power of two, kept under half full
#define GXM_SLOT_NONE		(-1)
#define GXM_SLOT_DEAD		(-2)		// tombstone; probing must walk past it
#define GXM_MAX_PROGRAMS	256

// one interleaved stream; the limit is 16, but packing keeps the program count down
typedef struct {
	float			xyz[3];
	float			uv0[2];
	float			uv1[2];
	unsigned char	rgba[4];
} gxmVert_t;

typedef struct {
	unsigned int			key;
	SceGxmFragmentProgram	*prog;
} gxmProgCache_t;

static gxmTexture_t		gxm_textures[GXM_MAX_TEXTURES];
static int				gxm_freeSlots[GXM_MAX_TEXTURES];
static int				gxm_numFree;
static unsigned int		gxm_mapKey[GXM_TEXMAP_SIZE];
static int				gxm_mapSlot[GXM_TEXMAP_SIZE];
static int				gxm_boundTex[2];		// resolved slots, not texnums

static unsigned int TexHash( unsigned int texnum )
{
	return ( texnum * 2654435761u ) & ( GXM_TEXMAP_SIZE - 1 );
}

static int TexFind( unsigned int texnum )
{
	unsigned int i = TexHash( texnum );
	for ( int n = 0; n < GXM_TEXMAP_SIZE; n++ ) {
		if ( gxm_mapSlot[i] == GXM_SLOT_NONE ) {
			return GXM_SLOT_NONE;
		}
		if ( gxm_mapSlot[i] >= 0 && gxm_mapKey[i] == texnum ) {
			return gxm_mapSlot[i];
		}
		i = ( i + 1 ) & ( GXM_TEXMAP_SIZE - 1 );
	}
	return GXM_SLOT_NONE;
}

// find or claim a slot; GXM_SLOT_NONE means the pool is genuinely full
static int TexClaim( unsigned int texnum )
{
	const int found = TexFind( texnum );
	if ( found >= 0 ) {
		return found;
	}
	if ( !gxm_numFree ) {
		return GXM_SLOT_NONE;
	}

	unsigned int i = TexHash( texnum );
	int dead = -1;
	for ( int n = 0; n < GXM_TEXMAP_SIZE; n++ ) {
		if ( gxm_mapSlot[i] == GXM_SLOT_DEAD && dead < 0 ) {
			dead = (int)i;
		}
		if ( gxm_mapSlot[i] == GXM_SLOT_NONE ) {
			if ( dead >= 0 ) {
				i = (unsigned int)dead;
			}
			gxm_mapKey[i]  = texnum;
			gxm_mapSlot[i] = gxm_freeSlots[--gxm_numFree];
			return gxm_mapSlot[i];
		}
		i = ( i + 1 ) & ( GXM_TEXMAP_SIZE - 1 );
	}
	return GXM_SLOT_NONE;
}

static void TexRelease( unsigned int texnum )
{
	unsigned int i = TexHash( texnum );
	for ( int n = 0; n < GXM_TEXMAP_SIZE; n++ ) {
		if ( gxm_mapSlot[i] == GXM_SLOT_NONE ) {
			return;
		}
		if ( gxm_mapSlot[i] >= 0 && gxm_mapKey[i] == texnum ) {
			gxm_freeSlots[gxm_numFree++] = gxm_mapSlot[i];
			gxm_mapSlot[i] = GXM_SLOT_DEAD;
			return;
		}
		i = ( i + 1 ) & ( GXM_TEXMAP_SIZE - 1 );
	}
}

static SceGxmShaderPatcherId	gxm_vertIds[3][2][2];	// [texcoord sets][vertex colour][fog]
// resolved once; the names are fixed at build time and the search is by string
static const SceGxmProgramParameter	*gxm_pMVP[3][2][2], *gxm_pColor[3][2][2], *gxm_pFogParams[3][2][2];
static const SceGxmProgramParameter	*gxm_pFogColor[3][3][5][2];
static SceGxmVertexProgram		*gxm_vertProgs[3][2][2];
static const SceGxmProgram		*gxm_vertBlobs[3][2][2];
static SceGxmShaderPatcherId	gxm_fragIds[3][3][5][2];	// [textures][env][alpha test][fog]
static const SceGxmProgram		*gxm_fragBlobs[3][3][5][2];

static gxmProgCache_t	gxm_progCache[GXM_MAX_PROGRAMS];
static int				gxm_progCount;

static float		gxm_proj[16], gxm_modelView[16], gxm_mvp[16];
static bool			gxm_mvpDirty = true;
static unsigned int	gxm_stateBits;
static int			gxm_texUnits = 1;
static int			gxm_vertexColor = 1;
static int			gxm_texEnv = GXM_TEXENV_MODULATE;
static int			gxm_cullFlip;
static int			gxm_fogOn;
static float		gxm_fogParams[4] = { 0, 1, 1, 0 };	// start, end, 1/(end-start)
static float		gxm_fogColor[4] = { 0, 0, 0, 1 };

// a pass that stages its own de-indexed arrays draws from these instead of tess
static const float			*gxm_ovXyz, *gxm_ovUv0, *gxm_ovUv1;
static const unsigned char	*gxm_ovRgba;
static float		gxm_constColor[4] = { 1, 1, 1, 1 };
static bool			gxm_backendOk;

// the viewport transform is rebuilt whenever either half of it moves
static int			gxm_viewX, gxm_viewY, gxm_viewW, gxm_viewH;
static float		gxm_depthScale = 0.5f, gxm_depthOffset = 0.5f;
// libgxm keeps programs, streams, textures and depth state until they change,
// so the last-set values are shadowed and only differences are re-issued
static SceGxmVertexProgram		*gxm_curVertProg;
static SceGxmFragmentProgram	*gxm_curFragProg;
static const SceGxmTexture		*gxm_curTex[2];
static gxmDepthState_t			gxm_curDepth;
static bool						gxm_depthKnown;
static bool						gxm_uniformsDirty = true;
static bool						gxm_fragUniformsDirty = true;

// what the backend actually did this run, reported by r_gxmStats
static int	gxm_statUploads, gxm_statDraws, gxm_statTextured, gxm_statNoTex, gxm_statRingFail;
static int	gxm_statDxtUploads;	// how many took the compressed path
static int	gxm_statSlotFail;	// textures refused because the pool is full
static int	gxm_statImmDraws;	// draws that came from a glBegin/glEnd block
static int	gxm_statProgFail;	// draws dropped because no fragment program resolved

static const SceGxmProgram *VertBlob( int nuv, int vcol, int fog )
{
	static const unsigned char *v[3][2][2] = {
		{ { gxs_generic_v_u0_c0_f0, gxs_generic_v_u0_c0_f1 }, { gxs_generic_v_u0_c1_f0, gxs_generic_v_u0_c1_f1 } },
		{ { gxs_generic_v_u1_c0_f0, gxs_generic_v_u1_c0_f1 }, { gxs_generic_v_u1_c1_f0, gxs_generic_v_u1_c1_f1 } },
		{ { gxs_generic_v_u2_c0_f0, gxs_generic_v_u2_c0_f1 }, { gxs_generic_v_u2_c1_f0, gxs_generic_v_u2_c1_f1 } },
	};
	return (const SceGxmProgram *)v[nuv][vcol][fog];
}

// GL_ADD only differs from GL_MODULATE once a second texture is in play
static const SceGxmProgram *FragBlob( int ntex, int env, int atest, int fog )
{
#define F(t,e,a) { gxs_generic_f_t##t##_e##e##_a##a##_f0, gxs_generic_f_t##t##_e##e##_a##a##_f1 }
	static const unsigned char *t0[5][2] = { F(0,0,0), F(0,0,1), F(0,0,2), F(0,0,3), F(0,0,4) };
	static const unsigned char *t1[5][2] = { F(1,0,0), F(1,0,1), F(1,0,2), F(1,0,3), F(1,0,4) };
	static const unsigned char *t2e0[5][2] = { F(2,0,0), F(2,0,1), F(2,0,2), F(2,0,3), F(2,0,4) };
	static const unsigned char *t2e1[5][2] = { F(2,1,0), F(2,1,1), F(2,1,2), F(2,1,3), F(2,1,4) };
	static const unsigned char *t2e2[5][2] = { F(2,2,0), F(2,2,1), F(2,2,2), F(2,2,3), F(2,2,4) };
#undef F

	if ( ntex <= 0 ) return (const SceGxmProgram *)t0[atest][fog];
	if ( ntex == 1 ) return (const SceGxmProgram *)t1[atest][fog];
	if ( env == GXM_TEXENV_REPLACE ) return (const SceGxmProgram *)t2e2[atest][fog];
	if ( env == GXM_TEXENV_ADD )     return (const SceGxmProgram *)t2e1[atest][fog];
	return (const SceGxmProgram *)t2e0[atest][fog];
}

/*
================
BuildVertexProgram

The patcher generates the attribute unpack, so one .gxp serves any stride.
================
*/
static SceGxmVertexProgram *BuildVertexProgram( const SceGxmProgram *blob,
												SceGxmShaderPatcherId id, int nuv )
{
	SceGxmVertexAttribute attrs[4];
	int n = 0;
	memset( attrs, 0, sizeof(attrs) );

	const SceGxmProgramParameter *p = sceGxmProgramFindParameterByName( blob, "aPosition" );
	if ( !p ) {
		return NULL;
	}
	attrs[n].streamIndex = 0; attrs[n].offset = 0;
	attrs[n].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; attrs[n].componentCount = 3;
	attrs[n].regIndex = sceGxmProgramParameterGetResourceIndex( p );
	n++;

	if ( nuv >= 1 && ( p = sceGxmProgramFindParameterByName( blob, "aTexCoord0" ) ) != NULL ) {
		attrs[n].streamIndex = 0; attrs[n].offset = 12;
		attrs[n].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; attrs[n].componentCount = 2;
		attrs[n].regIndex = sceGxmProgramParameterGetResourceIndex( p );
		n++;
	}
	if ( nuv >= 2 && ( p = sceGxmProgramFindParameterByName( blob, "aTexCoord1" ) ) != NULL ) {
		attrs[n].streamIndex = 0; attrs[n].offset = 20;
		attrs[n].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; attrs[n].componentCount = 2;
		attrs[n].regIndex = sceGxmProgramParameterGetResourceIndex( p );
		n++;
	}
	if ( ( p = sceGxmProgramFindParameterByName( blob, "aColor" ) ) != NULL ) {
		attrs[n].streamIndex = 0; attrs[n].offset = 28;
		attrs[n].format = SCE_GXM_ATTRIBUTE_FORMAT_U8N; attrs[n].componentCount = 4;
		attrs[n].regIndex = sceGxmProgramParameterGetResourceIndex( p );
		n++;
	}

	SceGxmVertexStream stream;
	memset( &stream, 0, sizeof(stream) );
	stream.stride      = sizeof(gxmVert_t);
	stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;

	SceGxmVertexProgram *out = NULL;
	if ( sceGxmShaderPatcherCreateVertexProgram( GXM_ShaderPatcher(), id,
			attrs, n, &stream, 1, &out ) < 0 ) {
		return NULL;
	}
	return out;
}

int GXM_BackendInit( void )
{
	memset( gxm_textures, 0, sizeof(gxm_textures) );
	for ( int i = 0; i < GXM_TEXMAP_SIZE; i++ ) {
		gxm_mapSlot[i] = GXM_SLOT_NONE;
	}
	gxm_numFree = 0;
	for ( int i = GXM_MAX_TEXTURES - 1; i >= 0; i-- ) {
		gxm_freeSlots[gxm_numFree++] = i;
	}
	gxm_boundTex[0] = gxm_boundTex[1] = GXM_SLOT_NONE;

	// the shader patcher keeps its registrations across a vid_restart
	if ( gxm_backendOk ) {
		GXM_InvalidateStateShadow();
		return 1;
	}

	memset( gxm_progCache, 0, sizeof(gxm_progCache) );
	gxm_progCount = 0;

	for ( int nuv = 0; nuv < 3; nuv++ ) {
		for ( int vcol = 0; vcol < 2; vcol++ ) {
			for ( int fog = 0; fog < 2; fog++ ) {
				const SceGxmProgram *b = VertBlob( nuv, vcol, fog );
				gxm_vertBlobs[nuv][vcol][fog] = b;
				if ( sceGxmShaderPatcherRegisterProgram( GXM_ShaderPatcher(),
						b, &gxm_vertIds[nuv][vcol][fog] ) < 0 ) {
					return 0;
				}
				gxm_vertProgs[nuv][vcol][fog] = BuildVertexProgram( b,
					gxm_vertIds[nuv][vcol][fog], nuv );
				if ( !gxm_vertProgs[nuv][vcol][fog] ) {
					return 0;
				}
				gxm_pMVP[nuv][vcol][fog]       = sceGxmProgramFindParameterByName( b, "uMVP" );
				gxm_pColor[nuv][vcol][fog]     = sceGxmProgramFindParameterByName( b, "uColor" );
				gxm_pFogParams[nuv][vcol][fog] = sceGxmProgramFindParameterByName( b, "uFogParams" );
			}
		}
	}

	for ( int t = 0; t < 3; t++ ) {
		// only the two-texture set has env variants; the rest alias env 0
		for ( int e = 0; e < ( t == 2 ? 3 : 1 ); e++ ) {
			for ( int a = 0; a < 5; a++ ) {
				for ( int fog = 0; fog < 2; fog++ ) {
					const SceGxmProgram *b = FragBlob( t, e, a, fog );
					gxm_fragBlobs[t][e][a][fog] = b;
					if ( sceGxmShaderPatcherRegisterProgram( GXM_ShaderPatcher(),
							b, &gxm_fragIds[t][e][a][fog] ) < 0 ) {
						return 0;
					}
					gxm_pFogColor[t][e][a][fog] = sceGxmProgramFindParameterByName( b, "uFogColor" );
				}
			}
		}
	}

	GXM_InvalidateStateShadow();
	GXM_SetStateResetCallback( GXM_InvalidateStateShadow );
	gxm_backendOk = true;
	return 1;
}

// the clear path sets programs and depth itself, so the shadow stops being true
void GXM_InvalidateStateShadow( void )
{
	gxm_curVertProg = NULL;
	gxm_curFragProg = NULL;
	gxm_curTex[0] = gxm_curTex[1] = NULL;
	gxm_depthKnown = false;
	gxm_uniformsDirty = true;
	gxm_fragUniformsDirty = true;
}

void GXM_BackendShutdown( void )
{
	gxm_backendOk = false;
	for ( unsigned int i = 0; i < GXM_MAX_TEXTURES; i++ ) {
		GXM_TextureFree( &gxm_textures[i] );
	}
}

// ---------------------------------------------------------------------------
// textures
// ---------------------------------------------------------------------------

// the slot address survives a re-upload, so a shadowed bind would go stale
static void ForgetTexture( int slot )
{
	for ( int t = 0; t < 2; t++ ) {
		if ( gxm_curTex[t] == &gxm_textures[slot].tex ) {
			gxm_curTex[t] = NULL;
		}
	}
}

void GXM_TexUpload( unsigned int texnum, const void *rgba, int width, int height )
{
	if ( width <= 0 || height <= 0 ) {
		return;
	}
	const int slot = TexClaim( texnum );
	if ( slot < 0 ) {
		gxm_statSlotFail++;
		return;
	}
	if ( GXM_TextureUpdateRGBA( &gxm_textures[slot], rgba, (unsigned)width, (unsigned)height ) ) {
		return;	// same size, so the pixels went straight in
	}
	ForgetTexture( slot );
	GXM_TextureFree( &gxm_textures[slot] );
	if ( GXM_TextureCreateRGBA( &gxm_textures[slot], rgba, (unsigned)width, (unsigned)height ) ) {
		gxm_statUploads++;
	}
}

int GXM_TexUploadDxt( unsigned int texnum, const void *blob, unsigned int size,
					   unsigned int width, unsigned int height, unsigned int mipCount, int isDxt5 )
{
	if ( !blob || !size ) {
		return 0;
	}
	const int slot = TexClaim( texnum );
	if ( slot < 0 ) {
		gxm_statSlotFail++;
		return 0;
	}
	ForgetTexture( slot );
	GXM_TextureFree( &gxm_textures[slot] );
	if ( !GXM_TextureCreateDxt( &gxm_textures[slot], blob, size, width, height,
			mipCount, isDxt5 != 0 ) ) {
		return 0;
	}
	gxm_statUploads++;
	gxm_statDxtUploads++;
	return 1;
}

void GXM_TexFree( unsigned int texnum )
{
	const int slot = TexFind( texnum );
	if ( slot >= 0 ) {
		ForgetTexture( slot );
		GXM_TextureFree( &gxm_textures[slot] );
		TexRelease( texnum );
	}
}

// resolved once here rather than per draw; GL_Bind already collapses repeats
void GXM_TexBind( int tmu, unsigned int texnum )
{
	if ( tmu >= 0 && tmu < 2 ) {
		gxm_boundTex[tmu] = TexFind( texnum );
	}
}

void GXM_TexFilter( unsigned int texnum, int linear, int clampToEdge )
{
	const int slot = TexFind( texnum );
	if ( slot >= 0 ) {
		GXM_TextureSetFilter( &gxm_textures[slot], linear != 0, clampToEdge != 0 );
	}
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

// GL matrices are column-major and the shader does mul(v, uMVP), so the product
// is taken in the order the engine already stores them
static void MulMat( float *out, const float *a, const float *b )
{
	for ( int c = 0; c < 4; c++ ) {
		for ( int r = 0; r < 4; r++ ) {
			float v = 0.0f;
			for ( int k = 0; k < 4; k++ ) {
				v += a[k * 4 + r] * b[c * 4 + k];
			}
			out[c * 4 + r] = v;
		}
	}
}

void GXM_SetProjection( const float *m )
{
	memcpy( gxm_proj, m, sizeof(gxm_proj) );
	gxm_mvpDirty = true;
}

void GXM_SetModelView( const float *m )
{
	memcpy( gxm_modelView, m, sizeof(gxm_modelView) );
	gxm_mvpDirty = true;
}

void GXM_SetStateBits( unsigned int stateBits )	{ gxm_stateBits = stateBits; }
void GXM_SetTexUnitCount( int count )			{ gxm_texUnits = count; }
void GXM_SetVertexColorEnabled( int enabled )	{ gxm_vertexColor = enabled; }
// clamped: env indexes the fragment program table
void GXM_SetTexEnv( int env )					{ gxm_texEnv = ( env >= 0 && env <= GXM_TEXENV_REPLACE ) ? env : GXM_TEXENV_MODULATE; }

// linear fog only; start/end are eye distances, matching GL_LINEAR
void GXM_SetFog( int enabled, float start, float end, const float *color )
{
	gxm_fogOn = enabled;
	if ( !enabled ) {
		return;
	}
	gxm_fogParams[0] = start;
	gxm_fogParams[1] = end;
	gxm_fogParams[2] = ( end > start ) ? 1.0f / ( end - start ) : 1.0f;
	if ( color ) {
		gxm_fogColor[0] = color[0]; gxm_fogColor[1] = color[1];
		gxm_fogColor[2] = color[2]; gxm_fogColor[3] = 1.0f;
		gxm_fragUniformsDirty = true;
	}
	gxm_uniformsDirty = true;
}

// xyz is 4 floats per vertex, uv 2, rgba 4 bytes; cleared by the next draw
void GXM_SetVertexArrays( const float *xyz, const float *uv0, const float *uv1,
						  const unsigned char *rgba )
{
	gxm_ovXyz = xyz; gxm_ovUv0 = uv0; gxm_ovUv1 = uv1; gxm_ovRgba = rgba;
}

/*
================
GXM_SetDepthBias

GL's polygon offset; there is no enable, so zero is the off state.
================
*/
void GXM_SetDepthBias( float factor, float units )
{
	int f = (int)factor, u = (int)units;
	if ( f < -16 ) f = -16; else if ( f > 15 ) f = 15;
	if ( u < -16 ) u = -16; else if ( u > 15 ) u = 15;

	sceGxmSetFrontDepthBias( GXM_Context(), f, u );
	sceGxmSetBackDepthBias( GXM_Context(), f, u );
}

void GXM_SetConstantColor( float r, float g, float b, float a )
{
	if ( gxm_constColor[0] != r || gxm_constColor[1] != g
		|| gxm_constColor[2] != b || gxm_constColor[3] != a ) {
		gxm_constColor[0] = r; gxm_constColor[1] = g;
		gxm_constColor[2] = b; gxm_constColor[3] = a;
		gxm_uniformsDirty = true;
	}
}

void GXM_SetCullFlip( int flip )					{ gxm_cullFlip = flip; }

/*
================
GXM_SetCull

GL measures winding bottom-up and GXM top-down, so the sense inverts.
================
*/
void GXM_SetCull( int glCullMode, int enabled )
{
	if ( !enabled ) {
		sceGxmSetCullMode( GXM_Context(), SCE_GXM_CULL_NONE );
		return;
	}

	// GL_FRONT is 0x0404
	int cullFront = ( glCullMode == 0x0404 );
	if ( gxm_cullFlip ) {
		cullFront = !cullFront;
	}
	sceGxmSetCullMode( GXM_Context(),
		cullFront ? SCE_GXM_CULL_CW : SCE_GXM_CULL_CCW );
}

/*
================
GXM_SetViewport

Takes GL's bottom-left rectangle and pins the NDC conventions explicitly.
================
*/
void GXM_SetViewport( int x, int y, int w, int h )
{
	if ( w <= 0 || h <= 0 ) {
		return;
	}
	const int top = GXM_DISPLAY_HEIGHT - ( y + h );

	sceGxmSetViewport( GXM_Context(),
		(float)x + (float)w * 0.5f,      (float)w * 0.5f,
		(float)top + (float)h * 0.5f, -( (float)h * 0.5f ),
		gxm_depthOffset, gxm_depthScale );

	// region clip is unsigned, so an off-screen origin wraps and kills every tile
	int cx0 = x, cy0 = top, cx1 = x + w - 1, cy1 = top + h - 1;
	if ( cx0 < 0 ) cx0 = 0;
	if ( cy0 < 0 ) cy0 = 0;
	if ( cx1 > GXM_DISPLAY_WIDTH  - 1 ) cx1 = GXM_DISPLAY_WIDTH  - 1;
	if ( cy1 > GXM_DISPLAY_HEIGHT - 1 ) cy1 = GXM_DISPLAY_HEIGHT - 1;
	if ( cx1 < cx0 || cy1 < cy0 ) {
		cx0 = cy0 = cx1 = cy1 = 0;	// entirely off screen
	}

	sceGxmSetRegionClip( GXM_Context(), SCE_GXM_REGION_CLIP_OUTSIDE,
		(unsigned)cx0, (unsigned)cy0, (unsigned)cx1, (unsigned)cy1 );

	gxm_viewX = x; gxm_viewY = top; gxm_viewW = w; gxm_viewH = h;
}

/*
================
GXM_SetDepthRange

qglDepthRange's near/far, folded into the viewport's Z transform.
================
*/
void GXM_SetDepthRange( float zNear, float zFar )
{
	gxm_depthScale  = ( zFar - zNear ) * 0.5f;
	gxm_depthOffset = ( zFar + zNear ) * 0.5f;

	if ( gxm_viewW > 0 ) {
		sceGxmSetViewport( GXM_Context(),
			(float)gxm_viewX + (float)gxm_viewW * 0.5f,      (float)gxm_viewW * 0.5f,
			(float)gxm_viewY + (float)gxm_viewH * 0.5f, -( (float)gxm_viewH * 0.5f ),
			gxm_depthOffset, gxm_depthScale );
	}
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

static SceGxmFragmentProgram *ResolveFragment( int ntex, int env, int vcol, int fog,
											   const gxmProgramKey_t *key )
{
	const unsigned int hash = ( GXM_ProgramKeyHash( key ) << 6 )
		| ( (unsigned)ntex << 4 ) | ( (unsigned)env << 2 )
		| ( (unsigned)vcol << 1 ) | (unsigned)fog;

	for ( int i = 0; i < gxm_progCount; i++ ) {
		if ( gxm_progCache[i].key == hash ) {
			return gxm_progCache[i].prog;
		}
	}
	if ( gxm_progCount >= GXM_MAX_PROGRAMS ) {
		gxm_statProgFail++;
		return NULL;
	}

	SceGxmFragmentProgram *prog = NULL;
	if ( sceGxmShaderPatcherCreateFragmentProgram( GXM_ShaderPatcher(),
			gxm_fragIds[ntex][env][key->alphaTest][fog],
			SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
			key->blended ? &key->blend : NULL,
			gxm_vertBlobs[ntex][vcol][fog], &prog ) < 0 ) {	// links the fragment texcoords to the program that will be bound
		gxm_statProgFail++;
		return NULL;
	}

	gxm_progCache[gxm_progCount].key  = hash;
	gxm_progCache[gxm_progCount].prog = prog;
	gxm_progCount++;
	return prog;
}

void GXM_DrawTess( int numIndexes, const unsigned short *indexes, int numVertexes )
{
	// the override usually points at caller stack, so it is taken and cleared
	// before any early return can leave it dangling for the next draw
	const float *ovXyz = gxm_ovXyz, *ovUv0 = gxm_ovUv0, *ovUv1 = gxm_ovUv1;
	const unsigned char *ovRgba = gxm_ovRgba;
	gxm_ovXyz = gxm_ovUv0 = gxm_ovUv1 = NULL;
	gxm_ovRgba = NULL;

	if ( !gxm_backendOk || numIndexes <= 0 || numVertexes <= 0 ) {
		return;
	}

	const float *xyz = NULL, *uv0 = NULL, *uv1 = NULL;
	const unsigned char *rgba = NULL;
	if ( gxm_tessArrays ) {
		gxm_tessArrays( &xyz, &uv0, &uv1, &rgba );
	}
	if ( ovXyz ) {
		xyz = ovXyz; uv0 = ovUv0; uv1 = ovUv1; rgba = ovRgba;
	}
	if ( !xyz ) {
		return;
	}

	gxmProgramKey_t key;
	gxmDepthState_t depth;
	GXM_TranslateState( gxm_stateBits, &key, &depth );

	int ntex = gxm_texUnits;
	if ( ntex > 2 ) ntex = 2;
	if ( ntex < 0 ) ntex = 0;
	gxm_statDraws++;
	const int wanted = ntex;
	for ( int t = 0; t < ntex; t++ ) {
		if ( gxm_boundTex[t] < 0 || !gxm_textures[ gxm_boundTex[t] ].valid ) {
			ntex = t;	// an unbacked unit would sample garbage; drop it and any above
			break;
		}
	}
	if ( wanted >= 1 ) {
		if ( ntex >= 1 ) {
			gxm_statTextured++;
		} else {
			gxm_statNoTex++;
		}
	}

	const int nuv  = ntex;
	const int vcol = ( rgba && gxm_vertexColor ) ? 1 : 0;
	const int env  = ( ntex >= 2 ) ? gxm_texEnv : 0;
	const int fog  = gxm_fogOn ? 1 : 0;

	SceGxmFragmentProgram *frag = ResolveFragment( ntex, env, vcol, fog, &key );
	if ( !frag ) {
		return;
	}
	gxmVert_t *v = (gxmVert_t *)GXM_RingAlloc( numVertexes * sizeof(gxmVert_t), 4 );
	unsigned short *idx = (unsigned short *)GXM_RingAlloc( numIndexes * sizeof(unsigned short), 2 );
	if ( !v || !idx ) {
		gxm_statRingFail++;
		return;	// ring exhausted; dropping the draw beats scribbling on the GPU
	}

	for ( int i = 0; i < numVertexes; i++ ) {
		v[i].xyz[0] = xyz[i * 4 + 0];	// tess.xyz is vec4 for SIMD alignment
		v[i].xyz[1] = xyz[i * 4 + 1];
		v[i].xyz[2] = xyz[i * 4 + 2];
		v[i].uv0[0] = uv0 ? uv0[i * 2 + 0] : 0.0f;
		v[i].uv0[1] = uv0 ? uv0[i * 2 + 1] : 0.0f;
		if ( nuv >= 2 && uv1 ) {	// otherwise the program never reads it
			v[i].uv1[0] = uv1[i * 2 + 0];
			v[i].uv1[1] = uv1[i * 2 + 1];
		}
		if ( vcol ) {
			memcpy( v[i].rgba, &rgba[i * 4], 4 );
		}	// otherwise the program has no aColor and the bytes are never read
	}
	memcpy( idx, indexes, numIndexes * sizeof(unsigned short) );

	if ( gxm_mvpDirty ) {
		MulMat( gxm_mvp, gxm_proj, gxm_modelView );
		gxm_mvpDirty = false;
		gxm_uniformsDirty = true;
	}

	SceGxmVertexProgram *vp = gxm_vertProgs[nuv][vcol][fog];
	bool progChanged = false;
	if ( gxm_curVertProg != vp ) {
		sceGxmSetVertexProgram( GXM_Context(), vp );
		gxm_curVertProg = vp;
		progChanged = true;
	}
	bool fragProgChanged = false;
	if ( gxm_curFragProg != frag ) {
		sceGxmSetFragmentProgram( GXM_Context(), frag );
		gxm_curFragProg = frag;
		fragProgChanged = true;
	}
	if ( !gxm_depthKnown || gxm_curDepth.depthFunc != depth.depthFunc
		|| gxm_curDepth.depthWrite != depth.depthWrite
		|| gxm_curDepth.wireframe != depth.wireframe ) {
		GXM_ApplyDepthState( &depth );
		gxm_curDepth  = depth;
		gxm_depthKnown = true;
	}

	// a new program drops the reservation, so the uniforms must be written again
	if ( progChanged || gxm_uniformsDirty ) {
		void *uniforms = NULL;
		sceGxmReserveVertexDefaultUniformBuffer( GXM_Context(), &uniforms );
		if ( uniforms ) {
			const SceGxmProgramParameter *pm = gxm_pMVP[nuv][vcol][fog];
			const SceGxmProgramParameter *pc = gxm_pColor[nuv][vcol][fog];
			const SceGxmProgramParameter *pf = gxm_pFogParams[nuv][vcol][fog];
			if ( pm ) sceGxmSetUniformDataF( uniforms, pm, 0, 16, gxm_mvp );
			if ( pc ) sceGxmSetUniformDataF( uniforms, pc, 0, 4, gxm_constColor );
			if ( pf ) sceGxmSetUniformDataF( uniforms, pf, 0, 4, gxm_fogParams );
			gxm_uniformsDirty = false;
		}
	}

	for ( int t = 0; t < ntex; t++ ) {
		const int tn = gxm_boundTex[t];
		if ( tn >= 0 && gxm_textures[tn].valid && gxm_curTex[t] != &gxm_textures[tn].tex ) {
			sceGxmSetFragmentTexture( GXM_Context(), t, &gxm_textures[tn].tex );
			gxm_curTex[t] = &gxm_textures[tn].tex;
		}
	}

	if ( fog && ( fragProgChanged || gxm_fragUniformsDirty ) ) {
		void *funi = NULL;
		sceGxmReserveFragmentDefaultUniformBuffer( GXM_Context(), &funi );
		const SceGxmProgramParameter *pfc = gxm_pFogColor[ntex][env][key.alphaTest][fog];
		if ( funi && pfc ) {
			sceGxmSetUniformDataF( funi, pfc, 0, 4, gxm_fogColor );
			gxm_fragUniformsDirty = false;
		}
	}

	sceGxmSetVertexStream( GXM_Context(), 0, v );
	sceGxmDraw( GXM_Context(), SCE_GXM_PRIMITIVE_TRIANGLES,
		SCE_GXM_INDEX_FORMAT_U16, idx, numIndexes );
}

/*
================
GXM_DrawStaticBuffer

Draws resident vertices, so only the indices go through the ring.
================
*/
void GXM_DrawStaticBuffer( const void *vertexBuffer, const unsigned short *indexes,
						   int numIndexes, int vertexColor )
{
	gxm_ovXyz = gxm_ovUv0 = gxm_ovUv1 = NULL;
	gxm_ovRgba = NULL;

	if ( !gxm_backendOk || !vertexBuffer || numIndexes <= 0 ) {
		return;
	}

	gxmProgramKey_t key;
	gxmDepthState_t depth;
	GXM_TranslateState( gxm_stateBits, &key, &depth );

	int ntex = gxm_texUnits;
	if ( ntex > 2 ) ntex = 2;
	if ( ntex < 0 ) ntex = 0;
	for ( int t = 0; t < ntex; t++ ) {
		if ( gxm_boundTex[t] < 0 || !gxm_textures[ gxm_boundTex[t] ].valid ) {
			ntex = t;	// an unbacked unit would sample garbage; drop it and any above
			break;
		}
	}

	// a lightmapped batch tints from a uniform; a vertex-lit one reads the stream
	const int vcol = ( vertexColor && gxm_vertexColor ) ? 1 : 0;
	const int env = ( ntex >= 2 ) ? gxm_texEnv : 0;
	const int fog = gxm_fogOn ? 1 : 0;
	SceGxmFragmentProgram *frag = ResolveFragment( ntex, env, vcol, fog, &key );
	if ( !frag ) {
		return;
	}

	unsigned short *idx = (unsigned short *)GXM_RingAlloc(
		numIndexes * sizeof(unsigned short), 2 );
	if ( !idx ) {
		gxm_statRingFail++;
		return;
	}
	memcpy( idx, indexes, numIndexes * sizeof(unsigned short) );

	if ( gxm_mvpDirty ) {
		MulMat( gxm_mvp, gxm_proj, gxm_modelView );
		gxm_mvpDirty = false;
		gxm_uniformsDirty = true;
	}

	SceGxmVertexProgram *vp = gxm_vertProgs[ntex][vcol][fog];
	bool progChanged = false;
	if ( gxm_curVertProg != vp ) {
		sceGxmSetVertexProgram( GXM_Context(), vp );
		gxm_curVertProg = vp;
		progChanged = true;
	}
	bool fragProgChanged = false;
	if ( gxm_curFragProg != frag ) {
		sceGxmSetFragmentProgram( GXM_Context(), frag );
		gxm_curFragProg = frag;
		fragProgChanged = true;
	}
	if ( !gxm_depthKnown || gxm_curDepth.depthFunc != depth.depthFunc
		|| gxm_curDepth.depthWrite != depth.depthWrite
		|| gxm_curDepth.wireframe != depth.wireframe ) {
		GXM_ApplyDepthState( &depth );
		gxm_curDepth  = depth;
		gxm_depthKnown = true;
	}

	// a new program drops the reservation, so the uniforms must be written again
	if ( progChanged || gxm_uniformsDirty ) {
		void *uniforms = NULL;
		sceGxmReserveVertexDefaultUniformBuffer( GXM_Context(), &uniforms );
		if ( uniforms ) {
			const SceGxmProgramParameter *pm = gxm_pMVP[ntex][vcol][fog];
			const SceGxmProgramParameter *pc = gxm_pColor[ntex][vcol][fog];
			const SceGxmProgramParameter *pf = gxm_pFogParams[ntex][vcol][fog];
			if ( pm ) sceGxmSetUniformDataF( uniforms, pm, 0, 16, gxm_mvp );
			if ( pc ) sceGxmSetUniformDataF( uniforms, pc, 0, 4, gxm_constColor );
			if ( pf ) sceGxmSetUniformDataF( uniforms, pf, 0, 4, gxm_fogParams );
			gxm_uniformsDirty = false;
		}
	}

	for ( int t = 0; t < ntex; t++ ) {
		const int tn = gxm_boundTex[t];
		if ( tn >= 0 && gxm_curTex[t] != &gxm_textures[tn].tex ) {
			sceGxmSetFragmentTexture( GXM_Context(), t, &gxm_textures[tn].tex );
			gxm_curTex[t] = &gxm_textures[tn].tex;
		}
	}

	gxm_statDraws++;
	if ( fog && ( fragProgChanged || gxm_fragUniformsDirty ) ) {
		void *funi = NULL;
		sceGxmReserveFragmentDefaultUniformBuffer( GXM_Context(), &funi );
		const SceGxmProgramParameter *pfc = gxm_pFogColor[ntex][env][key.alphaTest][fog];
		if ( funi && pfc ) {
			sceGxmSetUniformDataF( funi, pfc, 0, 4, gxm_fogColor );
			gxm_fragUniformsDirty = false;
		}
	}

	sceGxmSetVertexStream( GXM_Context(), 0, vertexBuffer );
	sceGxmDraw( GXM_Context(), SCE_GXM_PRIMITIVE_TRIANGLES,
		SCE_GXM_INDEX_FORMAT_U16, idx, numIndexes );
}

// ---------------------------------------------------------------------------
// immediate mode
//
// glBegin/glVertex geometry accumulates here and leaves as one draw.
// ---------------------------------------------------------------------------

#define GXM_IMM_MAX_VERTS	16384

static float			gxm_immXyz[GXM_IMM_MAX_VERTS][4];
static float			gxm_immUv[GXM_IMM_MAX_VERTS][2];
static unsigned char	gxm_immRgba[GXM_IMM_MAX_VERTS][4];
static unsigned short	gxm_immIdx[GXM_IMM_MAX_VERTS * 3];
static int				gxm_immCount;
static unsigned int		gxm_immMode;
static bool				gxm_immActive;
static float			gxm_immCurUv[2];
static int				gxm_immSavedUnits = 1;
static unsigned char	gxm_immCurRgba[4] = { 255, 255, 255, 255 };

void GXM_ImmBegin( unsigned int glMode )
{
	gxm_immMode   = glMode;
	gxm_immCount  = 0;
	gxm_immActive = true;

	// one texcoord set is staged, so a second unit would sample an unwritten uv1
	gxm_immSavedUnits = gxm_texUnits;
	if ( gxm_texUnits > 1 ) {
		gxm_texUnits = 1;
	}
}

void GXM_ImmTexCoord2f( float s, float t )
{
	gxm_immCurUv[0] = s; gxm_immCurUv[1] = t;
}

void GXM_ImmColor4f( float r, float g, float b, float a )
{
	gxm_immCurRgba[0] = (unsigned char)( r * 255.0f );
	gxm_immCurRgba[1] = (unsigned char)( g * 255.0f );
	gxm_immCurRgba[2] = (unsigned char)( b * 255.0f );
	gxm_immCurRgba[3] = (unsigned char)( a * 255.0f );
}

void GXM_ImmColor4ubv( const unsigned char *c )
{
	gxm_immCurRgba[0] = c[0]; gxm_immCurRgba[1] = c[1];
	gxm_immCurRgba[2] = c[2]; gxm_immCurRgba[3] = c[3];
}

void GXM_ImmVertex3f( float x, float y, float z )
{
	if ( !gxm_immActive || gxm_immCount >= GXM_IMM_MAX_VERTS ) {
		return;
	}
	const int i = gxm_immCount++;
	gxm_immXyz[i][0] = x; gxm_immXyz[i][1] = y; gxm_immXyz[i][2] = z; gxm_immXyz[i][3] = 1.0f;
	gxm_immUv[i][0] = gxm_immCurUv[0]; gxm_immUv[i][1] = gxm_immCurUv[1];
	memcpy( gxm_immRgba[i], gxm_immCurRgba, 4 );
}

// GL_TRIANGLES 0x0004, GL_TRIANGLE_STRIP 0x0005, GL_TRIANGLE_FAN 0x0006, GL_QUADS 0x0007
void GXM_ImmEnd( void )
{
	if ( !gxm_immActive ) {
		return;
	}
	gxm_immActive = false;

	int ni = 0;
	switch ( gxm_immMode ) {
	case 0x0007:
		for ( int q = 0; q + 3 < gxm_immCount; q += 4 ) {
			gxm_immIdx[ni++] = (unsigned short)( q + 0 );
			gxm_immIdx[ni++] = (unsigned short)( q + 1 );
			gxm_immIdx[ni++] = (unsigned short)( q + 2 );
			gxm_immIdx[ni++] = (unsigned short)( q + 0 );
			gxm_immIdx[ni++] = (unsigned short)( q + 2 );
			gxm_immIdx[ni++] = (unsigned short)( q + 3 );
		}
		break;
	case 0x0004:
		for ( int v = 0; v + 2 < gxm_immCount; v += 3 ) {
			gxm_immIdx[ni++] = (unsigned short)( v + 0 );
			gxm_immIdx[ni++] = (unsigned short)( v + 1 );
			gxm_immIdx[ni++] = (unsigned short)( v + 2 );
		}
		break;
	case 0x0005:
		for ( int v = 0; v + 2 < gxm_immCount; v++ ) {
			const int odd = v & 1;
			gxm_immIdx[ni++] = (unsigned short)( v + 0 );
			gxm_immIdx[ni++] = (unsigned short)( v + ( odd ? 2 : 1 ) );
			gxm_immIdx[ni++] = (unsigned short)( v + ( odd ? 1 : 2 ) );
		}
		break;
	case 0x0006:
		for ( int v = 1; v + 1 < gxm_immCount; v++ ) {
			gxm_immIdx[ni++] = 0;
			gxm_immIdx[ni++] = (unsigned short)( v + 0 );
			gxm_immIdx[ni++] = (unsigned short)( v + 1 );
		}
		break;
	default:
		ni = 0;	// lines and points have no triangle expansion
		break;
	}

	if ( ni ) {
		GXM_SetVertexArrays( &gxm_immXyz[0][0], &gxm_immUv[0][0], NULL, &gxm_immRgba[0][0] );
		GXM_DrawTess( ni, gxm_immIdx, gxm_immCount );
		gxm_statImmDraws++;
	}

	gxm_texUnits = gxm_immSavedUnits;
}

// the engine log is not flushed on an abrupt exit, so stats get their own file
void GXM_LogStatsLine( const char *line )
{
	static int opened;	// one run per file, or it grows without bound
	sceIoMkdir( "ux0:data/JK2VITA", 0777 );
	SceUID f = sceIoOpen( "ux0:data/JK2VITA/gxm_stats.log",
		SCE_O_WRONLY | SCE_O_CREAT | ( opened ? SCE_O_APPEND : SCE_O_TRUNC ), 0777 );
	opened = 1;
	if ( f >= 0 ) {
		sceIoWrite( f, line, strlen( line ) );
		sceIoClose( f );
	}
}

// a picture of what the backend actually did, for r_gxmStats
void GXM_ReportStats( char *out, int outSize )
{
	extern int gxm_texAllocFail, gxm_texInitFail;
	extern unsigned int gxm_texBytes;
	snprintf( out, outSize,
		"GXM: uploads=%d dxt=%d allocfail=%d initfail=%d slotfail=%d texmem=%uMB | draws=%d imm=%d textured=%d notex=%d ringfail=%d ring=%uKB/%uKB | progs=%d/%d progfail=%d fragusse=%uKB\n",
		gxm_statUploads, gxm_statDxtUploads, gxm_texAllocFail, gxm_texInitFail,
		gxm_statSlotFail, gxm_texBytes / ( 1024 * 1024 ),
		gxm_statDraws, gxm_statImmDraws, gxm_statTextured, gxm_statNoTex,
		gxm_statRingFail, GXM_RingUsedLastFrame() / 1024, GXM_RingBytesPerFrame() / 1024,
		gxm_progCount, GXM_MAX_PROGRAMS, gxm_statProgFail,
		(unsigned)( sceGxmShaderPatcherGetFragmentUsseMemAllocated( GXM_ShaderPatcher() ) / 1024 ) );

	gxm_statDraws = gxm_statImmDraws = gxm_statTextured = gxm_statNoTex = gxm_statRingFail = 0;
	gxm_statProgFail = 0;

	GXM_LogStatsLine( out );
}
