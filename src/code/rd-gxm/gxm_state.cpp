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

// gxm_state.cpp -- GLS_ bitmask -> GXM

#include "gxm_state.h"
#include "gxm_device.h"

#include <string.h>

// mirrored from tr_local.h; the backend header drags in the whole engine, and
// these values are part of the renderer's on-the-wire state, not private detail
#define GLS_SRCBLEND_ZERO						0x00000001
#define GLS_SRCBLEND_ONE						0x00000002
#define GLS_SRCBLEND_DST_COLOR					0x00000003
#define GLS_SRCBLEND_ONE_MINUS_DST_COLOR		0x00000004
#define GLS_SRCBLEND_SRC_ALPHA					0x00000005
#define GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA		0x00000006
#define GLS_SRCBLEND_DST_ALPHA					0x00000007
#define GLS_SRCBLEND_ONE_MINUS_DST_ALPHA		0x00000008
#define GLS_SRCBLEND_ALPHA_SATURATE				0x00000009
#define GLS_SRCBLEND_BITS						0x0000000f

#define GLS_DSTBLEND_ZERO						0x00000010
#define GLS_DSTBLEND_ONE						0x00000020
#define GLS_DSTBLEND_SRC_COLOR					0x00000030
#define GLS_DSTBLEND_ONE_MINUS_SRC_COLOR		0x00000040
#define GLS_DSTBLEND_SRC_ALPHA					0x00000050
#define GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA		0x00000060
#define GLS_DSTBLEND_DST_ALPHA					0x00000070
#define GLS_DSTBLEND_ONE_MINUS_DST_ALPHA		0x00000080
#define GLS_DSTBLEND_BITS						0x000000f0

#define GLS_DEPTHMASK_TRUE						0x00000100
#define GLS_POLYMODE_LINE						0x00001000
#define GLS_DEPTHTEST_DISABLE					0x00010000
#define GLS_DEPTHFUNC_EQUAL						0x00020000

#define GLS_ATEST_GT_0							0x10000000
#define GLS_ATEST_LT_80							0x20000000
#define GLS_ATEST_GE_80							0x40000000
#define GLS_ATEST_GE_C0							0x80000000
#define GLS_ATEST_BITS							0xF0000000

static SceGxmBlendFactor SrcFactor( unsigned int bits )
{
	switch ( bits & GLS_SRCBLEND_BITS ) {
	case GLS_SRCBLEND_ZERO:						return SCE_GXM_BLEND_FACTOR_ZERO;
	case GLS_SRCBLEND_ONE:						return SCE_GXM_BLEND_FACTOR_ONE;
	case GLS_SRCBLEND_DST_COLOR:				return SCE_GXM_BLEND_FACTOR_DST_COLOR;
	case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:		return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case GLS_SRCBLEND_SRC_ALPHA:				return SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:		return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case GLS_SRCBLEND_DST_ALPHA:				return SCE_GXM_BLEND_FACTOR_DST_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:		return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	case GLS_SRCBLEND_ALPHA_SATURATE:			return SCE_GXM_BLEND_FACTOR_SRC_ALPHA_SATURATE;
	default:									return SCE_GXM_BLEND_FACTOR_ONE;
	}
}

static SceGxmBlendFactor DstFactor( unsigned int bits )
{
	switch ( bits & GLS_DSTBLEND_BITS ) {
	case GLS_DSTBLEND_ZERO:						return SCE_GXM_BLEND_FACTOR_ZERO;
	case GLS_DSTBLEND_ONE:						return SCE_GXM_BLEND_FACTOR_ONE;
	case GLS_DSTBLEND_SRC_COLOR:				return SCE_GXM_BLEND_FACTOR_SRC_COLOR;
	case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:		return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case GLS_DSTBLEND_SRC_ALPHA:				return SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:		return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case GLS_DSTBLEND_DST_ALPHA:				return SCE_GXM_BLEND_FACTOR_DST_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:		return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	default:									return SCE_GXM_BLEND_FACTOR_ZERO;
	}
}

static gxmAlphaTest_t AlphaTest( unsigned int bits )
{
	switch ( bits & GLS_ATEST_BITS ) {
	case GLS_ATEST_GT_0:	return GXM_ATEST_GT_0;
	case GLS_ATEST_LT_80:	return GXM_ATEST_LT_80;
	case GLS_ATEST_GE_80:	return GXM_ATEST_GE_80;
	case GLS_ATEST_GE_C0:	return GXM_ATEST_GE_C0;
	default:				return GXM_ATEST_NONE;
	}
}

