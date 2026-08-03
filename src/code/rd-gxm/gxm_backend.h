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

// gxm_backend.h -- the renderer's draw path on sceGxm

#ifndef GXM_BACKEND_H
#define GXM_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

// how a second texture combines onto the first, mirroring the GL texture env
#define GXM_TEXENV_MODULATE		0
#define GXM_TEXENV_ADD			1
#define GXM_TEXENV_REPLACE		2

// how the owner exposes tess; unset means draws must supply their own arrays
typedef void (*gxmTessArraysFn_t)( const float **xyz, const float **uv0,
								   const float **uv1, const unsigned char **rgba );
void GXM_SetTessArraysHook( gxmTessArraysFn_t fn );

// programs, caches and the texture table; after GXM_DeviceInit
int  GXM_BackendInit( void );
void GXM_BackendShutdown( void );

// forget the shadowed context state after something else has set it
void GXM_InvalidateStateShadow( void );

// --- textures, keyed by the engine's texnum ---
void GXM_TexUpload( unsigned int texnum, const void *rgba, int width, int height );
int  GXM_TexUploadDxt( unsigned int texnum, const void *blob, unsigned int size,
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
void GXM_SetTexEnv( int env );				// GXM_TEXENV_*
void GXM_SetFog( int enabled, float start, float end, const float *color );
// draw the next tess from these arrays rather than tess itself
void GXM_SetVertexArrays( const float *xyz, const float *uv0, const float *uv1,
						  const unsigned char *rgba );
void GXM_SetConstantColor( float r, float g, float b, float a );
void GXM_SetCull( int glCullMode, int enabled );
void GXM_SetCullFlip( int flip );	// r_gxmCullFlip: invert the winding mapping
void GXM_SetViewport( int x, int y, int w, int h );	// GL rectangle, origin bottom-left
void GXM_SetDepthRange( float zNear, float zFar );
void GXM_SetDepthBias( float factor, float units );	// zero is the off state

// one line describing what the backend actually did; also appended to
// ux0:data/JK2VITA/gxm_stats.log
void GXM_ReportStats( char *out, int outSize );

// append a caller's own stats line to the same file
void GXM_LogStatsLine( const char *line );

// the glBegin/glVertex paths that remain; one draw leaves per block
void GXM_ImmBegin( unsigned int glMode );
void GXM_ImmTexCoord2f( float s, float t );
void GXM_ImmColor4f( float r, float g, float b, float a );
void GXM_ImmColor4ubv( const unsigned char *c );
void GXM_ImmVertex3f( float x, float y, float z );
void GXM_ImmEnd( void );

// draw the current tess contents
void GXM_DrawTess( int numIndexes, const unsigned short *indexes, int numVertexes );

// draw from a vertex buffer that already lives in GPU memory, laid out as
// xyz(12) uv0(8) uv1(8) rgba(4); only the indices pass through the ring
void GXM_DrawStaticBuffer( const void *vertexBuffer, const unsigned short *indexes,
						   int numIndexes, int vertexColor );

#ifdef __cplusplus
}
#endif

#endif // GXM_BACKEND_H
