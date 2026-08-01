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

// gxm_testscene.cpp -- probe-only scene exercising texture, ring and blending.
//
// Draws two textured quads, the second alpha-blended over the first, so a
// correct frame proves the ring, the generic shaders and a blended fragment
// program instance all work together.

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
	const SceGxmProgram *vert = (const SceGxmProgram *)gxs_generic_v_u1_c1;
	const SceGxmProgram *frag = (const SceGxmProgram *)gxs_generic_f_t1_e0_a0;

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

static void EmitQuad( float cx, float cy, float half, unsigned char alpha, bool blended )
{
	testVert_t *v = (testVert_t *)GXM_RingAlloc( 4 * sizeof(testVert_t), 4 );
	if ( !v ) {
		return;	// ring exhausted this frame
	}

	const float x0 = cx - half, x1 = cx + half;
	const float y0 = cy - half, y1 = cy + half;

	v[0].x = x0; v[0].y = y0; v[0].z = 0.0f; v[0].u = 0.0f; v[0].v = 0.0f;
	v[1].x = x1; v[1].y = y0; v[1].z = 0.0f; v[1].u = 1.0f; v[1].v = 0.0f;
	v[2].x = x1; v[2].y = y1; v[2].z = 0.0f; v[2].u = 1.0f; v[2].v = 1.0f;
	v[3].x = x0; v[3].y = y1; v[3].z = 0.0f; v[3].u = 0.0f; v[3].v = 1.0f;
	for ( int i = 0; i < 4; i++ ) {
		v[i].r = 255; v[i].g = 255; v[i].b = 255; v[i].a = alpha;
	}

	sceGxmSetFragmentProgram( GXM_Context(), blended ? ts_fragBlend : ts_fragOpaque );
	GXM_TextureBind( 0, &ts_texture );
	sceGxmSetVertexStream( GXM_Context(), 0, v );
	sceGxmDraw( GXM_Context(), SCE_GXM_PRIMITIVE_TRIANGLES,
		SCE_GXM_INDEX_FORMAT_U16, ts_indices, 6 );
}

void GXM_DrawTestQuad( float frame )
{
	static const float identity[16] = {
		1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1
	};
	static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	GXM_RingBeginFrame();

	sceGxmSetVertexProgram( GXM_Context(), ts_vertProgram );

	void *uniforms = NULL;
	sceGxmReserveVertexDefaultUniformBuffer( GXM_Context(), &uniforms );
	if ( uniforms ) {
		if ( ts_mvpParam )   sceGxmSetUniformDataF( uniforms, ts_mvpParam, 0, 16, identity );
		if ( ts_colorParam ) sceGxmSetUniformDataF( uniforms, ts_colorParam, 0, 4, white );
	}

	// depth off: this is a 2D overlay test, and the clear already primed depth
	sceGxmSetFrontDepthFunc( GXM_Context(), SCE_GXM_DEPTH_FUNC_ALWAYS );
	sceGxmSetFrontDepthWriteEnable( GXM_Context(), SCE_GXM_DEPTH_WRITE_DISABLED );

	EmitQuad( -0.25f, 0.0f, 0.45f, 255, false );

	const float wobble = sinf( frame * 0.03f ) * 0.25f;
	EmitQuad( 0.30f + wobble, 0.0f, 0.35f, 160, true );
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
