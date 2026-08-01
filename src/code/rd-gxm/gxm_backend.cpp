/*
===========================================================================
Copyright (C) 2026 JK2VITA contributors

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
//
// tess is rebuilt every surface, and GXM reads draw data asynchronously at
// end-of-scene, so every draw copies its vertices into the per-frame ring.

#include "gxm_backend.h"
#include "gxm_device.h"
#include "gxm_texture.h"
#include "gxm_state.h"
#include "shaders/gxm_shaders.h"

#include <string.h>
#include <stdio.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

// supplied by tr_gxm_bridge.cpp, where tess is visible
extern "C" void GXM_GetTessArrays( const float **xyz, const float **uv0,
								   const float **uv1, const unsigned char **rgba );

#define GXM_MAX_TEXNUM		4096
#define GXM_MAX_PROGRAMS	256

// one interleaved stream: GXM allows 4 total and packing keeps the
// vertex-program count down
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

static gxmTexture_t		gxm_textures[GXM_MAX_TEXNUM];
static unsigned int		gxm_boundTex[2];

static SceGxmShaderPatcherId	gxm_vertIds[3][2];		// [texcoord sets][vertex colour]
static SceGxmVertexProgram		*gxm_vertProgs[3][2];
static const SceGxmProgram		*gxm_vertBlobs[3][2];
static SceGxmShaderPatcherId	gxm_fragIds[3][2][5];	// [textures][env][alpha test]
static const SceGxmProgram		*gxm_fragBlobs[3][2][5];

static gxmProgCache_t	gxm_progCache[GXM_MAX_PROGRAMS];
static int				gxm_progCount;

static float		gxm_proj[16], gxm_modelView[16], gxm_mvp[16];
static bool			gxm_mvpDirty = true;
static unsigned int	gxm_stateBits;
static int			gxm_texUnits = 1;
static int			gxm_vertexColor = 1;
static int			gxm_texEnv = GXM_TEXENV_MODULATE;
static int			gxm_cullFlip;
static float		gxm_constColor[4] = { 1, 1, 1, 1 };
static bool			gxm_backendOk;

// the viewport transform is rebuilt whenever either half of it moves
static int			gxm_viewX, gxm_viewY, gxm_viewW, gxm_viewH;
static float		gxm_depthScale = 0.5f, gxm_depthOffset = 0.5f;
// what the backend actually did this run, reported by r_gxmStats
static int	gxm_statUploads, gxm_statDraws, gxm_statTextured, gxm_statNoTex, gxm_statRingFail;
static int	gxm_statDxtUploads;	// how many took the compressed path

static const SceGxmProgram *VertBlob( int nuv, int vcol )
{
	static const unsigned char *v[3][2] = {
		{ gxs_generic_v_u0_c0, gxs_generic_v_u0_c1 },
		{ gxs_generic_v_u1_c0, gxs_generic_v_u1_c1 },
		{ gxs_generic_v_u2_c0, gxs_generic_v_u2_c1 },
	};
	return (const SceGxmProgram *)v[nuv][vcol];
}

// GL_ADD only differs from GL_MODULATE once a second texture is in play, so the
// env variants exist for two units alone
static const SceGxmProgram *FragBlob( int ntex, int env, int atest )
{
	static const unsigned char *t0[5] = {
		gxs_generic_f_t0_e0_a0, gxs_generic_f_t0_e0_a1, gxs_generic_f_t0_e0_a2,
		gxs_generic_f_t0_e0_a3, gxs_generic_f_t0_e0_a4 };
	static const unsigned char *t1[5] = {
		gxs_generic_f_t1_e0_a0, gxs_generic_f_t1_e0_a1, gxs_generic_f_t1_e0_a2,
		gxs_generic_f_t1_e0_a3, gxs_generic_f_t1_e0_a4 };
	static const unsigned char *t2e0[5] = {
		gxs_generic_f_t2_e0_a0, gxs_generic_f_t2_e0_a1, gxs_generic_f_t2_e0_a2,
		gxs_generic_f_t2_e0_a3, gxs_generic_f_t2_e0_a4 };
	static const unsigned char *t2e1[5] = {
		gxs_generic_f_t2_e1_a0, gxs_generic_f_t2_e1_a1, gxs_generic_f_t2_e1_a2,
		gxs_generic_f_t2_e1_a3, gxs_generic_f_t2_e1_a4 };

	if ( ntex <= 0 ) return (const SceGxmProgram *)t0[atest];
	if ( ntex == 1 ) return (const SceGxmProgram *)t1[atest];
	return (const SceGxmProgram *)( env ? t2e1[atest] : t2e0[atest] );
}

/*
================
BuildVertexProgram

The patcher generates the attribute unpack, so one .gxp serves any stride; only
the attribute set differs per texcoord count.
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
	memset( gxm_progCache, 0, sizeof(gxm_progCache) );
	gxm_progCount = 0;

	for ( int nuv = 0; nuv < 3; nuv++ ) {
		for ( int vcol = 0; vcol < 2; vcol++ ) {
			gxm_vertBlobs[nuv][vcol] = VertBlob( nuv, vcol );
			if ( sceGxmShaderPatcherRegisterProgram( GXM_ShaderPatcher(),
					gxm_vertBlobs[nuv][vcol], &gxm_vertIds[nuv][vcol] ) < 0 ) {
				return 0;
			}
			gxm_vertProgs[nuv][vcol] = BuildVertexProgram( gxm_vertBlobs[nuv][vcol],
				gxm_vertIds[nuv][vcol], nuv );
			if ( !gxm_vertProgs[nuv][vcol] ) {
				return 0;
			}
		}
	}

	for ( int t = 0; t < 3; t++ ) {
		for ( int e = 0; e < 2; e++ ) {
			for ( int a = 0; a < 5; a++ ) {
				gxm_fragBlobs[t][e][a] = FragBlob( t, e, a );
				if ( sceGxmShaderPatcherRegisterProgram( GXM_ShaderPatcher(),
						gxm_fragBlobs[t][e][a], &gxm_fragIds[t][e][a] ) < 0 ) {
					return 0;
				}
			}
		}
	}

	gxm_backendOk = true;
	return 1;
}

void GXM_BackendShutdown( void )
{
	gxm_backendOk = false;
	for ( unsigned int i = 0; i < GXM_MAX_TEXNUM; i++ ) {
		GXM_TextureFree( &gxm_textures[i] );
	}
}

// ---------------------------------------------------------------------------
// textures
// ---------------------------------------------------------------------------

void GXM_TexUpload( unsigned int texnum, const void *rgba, int width, int height )
{
	if ( texnum >= GXM_MAX_TEXNUM || width <= 0 || height <= 0 ) {
		return;
	}
	GXM_TextureFree( &gxm_textures[texnum] );
	if ( GXM_TextureCreateRGBA( &gxm_textures[texnum], rgba, (unsigned)width, (unsigned)height ) ) {
		gxm_statUploads++;
	}
}

void GXM_TexUploadDxt( unsigned int texnum, const void *blob, unsigned int size,
					   unsigned int width, unsigned int height, unsigned int mipCount, int isDxt5 )
{
	if ( texnum >= GXM_MAX_TEXNUM || !blob || !size ) {
		return;
	}
	GXM_TextureFree( &gxm_textures[texnum] );
	if ( GXM_TextureCreateDxt( &gxm_textures[texnum], blob, size, width, height,
			mipCount, isDxt5 != 0 ) ) {
		gxm_statUploads++;
		gxm_statDxtUploads++;
	}
}

void GXM_TexFree( unsigned int texnum )
{
	if ( texnum < GXM_MAX_TEXNUM ) {
		GXM_TextureFree( &gxm_textures[texnum] );
	}
}

void GXM_TexBind( int tmu, unsigned int texnum )
{
	if ( tmu >= 0 && tmu < 2 ) {
		gxm_boundTex[tmu] = texnum;
	}
}

void GXM_TexFilter( unsigned int texnum, int linear, int clampToEdge )
{
	if ( texnum < GXM_MAX_TEXNUM ) {
		GXM_TextureSetFilter( &gxm_textures[texnum], linear != 0, clampToEdge != 0 );
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
void GXM_SetTexEnv( int env )					{ gxm_texEnv = env; }

/*
================
GXM_SetDepthBias

There is no enable to toggle: libgxm-Reference documents "Depth bias is always
enabled", so the off state is a bias of zero and it has to be set back explicitly.
GL's factor/units are floats but GXM takes integer steps in [-16,15], which covers
every value the renderer actually asks for.
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
	gxm_constColor[0] = r; gxm_constColor[1] = g;
	gxm_constColor[2] = b; gxm_constColor[3] = a;
}

void GXM_SetCullFlip( int flip )					{ gxm_cullFlip = flip; }

/*
================
GXM_SetCull

GL decides facing from the signed area in window coordinates, which run bottom-up;
GXM uses screen coordinates, which run top-down. The mirror negates the area, so a
triangle GL calls front-facing (counter-clockwise, area > 0) is the same triangle
GXM calls front-facing (clockwise in its space) -- culling GL_FRONT is CULL_CW,
which libgxm defines as "cull triangles with clockwise window coordinates".

r_gxmCullFlip inverts that, because the sign is the one part of this the docs do
not pin down well enough to bet a build cycle on.
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

The engine's rectangle is GL's: origin bottom-left. GXM's screen space runs top-down,
so the rectangle is flipped here and the viewport's yScale carries the sign.

Setting the transform explicitly rather than inheriting what sceGxmBeginScene left is
what pins the conventions down: yScale < 0 puts NDC +1 at the top of the screen, and
zOffset/zScale 0.5 accept GL's [-1,1] clip Z, so the engine's own projection matrices
need no rewriting. sceGxmBeginScene resets both, so this has to run every frame.
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

	sceGxmSetRegionClip( GXM_Context(), SCE_GXM_REGION_CLIP_OUTSIDE,
		(unsigned)x, (unsigned)top,
		(unsigned)( x + w - 1 ), (unsigned)( top + h - 1 ) );

	gxm_viewX = x; gxm_viewY = top; gxm_viewW = w; gxm_viewH = h;
}

/*
================
GXM_SetDepthRange

qglDepthRange's near/far, folded into the viewport's Z transform. GL maps clip Z
[-1,1] onto [near,far], so scale and offset are half the span and its midpoint.
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

static SceGxmFragmentProgram *ResolveFragment( int ntex, int env, int vcol,
											   const gxmProgramKey_t *key )
{
	const unsigned int hash = ( GXM_ProgramKeyHash( key ) << 4 )
		| ( (unsigned)ntex << 2 ) | ( (unsigned)env << 1 ) | (unsigned)vcol;

	for ( int i = 0; i < gxm_progCount; i++ ) {
		if ( gxm_progCache[i].key == hash ) {
			return gxm_progCache[i].prog;
		}
	}
	if ( gxm_progCount >= GXM_MAX_PROGRAMS ) {
		return NULL;
	}

	SceGxmFragmentProgram *prog = NULL;
	if ( sceGxmShaderPatcherCreateFragmentProgram( GXM_ShaderPatcher(),
			gxm_fragIds[ntex][env][key->alphaTest],
			SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
			key->blended ? &key->blend : NULL,
			gxm_vertBlobs[ntex][vcol], &prog ) < 0 ) {	// links the fragment texcoords to the program that will be bound
		return NULL;
	}

	gxm_progCache[gxm_progCount].key  = hash;
	gxm_progCache[gxm_progCount].prog = prog;
	gxm_progCount++;
	return prog;
}

void GXM_DrawTess( int numIndexes, const unsigned short *indexes, int numVertexes )
{
	if ( !gxm_backendOk || numIndexes <= 0 || numVertexes <= 0 ) {
		return;
	}

	const float *xyz = NULL, *uv0 = NULL, *uv1 = NULL;
	const unsigned char *rgba = NULL;
	GXM_GetTessArrays( &xyz, &uv0, &uv1, &rgba );
	if ( !xyz ) {
		return;
	}

	gxmProgramKey_t key;
	gxmDepthState_t depth;
	if ( !GXM_TranslateState( gxm_stateBits, &key, &depth ) ) {
		return;	// wireframe and other modes with no GXM expression
	}

	int ntex = gxm_texUnits;
	if ( ntex > 2 ) ntex = 2;
	if ( ntex < 0 ) ntex = 0;
	gxm_statDraws++;
	if ( ntex >= 1 && !gxm_textures[ gxm_boundTex[0] & (GXM_MAX_TEXNUM-1) ].valid ) {
		ntex = 0;	// an unbacked texture would sample garbage
		gxm_statNoTex++;
	} else if ( ntex >= 1 ) {
		gxm_statTextured++;
	}

	const int nuv  = ntex;
	const int vcol = ( rgba && gxm_vertexColor ) ? 1 : 0;
	const int env  = ( ntex >= 2 && gxm_texEnv == GXM_TEXENV_ADD ) ? 1 : 0;

	SceGxmFragmentProgram *frag = ResolveFragment( ntex, env, vcol, &key );
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
		v[i].uv1[0] = uv1 ? uv1[i * 2 + 0] : 0.0f;
		v[i].uv1[1] = uv1 ? uv1[i * 2 + 1] : 0.0f;
		if ( vcol ) {
			memcpy( v[i].rgba, &rgba[i * 4], 4 );
		}	// otherwise the program has no aColor and the bytes are never read
	}
	memcpy( idx, indexes, numIndexes * sizeof(unsigned short) );

	if ( gxm_mvpDirty ) {
		MulMat( gxm_mvp, gxm_proj, gxm_modelView );
		gxm_mvpDirty = false;
	}

	sceGxmSetVertexProgram( GXM_Context(), gxm_vertProgs[nuv][vcol] );
	sceGxmSetFragmentProgram( GXM_Context(), frag );
	GXM_ApplyDepthState( &depth );

	void *uniforms = NULL;
	sceGxmReserveVertexDefaultUniformBuffer( GXM_Context(), &uniforms );
	if ( uniforms ) {
		const SceGxmProgramParameter *pm = sceGxmProgramFindParameterByName( gxm_vertBlobs[nuv][vcol], "uMVP" );
		const SceGxmProgramParameter *pc = sceGxmProgramFindParameterByName( gxm_vertBlobs[nuv][vcol], "uColor" );
		if ( pm ) sceGxmSetUniformDataF( uniforms, pm, 0, 16, gxm_mvp );
		if ( pc ) sceGxmSetUniformDataF( uniforms, pc, 0, 4, gxm_constColor );
	}

	for ( int t = 0; t < ntex; t++ ) {
		const unsigned int tn = gxm_boundTex[t] & (GXM_MAX_TEXNUM - 1);
		if ( gxm_textures[tn].valid ) {
			sceGxmSetFragmentTexture( GXM_Context(), t, &gxm_textures[tn].tex );
		}
	}

	sceGxmSetVertexStream( GXM_Context(), 0, v );
	sceGxmDraw( GXM_Context(), SCE_GXM_PRIMITIVE_TRIANGLES,
		SCE_GXM_INDEX_FORMAT_U16, idx, numIndexes );
}

/*
================
GXM_DrawStaticBuffer

The world VBO's vertices already sit in GPU memory in exactly the interleaved layout
the generic vertex program reads, so only the indices — rebuilt every frame as
surfaces batch — go through the ring. This is the whole point of the path: static
geometry is never copied per frame.
================
*/
void GXM_DrawStaticBuffer( const void *vertexBuffer, const unsigned short *indexes,
						   int numIndexes )
{
	if ( !gxm_backendOk || !vertexBuffer || numIndexes <= 0 ) {
		return;
	}

	gxmProgramKey_t key;
	gxmDepthState_t depth;
	if ( !GXM_TranslateState( gxm_stateBits, &key, &depth ) ) {
		return;
	}

	int ntex = gxm_texUnits;
	if ( ntex > 2 ) ntex = 2;
	if ( ntex < 0 ) ntex = 0;
	for ( int t = 0; t < ntex; t++ ) {
		if ( !gxm_textures[ gxm_boundTex[t] & (GXM_MAX_TEXNUM - 1) ].valid ) {
			ntex = t;	// an unbacked unit would sample garbage; drop it and any above
			break;
		}
	}

	// the batch gate only admits constant-colour stages, so the colour comes from
	// the uniform and the buffer's baked vertex colours go unread
	const int env = ( ntex >= 2 && gxm_texEnv == GXM_TEXENV_ADD ) ? 1 : 0;
	SceGxmFragmentProgram *frag = ResolveFragment( ntex, env, 0, &key );
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
	}

	sceGxmSetVertexProgram( GXM_Context(), gxm_vertProgs[ntex][0] );
	sceGxmSetFragmentProgram( GXM_Context(), frag );
	GXM_ApplyDepthState( &depth );

	void *uniforms = NULL;
	sceGxmReserveVertexDefaultUniformBuffer( GXM_Context(), &uniforms );
	if ( uniforms ) {
		const SceGxmProgramParameter *pm = sceGxmProgramFindParameterByName( gxm_vertBlobs[ntex][0], "uMVP" );
		const SceGxmProgramParameter *pc = sceGxmProgramFindParameterByName( gxm_vertBlobs[ntex][0], "uColor" );
		if ( pm ) sceGxmSetUniformDataF( uniforms, pm, 0, 16, gxm_mvp );
		if ( pc ) sceGxmSetUniformDataF( uniforms, pc, 0, 4, gxm_constColor );
	}

	for ( int t = 0; t < ntex; t++ ) {
		const unsigned int tn = gxm_boundTex[t] & (GXM_MAX_TEXNUM - 1);
		sceGxmSetFragmentTexture( GXM_Context(), t, &gxm_textures[tn].tex );
	}

	gxm_statDraws++;
	sceGxmSetVertexStream( GXM_Context(), 0, vertexBuffer );
	sceGxmDraw( GXM_Context(), SCE_GXM_PRIMITIVE_TRIANGLES,
		SCE_GXM_INDEX_FORMAT_U16, idx, numIndexes );
}

// a picture of what the backend actually did, for r_gxmStats
void GXM_ReportStats( char *out, int outSize )
{
	extern int gxm_texAllocFail, gxm_texInitFail;
	extern unsigned int gxm_texBytes;
	const int n = snprintf( out, outSize,
		"GXM: uploads=%d dxt=%d allocfail=%d initfail=%d texmem=%uMB | draws=%d textured=%d notex=%d ringfail=%d ring=%uKB/%uKB\n",
		gxm_statUploads, gxm_statDxtUploads, gxm_texAllocFail, gxm_texInitFail, gxm_texBytes / ( 1024 * 1024 ),
		gxm_statDraws, gxm_statTextured, gxm_statNoTex,
		gxm_statRingFail, GXM_RingUsedLastFrame() / 1024, GXM_RingBytesPerFrame() / 1024 );

	// also to its own file: the engine log is not flushed on an abrupt exit
	sceIoMkdir( "ux0:data/JK2VITA", 0777 );
	SceUID f = sceIoOpen( "ux0:data/JK2VITA/gxm_stats.log",
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777 );
	if ( f >= 0 ) {
		sceIoWrite( f, out, n );
		sceIoClose( f );
	}
}
