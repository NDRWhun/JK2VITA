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

// companion_gl.h -- the handful of GL entry points the config UI uses, on GXM.
// Private to the companion; the engine has its own path in rd-gxm.

#ifndef COMPANION_GL_H
#define COMPANION_GL_H

#include <psp2/gxm.h>

#include "../code/rd-gxm/gxm_device.h"
#include "../code/rd-gxm/gxm_texture.h"
#include "../code/rd-gxm/gxm_backend.h"

typedef unsigned int GLuint;
typedef int GLint;
typedef float GLfloat;
typedef unsigned int GLenum;

#define GL_QUADS				0x0007
#define GL_TEXTURE_2D			0x0DE1
#define GL_BLEND				0x0BE2
#define GL_DEPTH_TEST			0x0B71
#define GL_PROJECTION			0x1701
#define GL_MODELVIEW			0x1700
#define GL_COLOR_BUFFER_BIT		0x4000
#define GL_RGBA					0x1908
#define GL_UNSIGNED_BYTE		0x1401
#define GL_SRC_ALPHA			0x0302
#define GL_ONE_MINUS_SRC_ALPHA	0x0303
#define GL_NEAREST				0x2600
#define GL_CLAMP_TO_EDGE		0x812F
#define GL_TEXTURE_MIN_FILTER	0x2801
#define GL_TEXTURE_MAG_FILTER	0x2800
#define GL_TEXTURE_WRAP_S		0x2802
#define GL_TEXTURE_WRAP_T		0x2803

// mirrored from tr_local.h; the companion does not include the engine headers
#define CGL_SRCBLEND_SRC_ALPHA			0x00000005
#define CGL_DSTBLEND_ONE_MINUS_SRC_ALPHA	0x00000060
#define CGL_DEPTHTEST_DISABLE			0x00010000

static unsigned int	cgl_texnum = 2048;	// one font atlas, so one slot
static int			cgl_blend;
static int			cgl_textured;

static inline void cgl_ApplyState( void )
{
	unsigned int bits = CGL_DEPTHTEST_DISABLE;
	if ( cgl_blend ) {
		bits |= CGL_SRCBLEND_SRC_ALPHA | CGL_DSTBLEND_ONE_MINUS_SRC_ALPHA;
	}
	GXM_SetStateBits( bits );
	GXM_SetTexUnitCount( cgl_textured ? 1 : 0 );
}

static inline void glBegin( unsigned int mode )		{ cgl_ApplyState(); GXM_ImmBegin( mode ); }
static inline void glEnd( void )					{ GXM_ImmEnd(); }
static inline void glVertex2f( float x, float y )	{ GXM_ImmVertex3f( x, y, 0.0f ); }
static inline void glTexCoord2f( float s, float t )	{ GXM_ImmTexCoord2f( s, t ); }
static inline void glColor4f( float r, float g, float b, float a ) { GXM_ImmColor4f( r, g, b, a ); }

static inline void glEnable( unsigned int cap )
{
	if ( cap == GL_BLEND )			cgl_blend = 1;
	else if ( cap == GL_TEXTURE_2D )	cgl_textured = 1;
}

static inline void glDisable( unsigned int cap )
{
	if ( cap == GL_BLEND )			cgl_blend = 0;
	else if ( cap == GL_TEXTURE_2D )	cgl_textured = 0;
}

static inline void glBlendFunc( unsigned int s, unsigned int d ) { (void)s; (void)d; }
static inline void glMatrixMode( unsigned int m )	{ (void)m; }
static inline void glLoadIdentity( void )			{}

static inline void glGenTextures( int n, unsigned int *out )
{
	(void)n;
	*out = cgl_texnum;
}

static inline void glBindTexture( unsigned int target, unsigned int id )
{
	(void)target;
	GXM_TexBind( 0, id );
}

static inline void glTexImage2D( unsigned int target, int level, int internal,
								 int w, int h, int border, unsigned int fmt,
								 unsigned int type, const void *pixels )
{
	(void)target; (void)level; (void)internal; (void)border; (void)fmt; (void)type;
	GXM_TexUpload( cgl_texnum, pixels, w, h );
	GXM_TexBind( 0, cgl_texnum );
}

static inline void glTexParameteri( unsigned int target, unsigned int pname, int value )
{
	(void)target;
	// the atlas wants point sampling and clamping; both arrive through one call
	if ( pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER ) {
		GXM_TexFilter( cgl_texnum, value != GL_NEAREST, 1 );
	}
}

static inline void glClearColor( float r, float g, float b, float a )
{
	GXM_SetClearColor( r, g, b, a );
}

static inline void glClear( unsigned int mask )
{
	GXM_ClearBuffers( ( mask & GL_COLOR_BUFFER_BIT ) != 0, 0 );
}

// row-major, as glLoadMatrixf takes it
static inline void glOrtho( double l, double r, double b, double t, double n, double f )
{
	static const float ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
	float m[16] = { 0 };
	m[0]  = (float)(  2.0 / ( r - l ) );
	m[5]  = (float)(  2.0 / ( t - b ) );
	m[10] = (float)( -2.0 / ( f - n ) );
	m[12] = (float)( -( r + l ) / ( r - l ) );
	m[13] = (float)( -( t + b ) / ( t - b ) );
	m[14] = (float)( -( f + n ) / ( f - n ) );
	m[15] = 1.0f;
	GXM_SetProjection( m );
	GXM_SetModelView( ident );
}

static inline int cgl_Init( int width, int height )
{
	if ( !GXM_DeviceInit() || !GXM_RingInit( 256 * 1024 ) || !GXM_BackendInit() ) {
		return 0;
	}
	GXM_SetViewport( 0, 0, width, height );
	GXM_SetCull( 0, 0 );
	GXM_SetVertexColorEnabled( 1 );
	GXM_SetConstantColor( 1.0f, 1.0f, 1.0f, 1.0f );
	GXM_BeginFrame();
	return 1;
}

static inline void cgl_Swap( void )
{
	GXM_EndFrame();
	GXM_BeginFrame();
	GXM_RingBeginFrame();
	GXM_SetViewport( 0, 0, GXM_DISPLAY_WIDTH, GXM_DISPLAY_HEIGHT );
}

#endif // COMPANION_GL_H
