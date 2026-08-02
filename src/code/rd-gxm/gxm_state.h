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

// gxm_state.h -- the engine's GLS_ bitmask translated for GXM.
// Blending is not context state here; it is compiled into program instances.

#ifndef GXM_STATE_H
#define GXM_STATE_H

#include "gxm_device.h"

#ifdef __cplusplus
extern "C" {
#endif

// the alpha-test variants the fragment blobs were built with
typedef enum {
	GXM_ATEST_NONE = 0,
	GXM_ATEST_GT_0,
	GXM_ATEST_LT_80,
	GXM_ATEST_GE_80,
	GXM_ATEST_GE_C0,
} gxmAlphaTest_t;

// everything about a GLS_ mask that selects a fragment-program instance
typedef struct {
	SceGxmBlendInfo	blend;
	bool			blended;		// false = pass NULL blendInfo, the opaque fast path
	gxmAlphaTest_t	alphaTest;
} gxmProgramKey_t;

// context state that really is state
typedef struct {
	SceGxmDepthFunc	depthFunc;
	bool			depthWrite;
	bool			wireframe;
} gxmDepthState_t;

// split a GLS_ mask into the program half and the context half
void GXM_TranslateState( unsigned int stateBits, gxmProgramKey_t *key, gxmDepthState_t *depth );

// apply the context half; the program half is resolved by the draw path
void GXM_ApplyDepthState( const gxmDepthState_t *depth );

// qgl_gxm.h routes the qgl macros here; these give them C linkage
const unsigned char *GXM_GlGetString( unsigned int name );
void GXM_GlGetIntegerv( unsigned int pname, int *params );
void GXM_GlGetFloatv( unsigned int pname, float *params );
void GXM_GlClear( unsigned int mask );

// packs a key into a value usable as a cache index
unsigned int GXM_ProgramKeyHash( const gxmProgramKey_t *key );

#ifdef __cplusplus
}
#endif

#endif // GXM_STATE_H
