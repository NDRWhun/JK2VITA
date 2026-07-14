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

/*
 * JK2VITA - standalone config companion app.
 *
 * Its own Vita eboot, launched from a separate LiveArea entry. Draws a settings
 * menu with vitaGL immediate-mode 2D (no SDL/ImGui/vita2d), edits our Vita cvars
 * with the pad, patches them into the game cfg, then sceAppMgrLoadExec's the game.
 * Only vitaGL + psp2 SDK + newlib stdio, nothing else.
 */

#include <vitaGL.h>

#include <psp2/ctrl.h>
#include <psp2/appmgr.h>
#include <psp2/kernel/processmgr.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Constants                                                                  */
/* ------------------------------------------------------------------------- */

#define SCR_W 960
#define SCR_H 544

static const char *CFG_PATH = "ux0:data/JK2VITA/base/openjo_sp.cfg";
static const char *GAME_EXEC = "app0:/eboot.bin";

/* ------------------------------------------------------------------------- */
/* Settings model                                                             */
/* ------------------------------------------------------------------------- */

enum Kind {
	KIND_BOOL = 0,
	KIND_INT,
	KIND_FLOAT,
	KIND_ENUM
};

struct Setting {
	const char *label;
	const char *cvar;
	int kind;
	float vmin;
	float vmax;
	float step;
	float vdef;
	float value; /* current value */
};

static Setting g_settings[] = {
	/* label                       cvar                  kind        min    max    step  default */
	{ "Texture detail (picmip)",  "r_picmip",            KIND_INT,   0.0f,  3.0f,  1.0f,  1.0f,  1.0f },
	{ "Curve quality (subdiv)",   "r_subdivisions",      KIND_INT,   4.0f,  24.0f, 4.0f,  4.0f,  4.0f },
	{ "Model LOD bias",           "r_lodbias",           KIND_INT,   0.0f,  2.0f,  1.0f,  0.0f,  0.0f },
	{ "Render distance cap",      "r_distanceCull",      KIND_INT,   0.0f,  6000.0f, 500.0f, 0.0f, 0.0f },
	{ "Forced fog distance",      "r_forceFog",          KIND_INT,   0.0f,  6000.0f, 500.0f, 0.0f, 0.0f },
	{ "Crowd LOD (chars)",        "r_ghoul2CrowdLod",    KIND_INT,   0.0f,  16.0f, 1.0f,  4.0f,  4.0f },
	{ "Crowd LOD step",           "r_ghoul2CrowdLodStep",KIND_INT,   0.0f,  8.0f,  1.0f,  3.0f,  3.0f },
	{ "Player shadows",           "cg_shadows",          KIND_ENUM,  0.0f,  1.0f,  1.0f,  1.0f,  1.0f },
	{ "Rear-touch controls",      "vita_rearTouch",      KIND_BOOL,  0.0f,  1.0f,  1.0f,  1.0f,  1.0f },
};

static const int g_numSettings = (int)(sizeof(g_settings) / sizeof(g_settings[0]));

/* ------------------------------------------------------------------------- */
/* font8x8_basic - public domain 8x8 bitmap font (dhepper/font8x8)            */
/* ASCII 0x00..0x7F. Each glyph is 8 bytes (one per row); within a byte the   */
/* LSB is the leftmost pixel (standard font8x8 convention).                   */
/* ------------------------------------------------------------------------- */

