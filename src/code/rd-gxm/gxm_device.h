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

// gxm_device.h -- native GXM device: context, memory, swap chain, frame

#ifndef GXM_DEVICE_H
#define GXM_DEVICE_H

#include <psp2/gxm.h>
#include <psp2/types.h>
#include <psp2/kernel/sysmem.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ALIGN
#define ALIGN(x, a)		(((x) + ((a) - 1)) & ~((a) - 1))
#endif

typedef void (*gxmLogFn_t)( const char *msg );
typedef void (*gxmStateResetFn_t)( void );
void GXM_SetLogger( gxmLogFn_t fn );
void	 GXM_SetStateResetCallback( gxmStateResetFn_t fn );

#define GXM_DISPLAY_WIDTH		960
#define GXM_DISPLAY_HEIGHT		544
#define GXM_DISPLAY_STRIDE		1024	// framebuffers must be 64-byte aligned in width
#define GXM_DISPLAY_BUFFERS		3		// two would stall the GPU behind the flip

// GPU-visible allocation; every pointer the GPU reads must come from here
void	*GXM_Alloc( SceKernelMemBlockType type, unsigned int size, unsigned int alignment,
					unsigned int gpuAttrib, SceUID *uid );
void	 GXM_Free( SceUID uid );
void	*GXM_AllocVertexUsse( unsigned int size, SceUID *uid, unsigned int *usseOffset );
void	 GXM_FreeVertexUsse( SceUID uid );
void	*GXM_AllocFragmentUsse( unsigned int size, SceUID *uid, unsigned int *usseOffset );
void	 GXM_FreeFragmentUsse( SceUID uid );

// call on the thread that will own rendering; every other entry point below
// must then be called from that same thread
bool     GXM_DeviceInit( void );
void	 GXM_DeviceShutdown( void );

// one scene per frame against the swap chain
void	 GXM_BeginFrame( void );
void	 GXM_EndFrame( void );
void	 GXM_SetClearColor( float r, float g, float b, float a );
void	 GXM_ClearBuffers( int color, int depth );	// a fullscreen triangle; GXM has no clear op

// the last presented frame, bottom row first; comps is 3 or 4
void	 GXM_ReadPixels( int x, int y, int width, int height, int comps, int dstStride, void *dst );

// wait out the GPU before releasing anything it may still be reading
void	 GXM_Sync( void );

SceGxmContext		*GXM_Context( void );
SceGxmShaderPatcher	*GXM_ShaderPatcher( void );

#ifdef __cplusplus
}
#endif

#endif // GXM_DEVICE_H
