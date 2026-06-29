/*
============================================================================
JK2VITA — Vita GL extension compatibility shim, storage + stubs.
See gl_vita_ext.h. These unsupported-extension function pointers stay NULL so
the renderer's runtime gates (`if (qgl... )`) skip the corresponding paths.
============================================================================
*/
#ifdef VITA

#include "gl_vita_ext.h"

extern "C" {

/* vitaGL has only the integer glTexParameter; the float form is used solely for
   GL_TEXTURE_BORDER_COLOR, which has no effect under GXM. Best-effort no-op. */
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
	(void)target; (void)pname; (void)params;
}

void glDrawBuffer(GLenum mode)
{
	(void)mode;
}

void glArrayElement(GLint i)
{
	(void)i;
}

/* EXT_compiled_vertex_array */
PFNGLLOCKARRAYSEXTPROC   qglLockArraysEXT   = 0;
PFNGLUNLOCKARRAYSEXTPROC qglUnlockArraysEXT = 0;

/* ARB_vertex/fragment_program */
PFNGLPROGRAMSTRINGARBPROC              qglProgramStringARB              = 0;
PFNGLBINDPROGRAMARBPROC                qglBindProgramARB                = 0;
PFNGLDELETEPROGRAMSARBPROC             qglDeleteProgramsARB             = 0;
PFNGLGENPROGRAMSARBPROC                qglGenProgramsARB                = 0;
PFNGLPROGRAMENVPARAMETER4DARBPROC      qglProgramEnvParameter4dARB      = 0;
PFNGLPROGRAMENVPARAMETER4DVARBPROC     qglProgramEnvParameter4dvARB     = 0;
PFNGLPROGRAMENVPARAMETER4FARBPROC      qglProgramEnvParameter4fARB      = 0;
PFNGLPROGRAMENVPARAMETER4FVARBPROC     qglProgramEnvParameter4fvARB     = 0;
PFNGLPROGRAMLOCALPARAMETER4DARBPROC    qglProgramLocalParameter4dARB    = 0;
PFNGLPROGRAMLOCALPARAMETER4DVARBPROC   qglProgramLocalParameter4dvARB   = 0;
PFNGLPROGRAMLOCALPARAMETER4FARBPROC    qglProgramLocalParameter4fARB    = 0;
PFNGLPROGRAMLOCALPARAMETER4FVARBPROC   qglProgramLocalParameter4fvARB   = 0;
PFNGLGETPROGRAMENVPARAMETERDVARBPROC   qglGetProgramEnvParameterdvARB   = 0;
PFNGLGETPROGRAMENVPARAMETERFVARBPROC   qglGetProgramEnvParameterfvARB   = 0;
PFNGLGETPROGRAMLOCALPARAMETERDVARBPROC qglGetProgramLocalParameterdvARB = 0;
PFNGLGETPROGRAMLOCALPARAMETERFVARBPROC qglGetProgramLocalParameterfvARB = 0;
PFNGLGETPROGRAMIVARBPROC               qglGetProgramivARB               = 0;
PFNGLGETPROGRAMSTRINGARBPROC           qglGetProgramStringARB           = 0;
PFNGLISPROGRAMARBPROC                  qglIsProgramARB                  = 0;

/* NV_register_combiners */
PFNGLCOMBINERPARAMETERFVNVPROC              qglCombinerParameterfvNV              = 0;
PFNGLCOMBINERPARAMETERIVNVPROC              qglCombinerParameterivNV              = 0;
PFNGLCOMBINERPARAMETERFNVPROC               qglCombinerParameterfNV               = 0;
PFNGLCOMBINERPARAMETERINVPROC               qglCombinerParameteriNV               = 0;
PFNGLCOMBINERINPUTNVPROC                    qglCombinerInputNV                    = 0;
PFNGLCOMBINEROUTPUTNVPROC                   qglCombinerOutputNV                   = 0;
PFNGLFINALCOMBINERINPUTNVPROC               qglFinalCombinerInputNV               = 0;
PFNGLGETCOMBINERINPUTPARAMETERFVNVPROC      qglGetCombinerInputParameterfvNV      = 0;
PFNGLGETCOMBINERINPUTPARAMETERIVNVPROC      qglGetCombinerInputParameterivNV      = 0;
PFNGLGETCOMBINEROUTPUTPARAMETERFVNVPROC     qglGetCombinerOutputParameterfvNV     = 0;
PFNGLGETCOMBINEROUTPUTPARAMETERIVNVPROC     qglGetCombinerOutputParameterivNV     = 0;
PFNGLGETFINALCOMBINERINPUTPARAMETERFVNVPROC qglGetFinalCombinerInputParameterfvNV = 0;
PFNGLGETFINALCOMBINERINPUTPARAMETERIVNVPROC qglGetFinalCombinerInputParameterivNV = 0;

} /* extern "C" */

#endif /* VITA */