static const unsigned char font8x8_basic[128][8] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0000 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0001 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0002 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0003 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0004 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0005 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0006 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0007 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0008 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0009 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+000A */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+000B */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+000C */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+000D */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+000E */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+000F */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0010 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0011 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0012 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0013 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0014 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0015 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0016 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0017 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0018 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0019 */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+001A */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+001B */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+001C */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+001D */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+001E */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+001F */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0020 (space) */
	{ 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00 }, /* U+0021 (!) */
	{ 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0022 (") */
	{ 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00 }, /* U+0023 (#) */
	{ 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00 }, /* U+0024 ($) */
	{ 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00 }, /* U+0025 (%) */
	{ 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00 }, /* U+0026 (&) */
	{ 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0027 (') */
	{ 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00 }, /* U+0028 (() */
	{ 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00 }, /* U+0029 ()) */
	{ 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 }, /* U+002A (*) */
	{ 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00 }, /* U+002B (+) */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06 }, /* U+002C (,) */
	{ 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00 }, /* U+002D (-) */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00 }, /* U+002E (.) */
	{ 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00 }, /* U+002F (/) */
	{ 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00 }, /* U+0030 (0) */
	{ 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00 }, /* U+0031 (1) */
	{ 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00 }, /* U+0032 (2) */
	{ 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00 }, /* U+0033 (3) */
	{ 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00 }, /* U+0034 (4) */
	{ 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00 }, /* U+0035 (5) */
	{ 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00 }, /* U+0036 (6) */
	{ 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00 }, /* U+0037 (7) */
	{ 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00 }, /* U+0038 (8) */
	{ 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00 }, /* U+0039 (9) */
	{ 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00 }, /* U+003A (:) */
	{ 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06 }, /* U+003B (;) */
	{ 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00 }, /* U+003C (<) */
	{ 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00 }, /* U+003D (=) */
	{ 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00 }, /* U+003E (>) */
	{ 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00 }, /* U+003F (?) */
	{ 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00 }, /* U+0040 (@) */
	{ 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00 }, /* U+0041 (A) */
	{ 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00 }, /* U+0042 (B) */
	{ 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00 }, /* U+0043 (C) */
	{ 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00 }, /* U+0044 (D) */
	{ 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00 }, /* U+0045 (E) */
	{ 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00 }, /* U+0046 (F) */
	{ 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00 }, /* U+0047 (G) */
	{ 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00 }, /* U+0048 (H) */
	{ 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 }, /* U+0049 (I) */
	{ 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00 }, /* U+004A (J) */
	{ 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00 }, /* U+004B (K) */
	{ 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00 }, /* U+004C (L) */
	{ 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00 }, /* U+004D (M) */
	{ 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00 }, /* U+004E (N) */
	{ 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00 }, /* U+004F (O) */
	{ 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00 }, /* U+0050 (P) */
	{ 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00 }, /* U+0051 (Q) */
	{ 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00 }, /* U+0052 (R) */
	{ 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00 }, /* U+0053 (S) */
	{ 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 }, /* U+0054 (T) */
	{ 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00 }, /* U+0055 (U) */
	{ 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00 }, /* U+0056 (V) */
	{ 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 }, /* U+0057 (W) */
	{ 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00 }, /* U+0058 (X) */
	{ 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00 }, /* U+0059 (Y) */
	{ 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00 }, /* U+005A (Z) */
	{ 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00 }, /* U+005B ([) */
	{ 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00 }, /* U+005C (\) */
	{ 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00 }, /* U+005D (]) */
	{ 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00 }, /* U+005E (^) */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF }, /* U+005F (_) */
	{ 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+0060 (`) */
	{ 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00 }, /* U+0061 (a) */
	{ 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00 }, /* U+0062 (b) */
	{ 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00 }, /* U+0063 (c) */
	{ 0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00 }, /* U+0064 (d) */
	{ 0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00 }, /* U+0065 (e) */
	{ 0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00 }, /* U+0066 (f) */
	{ 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F }, /* U+0067 (g) */
	{ 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00 }, /* U+0068 (h) */
	{ 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 }, /* U+0069 (i) */
	{ 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E }, /* U+006A (j) */
	{ 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00 }, /* U+006B (k) */
	{ 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 }, /* U+006C (l) */
	{ 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00 }, /* U+006D (m) */
	{ 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00 }, /* U+006E (n) */
	{ 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00 }, /* U+006F (o) */
	{ 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F }, /* U+0070 (p) */
	{ 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78 }, /* U+0071 (q) */
	{ 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00 }, /* U+0072 (r) */
	{ 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00 }, /* U+0073 (s) */
	{ 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00 }, /* U+0074 (t) */
	{ 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00 }, /* U+0075 (u) */
	{ 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00 }, /* U+0076 (v) */
	{ 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00 }, /* U+0077 (w) */
	{ 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00 }, /* U+0078 (x) */
	{ 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F }, /* U+0079 (y) */
	{ 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00 }, /* U+007A (z) */
	{ 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00 }, /* U+007B ({) */
	{ 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00 }, /* U+007C (|) */
	{ 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00 }, /* U+007D (}) */
	{ 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+007E (~) */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* U+007F */
};

/* ------------------------------------------------------------------------- */
/* Font texture                                                               */
/* ------------------------------------------------------------------------- */

/*
 * 128x128 RGBA atlas, 16 glyphs/row in 8x8 cells. White where the font bit is
 * set, transparent elsewhere so blending leaves only the lit pixels.
 */
#define FONT_TEX_DIM 128
#define GLYPHS_PER_ROW 16
#define GLYPH_PX 8

static GLuint g_fontTex = 0;

static void buildFontTexture(void)
{
	static unsigned char pixels[FONT_TEX_DIM * FONT_TEX_DIM * 4];
	memset(pixels, 0, sizeof(pixels));

	for (int ch = 0; ch < 128; ++ch) {
		int cellX = (ch % GLYPHS_PER_ROW) * GLYPH_PX;
		int cellY = (ch / GLYPHS_PER_ROW) * GLYPH_PX;
		for (int row = 0; row < GLYPH_PX; ++row) {
			unsigned char bits = font8x8_basic[ch][row];
			for (int col = 0; col < GLYPH_PX; ++col) {
				if (bits & (1 << col)) { /* LSB = leftmost pixel */
					int px = cellX + col;
					int py = cellY + row;
					int idx = (py * FONT_TEX_DIM + px) * 4;
					pixels[idx + 0] = 255;
					pixels[idx + 1] = 255;
					pixels[idx + 2] = 255;
					pixels[idx + 3] = 255;
				}
			}
		}
	}

	glGenTextures(1, &g_fontTex);
	glBindTexture(GL_TEXTURE_2D, g_fontTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FONT_TEX_DIM, FONT_TEX_DIM, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* ------------------------------------------------------------------------- */
/* Drawing helpers                                                            */
/* ------------------------------------------------------------------------- */

static void drawRect(float x, float y, float w, float h,
	float r, float g, float b, float a)
{
	glDisable(GL_TEXTURE_2D);
	if (a < 1.0f) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glDisable(GL_BLEND);
	}
	glColor4f(r, g, b, a);
	glBegin(GL_QUADS);
	glVertex2f(x, y);
	glVertex2f(x + w, y);
	glVertex2f(x + w, y + h);
	glVertex2f(x, y + h);
	glEnd();
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* textured 8x8 quad scaled by 'scale', uv'd to the glyph's cell in the atlas */
static void drawGlyph(float x, float y, float scale, unsigned char ch)
{
	if (ch >= 128)
		ch = 0;

	float cellX = (float)((ch % GLYPHS_PER_ROW) * GLYPH_PX);
	float cellY = (float)((ch / GLYPHS_PER_ROW) * GLYPH_PX);

	float u0 = cellX / (float)FONT_TEX_DIM;
	float v0 = cellY / (float)FONT_TEX_DIM;
	float u1 = (cellX + GLYPH_PX) / (float)FONT_TEX_DIM;
	float v1 = (cellY + GLYPH_PX) / (float)FONT_TEX_DIM;

	float w = GLYPH_PX * scale;
	float h = GLYPH_PX * scale;

	glBegin(GL_QUADS);
	glTexCoord2f(u0, v0); glVertex2f(x, y);
	glTexCoord2f(u1, v0); glVertex2f(x + w, y);
	glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
	glTexCoord2f(u0, v1); glVertex2f(x, y + h);
	glEnd();
}

static void drawText(float x, float y, float scale,
	float r, float g, float b, const char *str)
{
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, g_fontTex);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(r, g, b, 1.0f);

	float cx = x;
	for (const char *p = str; *p; ++p) {
		drawGlyph(cx, y, scale, (unsigned char)*p);
		cx += GLYPH_PX * scale;
	}

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glDisable(GL_TEXTURE_2D);
}

/* Approximate text pixel width for right-aligning values. */
static float textWidth(const char *str, float scale)
{
	return (float)strlen(str) * GLYPH_PX * scale;
}

/* ------------------------------------------------------------------------- */
/* Value formatting                                                           */
/* ------------------------------------------------------------------------- */

/* Round-to-nearest int for INT/BOOL/ENUM storage. */
static int valueAsInt(const Setting &s)
{
	return (int)(s.value + (s.value >= 0.0f ? 0.5f : -0.5f));
}

static void clampValue(Setting &s)
{
	if (s.value < s.vmin)
		s.value = s.vmin;
	if (s.value > s.vmax)
		s.value = s.vmax;
}

/* Human-readable display string for the current value. */
static void formatDisplay(const Setting &s, char *out, size_t outsz)
{
	switch (s.kind) {
	case KIND_BOOL:
		snprintf(out, outsz, "%s", valueAsInt(s) ? "ON" : "OFF");
		break;
	case KIND_ENUM: /* cg_shadows: 0 -> OFF, 1 -> BLOB */
		snprintf(out, outsz, "%s", valueAsInt(s) ? "BLOB" : "OFF");
		break;
	case KIND_INT:
		snprintf(out, outsz, "%d", valueAsInt(s));
		break;
	case KIND_FLOAT:
	default:
		snprintf(out, outsz, "%.2f", s.value);
		break;
	}
}

/* Canonical string for writing into the cfg (numbers only). */
static void formatStore(const Setting &s, char *out, size_t outsz)
{
	if (s.kind == KIND_FLOAT)
		snprintf(out, outsz, "%.2f", s.value);
	else
		snprintf(out, outsz, "%d", valueAsInt(s));
}

/* ------------------------------------------------------------------------- */
/* Config read                                                                */
/* ------------------------------------------------------------------------- */

/* Returns index of the setting whose cvar matches 'name', or -1. */
static int findSetting(const char *name)
{
	for (int i = 0; i < g_numSettings; ++i) {
		if (strcmp(g_settings[i].cvar, name) == 0)
			return i;
	}
	return -1;
}

/*
 * Find a token matching one of our cvars, take the next numeric token as its
 * value. Tolerant of spacing/quotes: `seta r_picmip "1"` and `seta r_picmip 1`
 * both work.
 */
static void parseLine(char *line)
{
	/* tokenize on whitespace and quotes */
	char *tokens[16];
	int ntok = 0;

	char *p = line;
	while (*p && ntok < 16) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ||
			*p == '"')
			++p;
		if (!*p)
			break;
		tokens[ntok++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\r' &&
			*p != '\n' && *p != '"')
			++p;
		if (*p)
			*p++ = '\0';
	}

	if (ntok < 2)
		return;

	for (int t = 0; t + 1 < ntok; ++t) {
		int idx = findSetting(tokens[t]);
		if (idx < 0)
			continue;

		char *endp = NULL;
		double v = strtod(tokens[t + 1], &endp);
		if (endp == tokens[t + 1])
			continue; /* not numeric; ignore */

		g_settings[idx].value = (float)v;
		clampValue(g_settings[idx]);
		return;
	}
}

static void loadConfig(void)
{
	FILE *f = fopen(CFG_PATH, "r");
	if (!f)
		return; /* keep defaults */

	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		parseLine(line); /* it mutates line, but this is a scratch copy */
	}
	fclose(f);
}

/* ------------------------------------------------------------------------- */
/* Config write                                                               */
/* ------------------------------------------------------------------------- */

/* If 'line' is a `seta <cvar>` for one of our cvars, return its index, else -1. */
static int lineManagedSetting(const char *line)
{
	const char *p = line;
	while (*p == ' ' || *p == '\t')
		++p;

	if (strncmp(p, "seta", 4) != 0)
		return -1;
	if (p[4] != ' ' && p[4] != '\t')
		return -1;
	p += 4;
	while (*p == ' ' || *p == '\t')
		++p;

	/* cvar name token, stop at space/tab/quote/newline */
	char name[128];
	int n = 0;
	while (*p && *p != ' ' && *p != '\t' && *p != '"' && *p != '\r' &&
		*p != '\n' && n < (int)sizeof(name) - 1) {
		name[n++] = *p++;
	}
	name[n] = '\0';

	return findSetting(name);
}

static void writeConfig(void)
{
	/* slurp the existing file (if any) so we can rewrite in place */
	char *buf = NULL;
	long size = 0;

	FILE *fin = fopen(CFG_PATH, "r");
	if (fin) {
		fseek(fin, 0, SEEK_END);
		size = ftell(fin);
		if (size < 0)
			size = 0;
		fseek(fin, 0, SEEK_SET);
		buf = (char *)malloc((size_t)size + 1);
		if (buf) {
			size_t rd = fread(buf, 1, (size_t)size, fin);
			buf[rd] = '\0';
			size = (long)rd;
		}
		fclose(fin);
	}

	/* which of our cvars we've already written */
	bool emitted[64];
	for (int i = 0; i < g_numSettings; ++i)
		emitted[i] = false;

	FILE *fout = fopen(CFG_PATH, "w");
	if (!fout) {
		free(buf);
		return;
	}

	if (buf) {
		/* line by line; copes with a missing trailing newline */
		char *cur = buf;
		char *end = buf + size;

		while (cur < end) {
			char *nl = cur;
			while (nl < end && *nl != '\n')
				++nl;

			bool hasNewline = (nl < end); /* points at '\n' */

			/* NUL-terminated copy of the line (no newline) for inspection */
			size_t lineLen = (size_t)(nl - cur);
			char tmp[1024];
			size_t copyLen = lineLen < sizeof(tmp) - 1 ? lineLen
				: sizeof(tmp) - 1;
			memcpy(tmp, cur, copyLen);
			tmp[copyLen] = '\0';

			int idx = lineManagedSetting(tmp);
			if (idx >= 0) {
				/* Replace with our canonical seta line. */
				char val[64];
				formatStore(g_settings[idx], val, sizeof(val));
				fprintf(fout, "seta %s \"%s\"\n",
					g_settings[idx].cvar, val);
				emitted[idx] = true;
			} else {
				/* Keep the line verbatim. */
				fwrite(cur, 1, lineLen, fout);
				fputc('\n', fout);
			}

			if (hasNewline)
				cur = nl + 1;
			else
				cur = nl; /* == end, loop terminates */
		}
	}

	/* Append any of our cvars that did not already appear. */
	for (int i = 0; i < g_numSettings; ++i) {
		if (emitted[i])
			continue;
		char val[64];
		formatStore(g_settings[i], val, sizeof(val));
		fprintf(fout, "seta %s \"%s\"\n", g_settings[i].cvar, val);
	}

	fclose(fout);
	free(buf);
}

/* ------------------------------------------------------------------------- */
/* Rendering the menu                                                         */
/* ------------------------------------------------------------------------- */

static void setup2D(void)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, SCR_W, SCR_H, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glDisable(GL_DEPTH_TEST);
}

static void renderMenu(int selected)
{
	glClearColor(0.06f, 0.08f, 0.12f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	setup2D();

	/* Title. */
	drawText(40.0f, 30.0f, 3.0f, 0.55f, 0.95f, 1.0f, "JK2VITA  CONFIGURATION");

	/* fixed-height rows; a window scrolls to follow the selection, scrollbar on the right */
	const float rowStartY = 80.0f;
	const float rowStep   = 34.0f;
	const float labelX    = 60.0f;
	const float valueX    = 560.0f;
	const float bodyScale = 2.0f;
	const int   visibleCount = 12;

	/* keep the selection inside the window; scrollTop persists across frames */
	static int scrollTop = 0;
	if (selected < scrollTop)                  scrollTop = selected;
	if (selected >= scrollTop + visibleCount)  scrollTop = selected - visibleCount + 1;
	int maxTop = g_numSettings - visibleCount;
	if (maxTop < 0) maxTop = 0;
	if (scrollTop > maxTop) scrollTop = maxTop;
	if (scrollTop < 0)      scrollTop = 0;

	int last = scrollTop + visibleCount;
	if (last > g_numSettings) last = g_numSettings;

	for (int i = scrollTop; i < last; ++i) {
		float y = rowStartY + rowStep * (i - scrollTop);

		if (i == selected) {
			/* Translucent highlight behind the selected row. */
			drawRect(40.0f, y - 4.0f, (float)SCR_W - 110.0f, rowStep - 4.0f,
				0.20f, 0.70f, 0.95f, 0.25f);
		}

		float lr = (i == selected) ? 1.0f : 0.85f;
		float lg = (i == selected) ? 1.0f : 0.85f;
		float lb = (i == selected) ? 1.0f : 0.90f;

		drawText(labelX, y, bodyScale, lr, lg, lb, g_settings[i].label);

		char val[64];
		formatDisplay(g_settings[i], val, sizeof(val));
		drawText(valueX, y, bodyScale, 0.70f, 1.0f, 0.70f, val);
	}

	/* Scrollbar (only when the list is taller than the window). */
	if (g_numSettings > visibleCount) {
		const float trackX = (float)SCR_W - 34.0f;
		const float trackW = 12.0f;
		const float trackY = rowStartY - 4.0f;
		const float trackH = (float)visibleCount * rowStep;
		drawRect(trackX, trackY, trackW, trackH, 0.20f, 0.24f, 0.30f, 0.55f);

		float thumbH = trackH * (float)visibleCount / (float)g_numSettings;
		if (thumbH < 18.0f) thumbH = 18.0f;
		float thumbY = trackY + (trackH - thumbH) * (float)scrollTop / (float)maxTop;
		drawRect(trackX, thumbY, trackW, thumbH, 0.45f, 0.80f, 0.95f, 0.9f);
	}

	/* Help line at the bottom. */
	drawText(40.0f, 500.0f, 2.0f, 0.70f, 0.75f, 0.85f,
		"D-pad: navigate / change    X: Save & Play    O: Play    /\\: Reset");

	vglSwapBuffers(GL_FALSE);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                       */
/* ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	/* vitaGL: 2MB legacy pool for immediate-mode 2D, no MSAA. */
	vglInitExtended(0x200000, SCR_W, SCR_H, 0x1000000, SCE_GXM_MULTISAMPLE_NONE);

	buildFontTexture();

	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

	loadConfig();

	int selected = 0;
	unsigned int prevButtons = 0;

	bool launch = false;
	bool saveBeforeLaunch = false;

	for (;;) {
		SceCtrlData pad;
		memset(&pad, 0, sizeof(pad));
		sceCtrlPeekBufferPositive(0, &pad, 1);

		/* Edge detection: only act on the press transition. */
		unsigned int pressed = pad.buttons & ~prevButtons;
		prevButtons = pad.buttons;

		if (pressed & SCE_CTRL_UP) {
			selected = (selected - 1 + g_numSettings) % g_numSettings;
		}
		if (pressed & SCE_CTRL_DOWN) {
			selected = (selected + 1) % g_numSettings;
		}
		if (pressed & SCE_CTRL_LEFT) {
			g_settings[selected].value -= g_settings[selected].step;
			clampValue(g_settings[selected]);
		}
		if (pressed & SCE_CTRL_RIGHT) {
			g_settings[selected].value += g_settings[selected].step;
			clampValue(g_settings[selected]);
		}
		if (pressed & SCE_CTRL_TRIANGLE) {
			/* Reset all rows to defaults (in-memory only). */
			for (int i = 0; i < g_numSettings; ++i)
				g_settings[i].value = g_settings[i].vdef;
		}
		if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_START)) {
			launch = true;
			saveBeforeLaunch = true;
		}
		if (pressed & SCE_CTRL_CIRCLE) {
			launch = true;
			saveBeforeLaunch = false;
		}

		renderMenu(selected);

		if (launch) {
			if (saveBeforeLaunch)
				writeConfig();
			sceAppMgrLoadExec(GAME_EXEC, NULL, NULL);
			/* Process is replaced on success; bail out regardless. */
			break;
		}
	}

	return 0;
}
