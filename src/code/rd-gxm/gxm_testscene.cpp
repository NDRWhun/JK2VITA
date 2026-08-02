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

// gxm_testscene.cpp -- probe-only scene exercising the 3D pipeline.

#include "gxm_texture.h"
#include "shaders/gxm_shaders.h"

#include <math.h>
#include <string.h>

typedef struct {
	float x, y, z;
	float u, v;
	unsigned char r, g, b, a;
} testVert_t;

static SceGxmShaderPatcherId	ts_vertId, ts_fragId;
static SceGxmVertexProgram		*ts_vertProgram;
static SceGxmFragmentProgram	*ts_fragOpaque;
static SceGxmFragmentProgram	*ts_fragBlend;
static const SceGxmProgramParameter *ts_mvpParam, *ts_colorParam;
static gxmTexture_t				ts_texture;
static SceUID					ts_indexUid;
static uint16_t					*ts_indices;

static void BuildCheckerboard( unsigned int *px, unsigned int w, unsigned int h )
{
	for ( unsigned int y = 0; y < h; y++ ) {
		for ( unsigned int x = 0; x < w; x++ ) {
			const bool on = ( ( x >> 4 ) ^ ( y >> 4 ) ) & 1;
			px[y * w + x] = on ? 0xFFFFFFFFu : 0xFF3050C0u;	// ABGR
		}
	}
}

bool GXM_TestSceneInit( void )
{
	const SceGxmProgram *vert = (const SceGxmProgram *)gxs_generic_v_u1_c1_f0;
	const SceGxmProgram *frag = (const SceGxmProgram *)gxs_generic_f_t1_e0_a0_f0;

	if ( sceGxmShaderPatcherRegisterProgram( GXM_ShaderPatcher(), vert, &ts_vertId ) < 0
		|| sceGxmShaderPatcherRegisterProgram( GXM_ShaderPatcher(), frag, &ts_fragId ) < 0 ) {
		return false;
	}

	const SceGxmProgramParameter *pPos = sceGxmProgramFindParameterByName( vert, "aPosition" );
	const SceGxmProgramParameter *pUV  = sceGxmProgramFindParameterByName( vert, "aTexCoord0" );
	const SceGxmProgramParameter *pCol = sceGxmProgramFindParameterByName( vert, "aColor" );
	if ( !pPos || !pUV || !pCol ) {
		return false;
	}

	// one interleaved stream; GXM allows only 4 streams total so packing is the norm
	SceGxmVertexAttribute attrs[3];
	memset( attrs, 0, sizeof(attrs) );
	attrs[0].streamIndex = 0; attrs[0].offset = 0;
	attrs[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; attrs[0].componentCount = 3;
	attrs[0].regIndex = sceGxmProgramParameterGetResourceIndex( pPos );
	attrs[1].streamIndex = 0; attrs[1].offset = 12;
	attrs[1].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; attrs[1].componentCount = 2;
	attrs[1].regIndex = sceGxmProgramParameterGetResourceIndex( pUV );
	attrs[2].streamIndex = 0; attrs[2].offset = 20;
	attrs[2].format = SCE_GXM_ATTRIBUTE_FORMAT_U8N; attrs[2].componentCount = 4;
	attrs[2].regIndex = sceGxmProgramParameterGetResourceIndex( pCol );

	SceGxmVertexStream stream;
	memset( &stream, 0, sizeof(stream) );
	stream.stride      = sizeof(testVert_t);
	stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;

	if ( sceGxmShaderPatcherCreateVertexProgram( GXM_ShaderPatcher(), ts_vertId,
			attrs, 3, &stream, 1, &ts_vertProgram ) < 0 ) {
		return false;
	}

	// opaque instance
	if ( sceGxmShaderPatcherCreateFragmentProgram( GXM_ShaderPatcher(), ts_fragId,
			SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
			NULL, vert, &ts_fragOpaque ) < 0 ) {
		return false;
	}

	// blended instance: same program, blend compiled into a second instance
	SceGxmBlendInfo blend;
	memset( &blend, 0, sizeof(blend) );
	blend.colorMask = SCE_GXM_COLOR_MASK_ALL;
	blend.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
	blend.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
	blend.colorSrc  = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
	blend.colorDst  = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alphaSrc  = SCE_GXM_BLEND_FACTOR_ONE;
	blend.alphaDst  = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

	if ( sceGxmShaderPatcherCreateFragmentProgram( GXM_ShaderPatcher(), ts_fragId,
			SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
			&blend, vert, &ts_fragBlend ) < 0 ) {
		return false;
	}

	ts_mvpParam   = sceGxmProgramFindParameterByName( vert, "uMVP" );
	ts_colorParam = sceGxmProgramFindParameterByName( vert, "uColor" );

	unsigned int px[128 * 128];
	BuildCheckerboard( px, 128, 128 );
	if ( !GXM_TextureCreateRGBA( &ts_texture, px, 128, 128 ) ) {
		return false;
	}

	// quads are two triangles; GXM has no quad primitive
	ts_indices = (uint16_t *)GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		6 * sizeof(uint16_t), 4, SCE_GXM_MEMORY_ATTRIB_READ, &ts_indexUid );
	if ( !ts_indices ) {
		return false;
	}
	ts_indices[0] = 0; ts_indices[1] = 1; ts_indices[2] = 2;
	ts_indices[3] = 0; ts_indices[4] = 2; ts_indices[5] = 3;

	return true;
}

