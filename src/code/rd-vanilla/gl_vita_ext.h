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
============================================================================
JK2VITA — Vita GL extension compatibility shim (rd-vanilla).

The OpenJK vanilla renderer references a handful of legacy desktop-GL
extensions that the PS Vita's GXM / vitaGL does not implement:

  * EXT_compiled_vertex_array   (qglLockArraysEXT / qglUnlockArraysEXT)
  * ARB_vertex/fragment_program (the "dynamic glow" effect)
  * NV_register_combiners       (an old multitexture-combine fallback)

All of these are OPTIONAL and runtime-gated in the renderer: it checks the
corresponding qgl* function pointer for NULL (or a support flag derived from
it) before use. On the Vita we therefore declare them as NULL function
pointers (with correct signatures so the gated call sites still type-check)
plus the enum values they reference. The features stay disabled at runtime —
the renderer's existing fallback paths handle their absence.

It also supplies a no-op glTexParameterfv (vitaGL exposes only the integer
form; the only use is GL_TEXTURE_BORDER_COLOR, irrelevant on GXM).

Included only from qgl.h's VITA branch. Storage is defined in gl_vita_ext.cpp.
============================================================================
*/
#ifndef JK2VITA_GL_VITA_EXT_H
#define JK2VITA_GL_VITA_EXT_H

#ifdef VITA

#include <vitaGL.h>

/* GL calling-convention macros (vitaGL doesn't define them; harmless empty). */
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Missing core functions (best-effort shims) ---- */
/* Border color: vitaGL exposes only the integer glTexParameter. */
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
/* Single framebuffer on GXM: draw-buffer selection is a no-op. */
void glDrawBuffer(GLenum mode);
/* Immediate-mode array element: used only by the non-default conformance strip
   path (Vita takes the glDrawElements path). No-op keeps it compiling. */
void glArrayElement(GLint i);

/* ---- Enums vitaGL doesn't define (real GL values; gated code only) ---- */
/* ARB vertex/fragment program */
#ifndef GL_VERTEX_PROGRAM_ARB
#define GL_VERTEX_PROGRAM_ARB            0x8620
#endif
#ifndef GL_FRAGMENT_PROGRAM_ARB
#define GL_FRAGMENT_PROGRAM_ARB          0x8804
#endif
#ifndef GL_PROGRAM_FORMAT_ASCII_ARB
#define GL_PROGRAM_FORMAT_ASCII_ARB      0x8875
#endif
/* NV register combiners */
#ifndef GL_REGISTER_COMBINERS_NV
#define GL_REGISTER_COMBINERS_NV         0x8522
#endif
#ifndef GL_COMBINER0_NV
#define GL_COMBINER0_NV                  0x8550
#endif
#ifndef GL_COMBINER1_NV
#define GL_COMBINER1_NV                  0x8551
#endif
#ifndef GL_NUM_GENERAL_COMBINERS_NV
#define GL_NUM_GENERAL_COMBINERS_NV      0x854E
#endif
#ifndef GL_CONSTANT_COLOR0_NV
#define GL_CONSTANT_COLOR0_NV            0x852A
#endif
#ifndef GL_DISCARD_NV
#define GL_DISCARD_NV                    0x8530
#endif
#ifndef GL_SPARE0_NV
#define GL_SPARE0_NV                     0x852E
#endif
#ifndef GL_SPARE1_NV
#define GL_SPARE1_NV                     0x852F
#endif
#ifndef GL_UNSIGNED_IDENTITY_NV
#define GL_UNSIGNED_IDENTITY_NV          0x8536
#endif
#ifndef GL_UNSIGNED_INVERT_NV
#define GL_UNSIGNED_INVERT_NV            0x8537
#endif
#ifndef GL_VARIABLE_A_NV
#define GL_VARIABLE_A_NV                 0x8523
#endif
#ifndef GL_VARIABLE_B_NV
#define GL_VARIABLE_B_NV                 0x8524
#endif
#ifndef GL_VARIABLE_C_NV
#define GL_VARIABLE_C_NV                 0x8525
#endif
#ifndef GL_VARIABLE_D_NV
#define GL_VARIABLE_D_NV                 0x8526
#endif

