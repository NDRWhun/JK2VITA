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

// gxm_texture.cpp -- GXM textures and the per-frame vertex ring

#include "gxm_texture.h"

#include <psp2/kernel/sysmem.h>
#include <string.h>

// a failed texture draws white, so the two failure modes are counted apart
int gxm_texAllocFail, gxm_texInitFail;
unsigned int gxm_texBytes;

// a power-of-two texture this big is an exact multiple of CDRAM's granularity
#define GXM_TEX_CDRAM_MIN	( 256 * 1024 )

static void *TexAlloc( unsigned int size, SceUID *uid )
{
	void *mem = NULL;
	if ( size >= GXM_TEX_CDRAM_MIN ) {
		mem = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size,
			SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, uid );
	}
	if ( !mem ) {	// small, or CDRAM is full
		mem = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size,
			SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, uid );
	}
	if ( mem ) {
		gxm_texBytes += size;
	} else {
		gxm_texAllocFail++;
	}
	return mem;
}

bool GXM_TextureCreateRGBA( gxmTexture_t *t, const void *rgba, unsigned int w, unsigned int h )
{
	memset( t, 0, sizeof(*t) );

	// a linear texture's stride is implicit: the width rounded up to 8 texels
	const unsigned int stride = ALIGN( w, 8 );
	const unsigned int size   = stride * h * 4;
	t->data = TexAlloc( size, &t->uid );
	if ( !t->data ) {
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
		gxm_texBytes -= size;
		GXM_Free( t->uid );
		t->data = NULL;
		return false;
	}

	t->width  = w;
	t->height = h;
	t->bytes  = size;
	t->mipCount = 0;		// RGBA uploads are the top level only
	t->valid  = true;
	GXM_TextureSetFilter( t, true, false );
	return true;
}

// a video frame arrives every frame at a fixed size; reallocating each time would
// churn memblocks, so an in-place copy is the analogue of glTexSubImage2D
bool GXM_TextureUpdateRGBA( gxmTexture_t *t, const void *rgba, unsigned int w, unsigned int h )
{
	if ( !t->valid || !rgba || t->width != w || t->height != h || t->mipCount ) {
		return false;
	}

	const unsigned int stride = ALIGN( w, 8 );
	if ( stride == w ) {
		memcpy( t->data, rgba, stride * h * 4 );
	} else {
		for ( unsigned int y = 0; y < h; y++ ) {
			memcpy( (unsigned char *)t->data + y * stride * 4,
					(const unsigned char *)rgba + y * w * 4, w * 4 );
		}
	}
	return true;
}

/*
================
SwizzledIndex

Morton order: u and v interleave, v first, odd bits appended on top.
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

// UBC1/UBC3 are DXT1/DXT5, but libgxm only takes them swizzled
bool GXM_TextureCreateDxt( gxmTexture_t *t, const void *blob, unsigned int size,
						   unsigned int w, unsigned int h, unsigned int mipCount, bool isDxt5 )
{
	memset( t, 0, sizeof(*t) );

	t->data = TexAlloc( size, &t->uid );
	if ( !t->data ) {
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
			gxm_texBytes -= size;
			GXM_Free( t->uid );	// the descriptor below would claim mips this cannot hold
			memset( t, 0, sizeof(*t) );
			return false;
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
		gxm_texBytes -= size;
		GXM_Free( t->uid );
		t->data = NULL;
		return false;
	}

	t->width  = w;
	t->height = h;
	t->bytes  = size;
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
	gxm_texBytes -= t->bytes;
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

	// without this an uploaded mip chain is never sampled
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
	// the ring outlives a vid_restart, which brings the render thread back up
	if ( ring_base ) {
		ring_offset = 0;
		ring_frame  = 0;
		return true;
	}

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

One slice per frame gives the GPU a frame of grace before reuse.
================
*/
void GXM_RingBeginFrame( void )
{
	// a high-water mark, so a heavy frame still shows in an occasional report
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
