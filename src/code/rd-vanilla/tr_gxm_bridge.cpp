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

// tr_gxm_bridge.cpp -- hands the renderer's tess and state to the GXM backend.
// Lives here because tess and glState are private to the renderer.

#ifdef USE_GXM_NATIVE

#include "tr_local.h"
#include "../rd-gxm/gxm_backend.h"

/*
================
GXM_GetTessArrays

tess is private to the renderer, so the backend is handed pointers.
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
