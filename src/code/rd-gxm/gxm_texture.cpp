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

bool GXM_TextureCreateRGBA( gxmTexture_t *t, const void *rgba, unsigned int w, unsigned int h )
{
	memset( t, 0, sizeof(*t) );

	const unsigned int size = w * h * 4;
	t->data = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size,
		SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, &t->uid );
	if ( !t->data ) {
		return false;
	}

	if ( rgba ) {
		memcpy( t->data, rgba, size );
	} else {
		memset( t->data, 0, size );
	}

	if ( sceGxmTextureInitLinear( &t->tex, t->data,
			SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, w, h, 0 ) < 0 ) {
		GXM_Free( t->uid );
		t->data = NULL;
		return false;
	}

	t->width  = w;
	t->height = h;
	t->valid  = true;
	GXM_TextureSetFilter( t, true, false );
	return true;
}

// DXT1/DXT5 are GXM's UBC1/UBC3 exactly, so the cached blob is uploaded as-is:
// no decode, no re-encode, and a quarter the memory of RGBA
bool GXM_TextureCreateDxt( gxmTexture_t *t, const void *blob, unsigned int size,
						   unsigned int w, unsigned int h, unsigned int mipCount, bool isDxt5 )
{
	memset( t, 0, sizeof(*t) );

	t->data = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size,
		SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, &t->uid );
	if ( !t->data ) {
		return false;
	}
	memcpy( t->data, blob, size );

	const SceGxmTextureFormat fmt = isDxt5
		? SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR : SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR;
	if ( sceGxmTextureInitLinear( &t->tex, t->data, fmt, w, h, mipCount ) < 0 ) {
		GXM_Free( t->uid );
		t->data = NULL;
		return false;
	}

	t->width  = w;
	t->height = h;
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
	ring_lastUsed = ring_offset;
	ring_frame    = ( ring_frame + 1 ) % GXM_RING_FRAMES;
	ring_offset   = 0;

	if ( ring_overflowed ) {
		ring_overflowed = false;
	}
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
