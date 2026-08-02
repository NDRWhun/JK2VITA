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

/*
gl_vita_types.h -- the GL types and enums the renderer is written in, at their
OpenGL registry values. Extension enums live in gl_vita_ext.h.
*/
#ifndef JK2VITA_GL_VITA_TYPES_H
#define JK2VITA_GL_VITA_TYPES_H

#ifdef VITA

#include <stddef.h>

typedef void			GLvoid;
typedef unsigned int	GLenum;
typedef unsigned char	GLboolean;
typedef unsigned int	GLbitfield;
typedef signed char		GLbyte;
typedef short			GLshort;
typedef int				GLint;
typedef int				GLsizei;
typedef unsigned char	GLubyte;
typedef unsigned short	GLushort;
typedef unsigned int	GLuint;
typedef float			GLfloat;
typedef float			GLclampf;
typedef double			GLdouble;
typedef double			GLclampd;
typedef char			GLchar;
typedef ptrdiff_t		GLintptr;
typedef ptrdiff_t		GLsizeiptr;

#define GL_FALSE						0
#define GL_TRUE							1
#define GL_NONE							0
#define GL_ZERO							0
#define GL_ONE							1

/* primitives */
#define GL_POINTS						0x0000
#define GL_LINES						0x0001
#define GL_LINE_LOOP					0x0002
#define GL_LINE_STRIP					0x0003
#define GL_TRIANGLES					0x0004
#define GL_TRIANGLE_STRIP				0x0005
#define GL_TRIANGLE_FAN					0x0006
#define GL_QUADS						0x0007
#define GL_QUAD_STRIP					0x0008
#define GL_POLYGON						0x0009

/* clear masks */
#define GL_DEPTH_BUFFER_BIT				0x00000100
#define GL_STENCIL_BUFFER_BIT			0x00000400
#define GL_COLOR_BUFFER_BIT				0x00004000

/* comparison, shared by depth test, alpha test and stencil test */
#define GL_NEVER						0x0200
#define GL_LESS							0x0201
#define GL_EQUAL						0x0202
#define GL_LEQUAL						0x0203
#define GL_GREATER						0x0204
#define GL_NOTEQUAL						0x0205
#define GL_GEQUAL						0x0206
#define GL_ALWAYS						0x0207

/* blend factors */
#define GL_SRC_COLOR					0x0300
#define GL_ONE_MINUS_SRC_COLOR			0x0301
#define GL_SRC_ALPHA					0x0302
#define GL_ONE_MINUS_SRC_ALPHA			0x0303
#define GL_DST_ALPHA					0x0304
#define GL_ONE_MINUS_DST_ALPHA			0x0305
#define GL_DST_COLOR					0x0306
#define GL_ONE_MINUS_DST_COLOR			0x0307
#define GL_SRC_ALPHA_SATURATE			0x0308

/* errors */
#define GL_NO_ERROR						0
#define GL_INVALID_ENUM					0x0500
#define GL_INVALID_VALUE				0x0501
#define GL_INVALID_OPERATION			0x0502
#define GL_STACK_OVERFLOW				0x0503
#define GL_STACK_UNDERFLOW				0x0504
#define GL_OUT_OF_MEMORY				0x0505

/* faces and buffers */
#define GL_FRONT						0x0404
#define GL_BACK							0x0405
#define GL_FRONT_AND_BACK				0x0408
#define GL_BACK_LEFT					0x0402
#define GL_BACK_RIGHT					0x0403

/* enable bits */
#define GL_FOG							0x0B60
#define GL_CULL_FACE					0x0B44
#define GL_DEPTH_TEST					0x0B71
#define GL_STENCIL_TEST					0x0B90
#define GL_ALPHA_TEST					0x0BC0
#define GL_BLEND						0x0BE2
#define GL_SCISSOR_TEST					0x0C11
#define GL_TEXTURE_2D					0x0DE1
#define GL_POLYGON_OFFSET_LINE			0x2A02
#define GL_POLYGON_OFFSET_FILL			0x8037
#define GL_CLIP_PLANE0					0x3000

/* fog */
#define GL_EXP							0x0800
#define GL_EXP2							0x0801
#define GL_FOG_DENSITY					0x0B62
#define GL_FOG_START					0x0B63
#define GL_FOG_END						0x0B64
#define GL_FOG_MODE						0x0B65
#define GL_FOG_COLOR					0x0B66

/* queryable state */
#define GL_DEPTH_CLEAR_VALUE			0x0B73
#define GL_MAX_TEXTURE_SIZE				0x0D33
#define GL_UNPACK_ALIGNMENT				0x0CF5
#define GL_PACK_ALIGNMENT				0x0D05

/* data types */
#define GL_BYTE							0x1400
#define GL_UNSIGNED_BYTE				0x1401
#define GL_SHORT						0x1402
#define GL_UNSIGNED_SHORT				0x1403
#define GL_INT							0x1404
#define GL_UNSIGNED_INT					0x1405
#define GL_FLOAT						0x1406