static void MakePerspective( float *m, float fovDeg, float aspect, float zn, float zf )
{
	const float f = 1.0f / tanf( fovDeg * 0.5f * 3.14159265f / 180.0f );
	memset( m, 0, 16 * sizeof(float) );
	m[0]  = f / aspect;
	m[5]  = f;
	m[10] = ( zf + zn ) / ( zn - zf );
	m[11] = -1.0f;
	m[14] = ( 2.0f * zf * zn ) / ( zn - zf );
}

// row-vector convention to match mul(v, uMVP) in the shader
static void MulMat( float *out, const float *a, const float *b )
{
	for ( int r = 0; r < 4; r++ ) {
		for ( int c = 0; c < 4; c++ ) {
			float v = 0.0f;
			for ( int k = 0; k < 4; k++ ) {
				v += a[r * 4 + k] * b[k * 4 + c];
			}
			out[r * 4 + c] = v;
		}
	}
}

static const float kCubePos[8][3] = {
	{-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
	{-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1},
};
static const unsigned char kCubeFace[6][4] = {
	{0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0},
};
static const unsigned int kFaceTint[6] = {
	0xFFFFFFFF, 0xFFC0FFFF, 0xFFFFC0FF, 0xFFFFFFC0, 0xFFC0C0FF, 0xFFFFC0C0,
};

void GXM_DrawTestQuad( float frame )
{
	GXM_RingBeginFrame();

	float proj[16], mvp[16];
	MakePerspective( proj, 60.0f, (float)GXM_DISPLAY_WIDTH / (float)GXM_DISPLAY_HEIGHT, 1.0f, 100.0f );

	const float a = frame * 0.012f;
	const float ca = cosf( a ), sa = sinf( a );
	const float cb = cosf( a * 0.7f ), sb = sinf( a * 0.7f );

	// yaw * pitch * translate, row-vector order
	const float model[16] = {
		 ca,        0.0f,      -sa,       0.0f,
		 sa * sb,   cb,         ca * sb,  0.0f,
		 sa * cb,  -sb,         ca * cb,  0.0f,
		 0.0f,      0.0f,      -6.0f,     1.0f,
	};
	MulMat( mvp, model, proj );

	sceGxmSetVertexProgram( GXM_Context(), ts_vertProgram );
	sceGxmSetFragmentProgram( GXM_Context(), ts_fragOpaque );

	void *uniforms = NULL;
	sceGxmReserveVertexDefaultUniformBuffer( GXM_Context(), &uniforms );
	if ( uniforms ) {
		static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		if ( ts_mvpParam )   sceGxmSetUniformDataF( uniforms, ts_mvpParam, 0, 16, mvp );
		if ( ts_colorParam ) sceGxmSetUniformDataF( uniforms, ts_colorParam, 0, 4, white );
	}

	// the real 3D contract: depth test and write on, backfaces dropped
	sceGxmSetFrontDepthFunc( GXM_Context(), SCE_GXM_DEPTH_FUNC_LESS_EQUAL );
	sceGxmSetFrontDepthWriteEnable( GXM_Context(), SCE_GXM_DEPTH_WRITE_ENABLED );
	sceGxmSetCullMode( GXM_Context(), SCE_GXM_CULL_CCW );

	GXM_TextureBind( 0, &ts_texture );

	testVert_t *v = (testVert_t *)GXM_RingAlloc( 6 * 4 * sizeof(testVert_t), 4 );
	if ( !v ) {
		return;
	}

	for ( int f = 0; f < 6; f++ ) {
		static const float uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
		const unsigned int tint = kFaceTint[f];
		for ( int i = 0; i < 4; i++ ) {
			testVert_t *o = &v[f * 4 + i];
			const float *p = kCubePos[ kCubeFace[f][i] ];
			o->x = p[0]; o->y = p[1]; o->z = p[2];
			o->u = uv[i][0]; o->v = uv[i][1];
			o->r = (unsigned char)( tint        & 0xFF );
			o->g = (unsigned char)( (tint >> 8) & 0xFF );
			o->b = (unsigned char)( (tint >> 16)& 0xFF );
			o->a = 255;
		}
		sceGxmSetVertexStream( GXM_Context(), 0, &v[f * 4] );
		sceGxmDraw( GXM_Context(), SCE_GXM_PRIMITIVE_TRIANGLES,
			SCE_GXM_INDEX_FORMAT_U16, ts_indices, 6 );
	}
}

void GXM_TestSceneShutdown( void )
{
	GXM_TextureFree( &ts_texture );
	if ( ts_indices ) {
		GXM_Free( ts_indexUid );
		ts_indices = NULL;
	}
	sceGxmShaderPatcherReleaseVertexProgram( GXM_ShaderPatcher(), ts_vertProgram );
	sceGxmShaderPatcherReleaseFragmentProgram( GXM_ShaderPatcher(), ts_fragOpaque );
	sceGxmShaderPatcherReleaseFragmentProgram( GXM_ShaderPatcher(), ts_fragBlend );
	sceGxmShaderPatcherUnregisterProgram( GXM_ShaderPatcher(), ts_vertId );
	sceGxmShaderPatcherUnregisterProgram( GXM_ShaderPatcher(), ts_fragId );
}