void GXM_TranslateState( unsigned int stateBits, gxmProgramKey_t *key, gxmDepthState_t *depth )
{
	memset( key, 0, sizeof(*key) );

	const bool hasBlend = ( stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) != 0;
	key->blended = hasBlend;
	if ( hasBlend ) {
		key->blend.colorMask = SCE_GXM_COLOR_MASK_ALL;
		key->blend.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
		key->blend.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
		key->blend.colorSrc  = SrcFactor( stateBits );
		key->blend.colorDst  = DstFactor( stateBits );
		key->blend.alphaSrc  = key->blend.colorSrc;
		key->blend.alphaDst  = key->blend.colorDst;
	}

	key->alphaTest = AlphaTest( stateBits );

	depth->depthWrite = ( stateBits & GLS_DEPTHMASK_TRUE ) != 0;
	if ( stateBits & GLS_DEPTHTEST_DISABLE ) {
		// GL suppresses depth writes when the test is off, whatever the mask says
		depth->depthFunc  = SCE_GXM_DEPTH_FUNC_ALWAYS;
		depth->depthWrite = false;
	} else if ( stateBits & GLS_DEPTHFUNC_EQUAL ) {
		depth->depthFunc = SCE_GXM_DEPTH_FUNC_EQUAL;
	} else {
		depth->depthFunc = SCE_GXM_DEPTH_FUNC_LESS_EQUAL;
	}

	// TRIANGLE_LINE shades triangle edges only, which is what glPolygonMode(GL_LINE) does
	depth->wireframe = ( stateBits & GLS_POLYMODE_LINE ) != 0;
}

void GXM_ApplyDepthState( const gxmDepthState_t *depth )
{
	sceGxmSetFrontDepthFunc( GXM_Context(), depth->depthFunc );
	sceGxmSetFrontDepthWriteEnable( GXM_Context(),
		depth->depthWrite ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED );
	sceGxmSetBackDepthFunc( GXM_Context(), depth->depthFunc );
	sceGxmSetBackDepthWriteEnable( GXM_Context(),
		depth->depthWrite ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED );

	// polygon mode persists across draws and scenes, so it is set both ways
	sceGxmSetFrontPolygonMode( GXM_Context(), depth->wireframe
		? SCE_GXM_POLYGON_MODE_TRIANGLE_LINE : SCE_GXM_POLYGON_MODE_TRIANGLE_FILL );
	sceGxmSetBackPolygonMode( GXM_Context(), depth->wireframe
		? SCE_GXM_POLYGON_MODE_TRIANGLE_LINE : SCE_GXM_POLYGON_MODE_TRIANGLE_FILL );
}

unsigned int GXM_ProgramKeyHash( const gxmProgramKey_t *key )
{
	if ( !key->blended ) {
		return (unsigned int)key->alphaTest;	// slot 0..4 are the unblended variants
	}

	// factors are 0..11, so four of them plus the alpha test pack into 23 bits
	unsigned int h = 5;
	h += (unsigned int)key->blend.colorSrc;
	h |= (unsigned int)key->blend.colorDst << 5;
	h |= (unsigned int)key->blend.alphaSrc << 10;
	h |= (unsigned int)key->blend.alphaDst << 15;
	h |= (unsigned int)key->alphaTest      << 20;
	return h;
}

// the engine parses these at boot for its extension/vendor strings
const unsigned char *GXM_GlGetString( unsigned int name )
{
	switch ( name ) {
	case 0x1F00:	return (const unsigned char *)"Sony";			// GL_VENDOR
	case 0x1F01:	return (const unsigned char *)"SGX543MP4+";		// GL_RENDERER
	case 0x1F02:	return (const unsigned char *)"GXM native";		// GL_VERSION
	// for the console dump; WIN_GL_ExtensionSupported is what the renderer asks
	case 0x1F03:	return (const unsigned char *)
						"GL_ARB_texture_compression GL_EXT_texture_compression_s3tc "
						"GL_EXT_texture_env_add";
	default:		return (const unsigned char *)"";
	}
}

// referenced both as calls and as bare pointer tests by the renderer's
// multitexture gates, so they need real symbols rather than macros
void GXM_NoOpTexUnit( unsigned int ) {}
void GXM_NoOpMultiTexCoord2f( unsigned int, float, float ) {}
void GXM_NoOpStencilOpSeparate( unsigned int, unsigned int, unsigned int, unsigned int ) {}

// the renderer caches these at boot; maxTextureSize clamps every texture upload
void GXM_GlGetIntegerv( unsigned int pname, int *params )
{
	if ( !params ) {
		return;
	}
	switch ( pname ) {
	case 0x0D33:	*params = 4096;	break;	// GL_MAX_TEXTURE_SIZE
	case 0x84E2:	*params = 2;	break;	// GL_MAX_TEXTURE_UNITS_ARB
	case 0x0D05:	*params = 4;	break;	// GL_PACK_ALIGNMENT
	case 0x0B44:	*params = 0;	break;	// GL_CULL_FACE
	default:		*params = 0;	break;
	}
}

// GL_COLOR_BUFFER_BIT 0x4000, GL_DEPTH_BUFFER_BIT 0x100
void GXM_GlClear( unsigned int mask )
{
	GXM_ClearBuffers( ( mask & 0x4000 ) != 0, ( mask & 0x0100 ) != 0 );
}

void GXM_GlGetFloatv( unsigned int pname, float *params )
{
	if ( !params ) {
		return;
	}
	switch ( pname ) {
	case 0x84FF:	*params = 1.0f;	break;	// GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
	default:		*params = 0.0f;	break;
	}
}
