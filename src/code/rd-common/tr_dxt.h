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

// tr_dxt.h -- DXT1/DXT5 block compression

#ifndef TR_DXT_H
#define TR_DXT_H

#ifdef __cplusplus
extern "C" {
#endif

// src is 16 RGBA texels, row-major within the 4x4; dst takes 8 bytes for DXT1
// and 16 for DXT5
void R_CompressDxtBlock( unsigned char *dst, const unsigned char *src,
						 int isDxt5, int highQuality );

#ifdef __cplusplus
}
#endif

#endif // TR_DXT_H
