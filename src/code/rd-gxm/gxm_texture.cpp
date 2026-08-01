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

// gxm_texture.cpp -- GXM textures and the per-frame vertex ring

#include "gxm_texture.h"

#include <psp2/kernel/sysmem.h>
#include <string.h>

// a failed texture reads back as white geometry, so the two ways it can fail are
// counted apart: out of memory vs libgxm rejecting the format
int gxm_texAllocFail, gxm_texInitFail;

bool GXM_TextureCreateRGBA( gxmTexture_t *t, const void *rgba, unsigned int w, unsigned int h )
{
	memset( t, 0, sizeof(*t) );

	// a linear texture's stride is implicit: the width rounded up to 8 texels
	const unsigned int stride = ALIGN( w, 8 );
	const unsigned int size   = stride * h * 4;
	t->data = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size,
		SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, &t->uid );
	if ( !t->data ) {
		gxm_texAllocFail++;
		return false;
	}

	if ( rgba ) {
		if ( stride == w ) {
			memcpy( t->data, rgba, size );
		} else {
			for ( unsigned int y = 0; y < h; y++ ) {
				memcpy( (unsigned char *)t->data + y * stride * 4,
						(const unsigned char *)rgba + y * w * 4, w * 4 );
			}
		}
	} else {
		memset( t->data, 0, size );
	}

	if ( sceGxmTextureInitLinear( &t->tex, t->data,
			SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, w, h, 0 ) < 0 ) {
		gxm_texInitFail++;
		GXM_Free( t->uid );
		t->data = NULL;
		return false;
	}

	t->width  = w;
	t->height = h;
	t->mipCount = 0;		// RGBA uploads are the top level only
	t->valid  = true;
	GXM_TextureSetFilter( t, true, false );
	return true;
}

/*
================
SwizzledIndex

Morton order: the low bits of u and v interleave, v first, and whichever
dimension has bits left over appends them above the interleaved field.
================
*/
static unsigned int SwizzledIndex( unsigned int u, unsigned int v,
								   unsigned int w, unsigned int h )
{
	unsigned int idx = 0, bit = 1, shift = 0;

	while ( bit < w && bit < h ) {
		if ( v & bit ) idx |= 1u << shift;
		shift++;
		if ( u & bit ) idx |= 1u << shift;
		shift++;
		bit <<= 1;
	}
	idx |= ( ( w > h ) ? ( u / bit ) : ( v / bit ) ) << shift;
	return idx;
}

// DXT1/DXT5 are GXM's UBC1/UBC3 block-for-block, but libgxm only accepts block
// compressed data in swizzled layout, so the raster-ordered blocks get shuffled
// into Morton order on the way in. The block contents are untouched.
bool GXM_TextureCreateDxt( gxmTexture_t *t, const void *blob, unsigned int size,
						   unsigned int w, unsigned int h, unsigned int mipCount, bool isDxt5 )
{
	memset( t, 0, sizeof(*t) );

	t->data = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size,
		SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, &t->uid );
	if ( !t->data ) {
		gxm_texAllocFail++;
		return false;
	}

	const unsigned int blockBytes = isDxt5 ? 16 : 8;
	const unsigned char *src = (const unsigned char *)blob;
	unsigned char *dst = (unsigned char *)t->data;
	unsigned int mw = w, mh = h, ofs = 0;

	for ( unsigned int level = 0; level < ( mipCount ? mipCount : 1 ); level++ ) {
		const unsigned int bw = ( mw + 3 ) / 4, bh = ( mh + 3 ) / 4;
		const unsigned int levelSize = bw * bh * blockBytes;
		if ( ofs + levelSize > size ) {
			break;	// the cached blob ran out; keep what decoded cleanly
		}
		for ( unsigned int by = 0; by < bh; by++ ) {
			for ( unsigned int bx = 0; bx < bw; bx++ ) {
				memcpy( dst + ofs + SwizzledIndex( bx, by, bw, bh ) * blockBytes,
						src + ofs + ( by * bw + bx ) * blockBytes, blockBytes );
			}
		}
		ofs += levelSize;
		if ( mw > 1 ) mw >>= 1;
		if ( mh > 1 ) mh >>= 1;
	}

	const SceGxmTextureFormat fmt = isDxt5
		? SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR : SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR;
	if ( sceGxmTextureInitSwizzled( &t->tex, t->data, fmt, w, h, mipCount ) < 0 ) {
		gxm_texInitFail++;
		GXM_Free( t->uid );
		t->data = NULL;
		return false;
	}

	t->width  = w;
	t->height = h;
	t->mipCount = mipCount;
	t->valid  = true;
	GXM_TextureSetFilter( t, true, false );
	return true;
}

