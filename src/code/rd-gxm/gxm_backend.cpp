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

static SceGxmShaderPatcherId	gxm_vertIds[3];		// by texcoord set count
static SceGxmVertexProgram		*gxm_vertProgs[3];
static const SceGxmProgram		*gxm_vertBlobs[3];
static SceGxmShaderPatcherId	gxm_fragIds[3][5];	// [textures][alpha test]
static const SceGxmProgram		*gxm_fragBlobs[3][5];

static gxmProgCache_t	gxm_progCache[GXM_MAX_PROGRAMS];
static int				gxm_progCount;

static float		gxm_proj[16], gxm_modelView[16], gxm_mvp[16];
static bool			gxm_mvpDirty = true;
static unsigned int	gxm_stateBits;
static int			gxm_texUnits = 1;
static int			gxm_vertexColor = 1;
static float		gxm_constColor[4] = { 1, 1, 1, 1 };
static bool			gxm_backendOk;
static int			gxm_statUploads, gxm_statDraws, gxm_statTextured, gxm_statNoTex, gxm_statRingFail;
static unsigned int	gxm_statFirstTexnum, gxm_statLastBound;
int gxm_statCreateImage, gxm_statUpload32, gxm_statRgbaHit, gxm_statDxtHit;

static const SceGxmProgram *VertBlob( int nuv )
{
	switch ( nuv ) {
	case 0:  return (const SceGxmProgram *)gxs_generic_v_u0_c1;
	case 1:  return (const SceGxmProgram *)gxs_generic_v_u1_c1;
	default: return (const SceGxmProgram *)gxs_generic_v_u2_c1;
	}
}