/* ---- Function-pointer types + NULL pointers for the unsupported funcs ---- */

/* EXT_compiled_vertex_array */
typedef void (*PFNGLLOCKARRAYSEXTPROC)(GLint first, GLsizei count);
typedef void (*PFNGLUNLOCKARRAYSEXTPROC)(void);
extern PFNGLLOCKARRAYSEXTPROC   qglLockArraysEXT;
extern PFNGLUNLOCKARRAYSEXTPROC qglUnlockArraysEXT;

/* ARB_vertex/fragment_program */
typedef void (*PFNGLPROGRAMSTRINGARBPROC)(GLenum, GLenum, GLsizei, const void *);
typedef void (*PFNGLBINDPROGRAMARBPROC)(GLenum, GLuint);
typedef void (*PFNGLDELETEPROGRAMSARBPROC)(GLsizei, const GLuint *);
typedef void (*PFNGLGENPROGRAMSARBPROC)(GLsizei, GLuint *);
typedef void (*PFNGLPROGRAMENVPARAMETER4DARBPROC)(GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
typedef void (*PFNGLPROGRAMENVPARAMETER4DVARBPROC)(GLenum, GLuint, const GLdouble *);
typedef void (*PFNGLPROGRAMENVPARAMETER4FARBPROC)(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLPROGRAMENVPARAMETER4FVARBPROC)(GLenum, GLuint, const GLfloat *);
typedef void (*PFNGLPROGRAMLOCALPARAMETER4DARBPROC)(GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
typedef void (*PFNGLPROGRAMLOCALPARAMETER4DVARBPROC)(GLenum, GLuint, const GLdouble *);
typedef void (*PFNGLPROGRAMLOCALPARAMETER4FARBPROC)(GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLPROGRAMLOCALPARAMETER4FVARBPROC)(GLenum, GLuint, const GLfloat *);
typedef void (*PFNGLGETPROGRAMENVPARAMETERDVARBPROC)(GLenum, GLuint, GLdouble *);
typedef void (*PFNGLGETPROGRAMENVPARAMETERFVARBPROC)(GLenum, GLuint, GLfloat *);
typedef void (*PFNGLGETPROGRAMLOCALPARAMETERDVARBPROC)(GLenum, GLuint, GLdouble *);
typedef void (*PFNGLGETPROGRAMLOCALPARAMETERFVARBPROC)(GLenum, GLuint, GLfloat *);
typedef void (*PFNGLGETPROGRAMIVARBPROC)(GLenum, GLenum, GLint *);
typedef void (*PFNGLGETPROGRAMSTRINGARBPROC)(GLenum, GLenum, void *);
typedef GLboolean (*PFNGLISPROGRAMARBPROC)(GLuint);
extern PFNGLPROGRAMSTRINGARBPROC              qglProgramStringARB;
extern PFNGLBINDPROGRAMARBPROC                qglBindProgramARB;
extern PFNGLDELETEPROGRAMSARBPROC             qglDeleteProgramsARB;
extern PFNGLGENPROGRAMSARBPROC                qglGenProgramsARB;
extern PFNGLPROGRAMENVPARAMETER4DARBPROC      qglProgramEnvParameter4dARB;
extern PFNGLPROGRAMENVPARAMETER4DVARBPROC     qglProgramEnvParameter4dvARB;
extern PFNGLPROGRAMENVPARAMETER4FARBPROC      qglProgramEnvParameter4fARB;
extern PFNGLPROGRAMENVPARAMETER4FVARBPROC     qglProgramEnvParameter4fvARB;
extern PFNGLPROGRAMLOCALPARAMETER4DARBPROC    qglProgramLocalParameter4dARB;
extern PFNGLPROGRAMLOCALPARAMETER4DVARBPROC   qglProgramLocalParameter4dvARB;
extern PFNGLPROGRAMLOCALPARAMETER4FARBPROC    qglProgramLocalParameter4fARB;
extern PFNGLPROGRAMLOCALPARAMETER4FVARBPROC   qglProgramLocalParameter4fvARB;
extern PFNGLGETPROGRAMENVPARAMETERDVARBPROC   qglGetProgramEnvParameterdvARB;
extern PFNGLGETPROGRAMENVPARAMETERFVARBPROC   qglGetProgramEnvParameterfvARB;
extern PFNGLGETPROGRAMLOCALPARAMETERDVARBPROC qglGetProgramLocalParameterdvARB;
extern PFNGLGETPROGRAMLOCALPARAMETERFVARBPROC qglGetProgramLocalParameterfvARB;
extern PFNGLGETPROGRAMIVARBPROC               qglGetProgramivARB;
extern PFNGLGETPROGRAMSTRINGARBPROC           qglGetProgramStringARB;
extern PFNGLISPROGRAMARBPROC                  qglIsProgramARB;

/* NV_register_combiners */
typedef void (*PFNGLCOMBINERPARAMETERFVNVPROC)(GLenum, const GLfloat *);
typedef void (*PFNGLCOMBINERPARAMETERIVNVPROC)(GLenum, const GLint *);
typedef void (*PFNGLCOMBINERPARAMETERFNVPROC)(GLenum, GLfloat);
typedef void (*PFNGLCOMBINERPARAMETERINVPROC)(GLenum, GLint);
typedef void (*PFNGLCOMBINERINPUTNVPROC)(GLenum, GLenum, GLenum, GLenum, GLenum, GLenum);
typedef void (*PFNGLCOMBINEROUTPUTNVPROC)(GLenum, GLenum, GLenum, GLenum, GLenum, GLenum, GLenum, GLboolean, GLboolean, GLboolean);
typedef void (*PFNGLFINALCOMBINERINPUTNVPROC)(GLenum, GLenum, GLenum, GLenum);
typedef void (*PFNGLGETCOMBINERINPUTPARAMETERFVNVPROC)(GLenum, GLenum, GLenum, GLenum, GLfloat *);
typedef void (*PFNGLGETCOMBINERINPUTPARAMETERIVNVPROC)(GLenum, GLenum, GLenum, GLenum, GLint *);
typedef void (*PFNGLGETCOMBINEROUTPUTPARAMETERFVNVPROC)(GLenum, GLenum, GLenum, GLfloat *);
typedef void (*PFNGLGETCOMBINEROUTPUTPARAMETERIVNVPROC)(GLenum, GLenum, GLenum, GLint *);
typedef void (*PFNGLGETFINALCOMBINERINPUTPARAMETERFVNVPROC)(GLenum, GLenum, GLfloat *);
typedef void (*PFNGLGETFINALCOMBINERINPUTPARAMETERIVNVPROC)(GLenum, GLenum, GLint *);
extern PFNGLCOMBINERPARAMETERFVNVPROC                qglCombinerParameterfvNV;
extern PFNGLCOMBINERPARAMETERIVNVPROC                qglCombinerParameterivNV;
extern PFNGLCOMBINERPARAMETERFNVPROC                 qglCombinerParameterfNV;
extern PFNGLCOMBINERPARAMETERINVPROC                 qglCombinerParameteriNV;
extern PFNGLCOMBINERINPUTNVPROC                      qglCombinerInputNV;
extern PFNGLCOMBINEROUTPUTNVPROC                     qglCombinerOutputNV;
extern PFNGLFINALCOMBINERINPUTNVPROC                 qglFinalCombinerInputNV;
extern PFNGLGETCOMBINERINPUTPARAMETERFVNVPROC        qglGetCombinerInputParameterfvNV;
extern PFNGLGETCOMBINERINPUTPARAMETERIVNVPROC        qglGetCombinerInputParameterivNV;
extern PFNGLGETCOMBINEROUTPUTPARAMETERFVNVPROC       qglGetCombinerOutputParameterfvNV;
extern PFNGLGETCOMBINEROUTPUTPARAMETERIVNVPROC       qglGetCombinerOutputParameterivNV;
extern PFNGLGETFINALCOMBINERINPUTPARAMETERFVNVPROC   qglGetFinalCombinerInputParameterfvNV;
extern PFNGLGETFINALCOMBINERINPUTPARAMETERIVNVPROC   qglGetFinalCombinerInputParameterivNV;

#ifdef __cplusplus
}
#endif

#endif /* VITA */
#endif /* JK2VITA_GL_VITA_EXT_H */