/* pixel formats */
#define GL_TEXTURE_BORDER_COLOR			0x1004
#define GL_STENCIL_INDEX				0x1901
#define GL_DEPTH_COMPONENT				0x1902
#define GL_RED							0x1903
#define GL_ALPHA						0x1906
#define GL_RGB							0x1907
#define GL_RGBA							0x1908
#define GL_LUMINANCE					0x1909
#define GL_LUMINANCE_ALPHA				0x190A

/* sized internal formats; the format-select switch keeps these apart */
#define GL_RGB5							0x8050
#define GL_RGB8							0x8051
#define GL_RGBA4						0x8056
#define GL_RGBA8						0x8058
#define GL_RGBA16						0x805B

/* stencil ops and texture env combines share this range */
#define GL_KEEP							0x1E00
#define GL_REPLACE						0x1E01
#define GL_INCR							0x1E02
#define GL_DECR							0x1E03
#define GL_INCR_WRAP					0x8507
#define GL_DECR_WRAP					0x8508

/* polygon and shade modes */
#define GL_POINT						0x1B00
#define GL_LINE							0x1B01
#define GL_FILL							0x1B02
#define GL_FLAT							0x1D00
#define GL_SMOOTH						0x1D01

/* strings */
#define GL_VENDOR						0x1F00
#define GL_RENDERER						0x1F01
#define GL_VERSION						0x1F02
#define GL_EXTENSIONS					0x1F03

/* hints */
#define GL_DONT_CARE					0x1100
#define GL_FASTEST						0x1101
#define GL_NICEST						0x1102
#define GL_TEXTURE_COMPRESSION_HINT		0x84EF

/* texture env */
#define GL_MODULATE						0x2100
#define GL_DECAL						0x2101
#define GL_ADD							0x0104
#define GL_TEXTURE_ENV_MODE				0x2200
#define GL_TEXTURE_ENV					0x2300

/* filters and wrap modes */
#define GL_NEAREST						0x2600
#define GL_LINEAR						0x2601
#define GL_NEAREST_MIPMAP_NEAREST		0x2700
#define GL_LINEAR_MIPMAP_NEAREST		0x2701
#define GL_NEAREST_MIPMAP_LINEAR		0x2702
#define GL_LINEAR_MIPMAP_LINEAR			0x2703
#define GL_TEXTURE_MAG_FILTER			0x2800
#define GL_TEXTURE_MIN_FILTER			0x2801
#define GL_TEXTURE_WRAP_S				0x2802
#define GL_TEXTURE_WRAP_T				0x2803
#define GL_CLAMP						0x2900
#define GL_REPEAT						0x2901
#define GL_CLAMP_TO_EDGE				0x812F

/* display lists */
#define GL_COMPILE						0x1300

/* client arrays */
#define GL_VERTEX_ARRAY					0x8074
#define GL_NORMAL_ARRAY					0x8075
#define GL_COLOR_ARRAY					0x8076
#define GL_TEXTURE_COORD_ARRAY			0x8078

/* multitexture */
#define GL_TEXTURE0						0x84C0
#define GL_TEXTURE1						0x84C1
#define GL_TEXTURE2						0x84C2
#define GL_TEXTURE3						0x84C3
#define GL_ACTIVE_TEXTURE				0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE		0x84E1
#define GL_MAX_TEXTURE_UNITS			0x84E2

/* S3TC, the format the texture cache stores */
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT		0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT	0x83F3
#define GL_RGB4_S3TC						0x83A1

/* anisotropic filtering */
#define GL_TEXTURE_MAX_ANISOTROPY_EXT		0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT	0x84FF

/* rectangle textures */
#define GL_TEXTURE_RECTANGLE_EXT		0x84F5
#define GL_TEXTURE_RECTANGLE_ARB		0x84F5

/* buffer objects */
#define GL_ARRAY_BUFFER					0x8892
#define GL_ELEMENT_ARRAY_BUFFER			0x8893
#define GL_STATIC_DRAW					0x88E4

/* framebuffer objects */
#define GL_DEPTH24_STENCIL8				0x88F0
#define GL_DEPTH_STENCIL_ATTACHMENT		0x821A
#define GL_DRAW_FRAMEBUFFER				0x8CA9
#define GL_FRAMEBUFFER_COMPLETE			0x8CD5
#define GL_COLOR_ATTACHMENT0			0x8CE0
#define GL_FRAMEBUFFER					0x8D40
#define GL_RENDERBUFFER					0x8D41

/* shader objects */
#define GL_FRAGMENT_SHADER				0x8B30
#define GL_VERTEX_SHADER				0x8B31
#define GL_COMPILE_STATUS				0x8B81
#define GL_LINK_STATUS					0x8B82

#endif /* VITA */
#endif /* JK2VITA_GL_VITA_TYPES_H */
