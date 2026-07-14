/*
===========================================================================
Copyright (C) 2026, JK2VITA contributors

This file is part of JK2VITA, a PS Vita port built on the OpenJK
source code.

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

// JK2VITA static-build glue (single ELF; no dlopen on the Vita): home for the
// newlib sizing knobs and any global that would otherwise be multiply defined.

#ifdef VITA
// newlib heap size = the engine's ENTIRE malloc budget (Hunk/Z_Malloc are malloc
// wrappers). Reserved before vglInit: every MiB here is one less for vitaGL's pools.
// Engine peak ~100-120 MiB; 144 leaves margin and ~158 MiB for the vgl RAM pool.
unsigned int _newlib_heap_size_user = 144 * 1024 * 1024;

// VitaSDK reads this to size the main thread's stack. The default (~256 KB) is
// too small for rd-vanilla: R_SubdividePatchToGrid alone uses a ~332 KB stack
// frame (large local drawVert_t grids) during BSP load and overflows it, faulting
// with a data abort. 4 MB is plenty of headroom for the engine's deep call paths.
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;
#endif

