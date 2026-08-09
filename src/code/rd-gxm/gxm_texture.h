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

// gxm_texture.h -- GXM textures and the per-frame vertex ring

#ifndef GXM_TEXTURE_H
#define GXM_TEXTURE_H

#include "gxm_device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	SceGxmTexture	tex;
	SceUID			uid;
	void			*data;
	unsigned int	width, height;
	unsigned int	bytes;			// what it took from its pool
	unsigned int	mipCount;		// 0 = top level only, so no mip filtering
	bool			valid;
} gxmTexture_t;

// linear A8B8G8R8 upload, top level only; swizzled goes through GXM_TextureCreateDxt
bool	GXM_TextureCreateRGBA( gxmTexture_t *t, const void *rgba, unsigned int w, unsigned int h );
bool	GXM_TextureCreateDxt( gxmTexture_t *t, const void *blob, unsigned int size,
							  unsigned int w, unsigned int h, unsigned int mipCount, bool isDxt5 );
bool	GXM_TextureUpdateRGBA( gxmTexture_t *t, const void *rgba, unsigned int w, unsigned int h );
void	GXM_TextureFree( gxmTexture_t *t );
void	GXM_TextureBind( unsigned int unit, const gxmTexture_t *t );
void	GXM_TextureSetFilter( gxmTexture_t *t, bool linear, bool clamp );

// Per-frame ring. GXM defers execution, so anything a draw references must stay
// untouched until the GPU consumes the scene; tess buffers get copied in here.
bool	GXM_RingInit( unsigned int bytesPerFrame );
void	GXM_RingShutdown( void );
void	GXM_RingBeginFrame( void );
void   *GXM_RingAlloc( unsigned int size, unsigned int alignment );
unsigned int GXM_RingUsedLastFrame( void );
unsigned int GXM_RingBytesPerFrame( void );

// probe-only test scene
bool	GXM_TestSceneInit( void );
void	GXM_DrawTestQuad( float frame );
void	GXM_TestSceneShutdown( void );

#ifdef __cplusplus
}
#endif

#endif // GXM_TEXTURE_H
