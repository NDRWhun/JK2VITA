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

// gxm_state.h -- the engine's GLS_ bitmask translated for GXM.
//
// GXM has no blend state: blending and colour masking are compiled into
// fragment-program instances. So the mask splits in two - the parts that are
// real context calls (depth, cull) and the parts that key a program instance.

#ifndef GXM_STATE_H
#define GXM_STATE_H

#include "gxm_device.h"

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
} gxmDepthState_t;

// split a GLS_ mask; returns false if the mask contains a bit with no GXM equivalent
bool GXM_TranslateState( unsigned int stateBits, gxmProgramKey_t *key, gxmDepthState_t *depth );

// apply the context half; the program half is resolved by the draw path
void GXM_ApplyDepthState( const gxmDepthState_t *depth );

// packs a key into a value usable as a cache index
unsigned int GXM_ProgramKeyHash( const gxmProgramKey_t *key );

#endif // GXM_STATE_H