static const SceGxmProgram *FragBlob( int ntex, int atest )
{
	static const unsigned char *t0[5] = {
		gxs_generic_f_t0_e0_a0, gxs_generic_f_t0_e0_a1, gxs_generic_f_t0_e0_a2,
		gxs_generic_f_t0_e0_a3, gxs_generic_f_t0_e0_a4 };
	static const unsigned char *t1[5] = {
		gxs_generic_f_t1_e0_a0, gxs_generic_f_t1_e0_a1, gxs_generic_f_t1_e0_a2,
		gxs_generic_f_t1_e0_a3, gxs_generic_f_t1_e0_a4 };
	static const unsigned char *t2[5] = {
		gxs_generic_f_t2_e0_a0, gxs_generic_f_t2_e0_a1, gxs_generic_f_t2_e0_a2,
		gxs_generic_f_t2_e0_a3, gxs_generic_f_t2_e0_a4 };
	const unsigned char **set = ( ntex <= 0 ) ? t0 : ( ntex == 1 ? t1 : t2 );
	return (const SceGxmProgram *)set[atest];
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
		gxm_vertBlobs[nuv] = VertBlob( nuv );
		if ( sceGxmShaderPatcherRegisterProgram( GXM_ShaderPatcher(), gxm_vertBlobs[nuv], &gxm_vertIds[nuv] ) < 0 ) {
			return 0;
		}
		gxm_vertProgs[nuv] = BuildVertexProgram( gxm_vertBlobs[nuv], gxm_vertIds[nuv], nuv );
		if ( !gxm_vertProgs[nuv] ) {
			return 0;
		}
	}

	for ( int t = 0; t < 3; t++ ) {
		for ( int a = 0; a < 5; a++ ) {
			gxm_fragBlobs[t][a] = FragBlob( t, a );
			if ( sceGxmShaderPatcherRegisterProgram( GXM_ShaderPatcher(), gxm_fragBlobs[t][a], &gxm_fragIds[t][a] ) < 0 ) {
				return 0;
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
		if ( !gxm_statUploads ) gxm_statFirstTexnum = texnum;
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
		if ( !gxm_statUploads ) gxm_statFirstTexnum = texnum;
		gxm_statUploads++;
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

void GXM_SetConstantColor( float r, float g, float b, float a )
{
	gxm_constColor[0] = r; gxm_constColor[1] = g;
	gxm_constColor[2] = b; gxm_constColor[3] = a;
}

void GXM_SetCull( int glCullMode, int enabled )
{
	if ( !enabled ) {
		sceGxmSetCullMode( GXM_Context(), SCE_GXM_CULL_NONE );
		return;
	}
	// GL_FRONT is 0x0404
	sceGxmSetCullMode( GXM_Context(),
		( glCullMode == 0x0404 ) ? SCE_GXM_CULL_CW : SCE_GXM_CULL_CCW );
}

void GXM_SetViewport( int x, int y, int w, int h )
{
	if ( w <= 0 || h <= 0 ) {
		return;
	}
	sceGxmSetRegionClip( GXM_Context(), SCE_GXM_REGION_CLIP_OUTSIDE,
		(unsigned)x, (unsigned)y, (unsigned)( x + w - 1 ), (unsigned)( y + h - 1 ) );
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

static SceGxmFragmentProgram *ResolveFragment( int ntex, const gxmProgramKey_t *key )
{
	const unsigned int hash = ( GXM_ProgramKeyHash( key ) << 2 ) | (unsigned)ntex;

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
			gxm_fragIds[ntex][key->alphaTest],
			SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
			key->blended ? &key->blend : NULL,
			gxm_vertBlobs[0], &prog ) < 0 ) {
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
	gxm_statLastBound = gxm_boundTex[0];
	if ( ntex >= 1 && !gxm_textures[ gxm_boundTex[0] & (GXM_MAX_TEXNUM-1) ].valid ) {
		ntex = 0;	// an unbacked texture would sample garbage
		gxm_statNoTex++;
	} else if ( ntex >= 1 ) {
		gxm_statTextured++;
	}

	SceGxmFragmentProgram *frag = ResolveFragment( ntex, &key );
	if ( !frag ) {
		return;
	}

	const int nuv = ntex;
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
		if ( rgba && gxm_vertexColor ) {
			memcpy( v[i].rgba, &rgba[i * 4], 4 );
		} else {
			v[i].rgba[0] = v[i].rgba[1] = v[i].rgba[2] = v[i].rgba[3] = 255;
		}
	}
	memcpy( idx, indexes, numIndexes * sizeof(unsigned short) );

	if ( gxm_mvpDirty ) {
		MulMat( gxm_mvp, gxm_proj, gxm_modelView );
		gxm_mvpDirty = false;
	}

	sceGxmSetVertexProgram( GXM_Context(), gxm_vertProgs[nuv] );
	sceGxmSetFragmentProgram( GXM_Context(), frag );
	GXM_ApplyDepthState( &depth );

	void *uniforms = NULL;
	sceGxmReserveVertexDefaultUniformBuffer( GXM_Context(), &uniforms );
	if ( uniforms ) {
		const SceGxmProgramParameter *pm = sceGxmProgramFindParameterByName( gxm_vertBlobs[nuv], "uMVP" );
		const SceGxmProgramParameter *pc = sceGxmProgramFindParameterByName( gxm_vertBlobs[nuv], "uColor" );
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

// one-shot picture of what the backend is actually seeing; written directly
// because the engine's log buffer is not flushed on an abrupt exit
extern "C" void GXM_ReportStats( int *uploads, int *draws, int *textured, int *notex,
								   int *ringfail, unsigned int *firstTex, unsigned int *lastBound )
{
	*uploads = gxm_statUploads; *draws = gxm_statDraws; *textured = gxm_statTextured;
	*notex = gxm_statNoTex; *ringfail = gxm_statRingFail;
	*firstTex = gxm_statFirstTexnum; *lastBound = gxm_statLastBound;

	char line[256];
	const int n = snprintf( line, sizeof(line),
		"createImage=%d upload32=%d rgbaHit=%d dxtHit=%d | uploads=%d draws=%d notex=%d lastBound=%u\n",
		gxm_statCreateImage, gxm_statUpload32, gxm_statRgbaHit, gxm_statDxtHit,
		gxm_statUploads, gxm_statDraws, gxm_statNoTex, gxm_statLastBound );

	sceIoMkdir( "ux0:data/JK2VITA", 0777 );
	SceUID f = sceIoOpen( "ux0:data/JK2VITA/gxm_stats.log",
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777 );
	if ( f >= 0 ) {
		sceIoWrite( f, line, n );
		sceIoClose( f );
	}
}
