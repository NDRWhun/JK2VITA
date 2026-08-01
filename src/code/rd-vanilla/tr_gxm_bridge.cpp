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

// tr_gxm_bridge.cpp -- hands the renderer's tess and state to the GXM backend.
//
// Lives in rd-vanilla because tess and glState are private to the renderer; the
// backend itself stays free of engine headers.

#ifdef USE_GXM_NATIVE

#include "tr_local.h"
#include "../rd-gxm/gxm_backend.h"

/*
================
GXM_GetTessArrays

tess.xyz is vec4 for SIMD alignment; the texcoords and colours are the per-stage
arrays ComputeTexCoords and ComputeColors just filled.
================
*/
extern "C" void GXM_GetTessArrays( const float **xyz, const float **uv0,
								   const float **uv1, const unsigned char **rgba )
{
	shaderCommands_t *t = tessPtr;

	*xyz  = (const float *)t->xyz;
	*uv0  = (const float *)t->svars.texcoords[0];
	*uv1  = (const float *)t->svars.texcoords[1];
	*rgba = (const unsigned char *)t->svars.colors;
}

#endif // USE_GXM_NATIVE