void GXM_TextureFree( gxmTexture_t *t )
{
	if ( !t->valid ) {
		return;
	}
	GXM_Free( t->uid );
	memset( t, 0, sizeof(*t) );
}

void GXM_TextureSetFilter( gxmTexture_t *t, bool linear, bool clamp )
{
	if ( !t->valid ) {
		return;
	}

	const SceGxmTextureFilter f = linear ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT;
	sceGxmTextureSetMinFilter( &t->tex, f );
	sceGxmTextureSetMagFilter( &t->tex, f );

	const SceGxmTextureAddrMode m = clamp ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT;
	sceGxmTextureSetUAddrMode( &t->tex, m );
	sceGxmTextureSetVAddrMode( &t->tex, m );

	// without this an uploaded mip chain is never sampled, which both aliases and
	// thrashes the texture cache on anything minified
	sceGxmTextureSetMipFilter( &t->tex, ( t->mipCount > 1 )
		? SCE_GXM_TEXTURE_MIP_FILTER_ENABLED : SCE_GXM_TEXTURE_MIP_FILTER_DISABLED );
}

void GXM_TextureBind( unsigned int unit, const gxmTexture_t *t )
{
	if ( !t || !t->valid ) {
		return;
	}
	sceGxmSetFragmentTexture( GXM_Context(), unit, &t->tex );
}

// ---------------------------------------------------------------------------
// per-frame ring
// ---------------------------------------------------------------------------

#define GXM_RING_FRAMES		GXM_DISPLAY_BUFFERS

static SceUID		 ring_uid;
static unsigned char *ring_base;
static unsigned int	 ring_frameBytes;
static unsigned int	 ring_offset;		// within the current frame's slice
static unsigned int	 ring_frame;
static unsigned int	 ring_lastUsed;
static bool			 ring_overflowed;

bool GXM_RingInit( unsigned int bytesPerFrame )
{
	ring_frameBytes = ALIGN( bytesPerFrame, 4096 );
	ring_base = (unsigned char *)GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		ring_frameBytes * GXM_RING_FRAMES, 4, SCE_GXM_MEMORY_ATTRIB_READ, &ring_uid );

	ring_offset = 0;
	ring_frame  = 0;
	return ( ring_base != NULL );
}

void GXM_RingShutdown( void )
{
	if ( ring_base ) {
		GXM_Free( ring_uid );
		ring_base = NULL;
	}
}

/*
================
GXM_RingBeginFrame

Advancing by one slice per frame gives the GPU GXM_RING_FRAMES-1 frames of grace
before the CPU can catch up with a slice it is still reading.
================
*/
void GXM_RingBeginFrame( void )
{
	// a high-water mark rather than the last sample, so an occasional heavy frame
	// still shows up in a report taken every N frames
	if ( ring_offset > ring_lastUsed ) {
		ring_lastUsed = ring_offset;
	}
	ring_frame  = ( ring_frame + 1 ) % GXM_RING_FRAMES;
	ring_offset = 0;

	if ( ring_overflowed ) {
		ring_overflowed = false;
	}
}

unsigned int GXM_RingBytesPerFrame( void )
{
	return ring_frameBytes;
}

void *GXM_RingAlloc( unsigned int size, unsigned int alignment )
{
	if ( !ring_base ) {
		return NULL;
	}

	const unsigned int aligned = ALIGN( ring_offset, alignment );
	if ( aligned + size > ring_frameBytes ) {
		ring_overflowed = true;	// caller must skip the draw rather than scribble
		return NULL;
	}

	ring_offset = aligned + size;
	return ring_base + ( ring_frame * ring_frameBytes ) + aligned;
}

unsigned int GXM_RingUsedLastFrame( void )
{
	return ring_lastUsed;
}
