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

// gxm_backend.h -- the renderer's draw path on sceGxm

#ifndef GXM_BACKEND_H
#define GXM_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

// programs, caches and the texture table; after GXM_DeviceInit
int  GXM_BackendInit( void );
void GXM_BackendShutdown( void );

// --- textures, keyed by the engine's texnum ---
void GXM_TexUpload( unsigned int texnum, const void *rgba, int width, int height );
void GXM_TexUploadDxt( unsigned int texnum, const void *blob, unsigned int size,
					   unsigned int width, unsigned int height, unsigned int mipCount, int isDxt5 );
void GXM_TexFree( unsigned int texnum );
void GXM_TexBind( int tmu, unsigned int texnum );
void GXM_TexFilter( unsigned int texnum, int linear, int clampToEdge );

// --- state the engine sets through what used to be GL calls ---
void GXM_SetProjection( const float *m );	// column-major, as glLoadMatrixf takes it
void GXM_SetModelView( const float *m );
void GXM_SetStateBits( unsigned int stateBits );
void GXM_SetTexUnitCount( int count );
void GXM_SetVertexColorEnabled( int enabled );
void GXM_SetConstantColor( float r, float g, float b, float a );
void GXM_SetCull( int glCullMode, int enabled );
void GXM_SetViewport( int x, int y, int w, int h );

// one-shot picture of what the backend is actually seeing
void GXM_ReportStats( int *uploads, int *draws, int *textured, int *notex,
					  int *ringfail, unsigned int *firstTex, unsigned int *lastBound );

// draw the current tess contents
void GXM_DrawTess( int numIndexes, const unsigned short *indexes, int numVertexes );

#ifdef __cplusplus
}
#endif

#endif // GXM_BACKEND_H
