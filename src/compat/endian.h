/*
 * JK2VITA compatibility <endian.h>.
 *
 * We build with -D__linux__ so OpenJK and SDL take the unix code paths, but
 * VitaSDK's newlib ships only <machine/endian.h>, not glibc's <endian.h>.
 * Code such as SDL_endian.h does `#ifdef __linux__ #include <endian.h>` and
 * then uses __BYTE_ORDER. This shim provides the glibc-style byte-order macros
 * (the Vita is always armv7 little-endian) on top of newlib's machine header.
 */
#ifndef JK2VITA_COMPAT_ENDIAN_H
#define JK2VITA_COMPAT_ENDIAN_H

#include <machine/endian.h>

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif
#ifndef __PDP_ENDIAN
#define __PDP_ENDIAN 3412
#endif
#ifndef __BYTE_ORDER
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN __LITTLE_ENDIAN
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN __BIG_ENDIAN
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER __BYTE_ORDER
#endif

#endif /* JK2VITA_COMPAT_ENDIAN_H */
