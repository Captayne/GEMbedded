/*
 * sys/cdefs.h - let a C++ sketch reach the toolchain's own headers
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * WHY THIS FILE EXISTS
 *
 * A sketch written as a single .ino is compiled as C++, and the moment it
 * includes <math.h> the C++ library takes over: math.h pulls in <cmath>,
 * cmath pulls in <bits/std_abs.h>, and that reaches for <stdlib.h> with
 * #include_next -- which skips libcmini's and lands on the toolchain's
 * own, newlib's.
 *
 * newlib's stdlib.h then asks for <sys/cdefs.h>, and there the two worlds
 * collide.  libcmini's include directory comes first on the command line
 * (it has to: its headers must describe the library the program is
 * actually linked against), so libcmini's sys/cdefs.h answers -- and it
 * is a glibc-flavoured one.  newlib's headers are BSD-flavoured and want
 * names libcmini has never heard of, so the compilation dies on
 *
 *     stdlib.h:90: error: expected initializer before '__malloc_like'
 *
 * with nothing in the message to suggest that the cause is an include
 * path.  A sketch in a .c file never sees it, because C does not take the
 * <cmath> road at all -- which is why GEMtest built from a single .ino
 * and Fractals did not.
 *
 * WHAT IT DOES
 *
 * This directory is put before libcmini's on the include path, so this
 * file answers first.  It hands the job straight back to libcmini with
 * #include_next -- nothing about the C world changes -- and then adds the
 * handful of BSD names newlib's headers use.  Each is guarded, so if a
 * future libcmini defines them itself, its definition stands.
 *
 * libcmini is a submodule of an upstream repository; patching it there
 * would put a local change in everybody's clone.  This belongs to the
 * board package, where it can be deleted the day it is not needed.
 */

#ifndef GEMDUINO_COMPAT_SYS_CDEFS_H
#define GEMDUINO_COMPAT_SYS_CDEFS_H

/* libcmini's own, which is what the rest of the program is built on. */
#include_next <sys/cdefs.h>

/*
 * The BSD spellings, exactly as the toolchain's own sys/cdefs.h defines
 * them (arm-none-eabi/include/sys/cdefs.h, lines 54, 168, 287, 362, 391).
 */

#ifndef __XSTRING
#define __XSTRING(x)            __STRING(x)     /* expand, then stringify */
#endif

#ifndef __ASMNAME
#define __ASMNAME(cname)        __XSTRING(__USER_LABEL_PREFIX__) cname
#endif

#ifndef __malloc_like
#define __malloc_like           __attribute__((__malloc__))
#endif

#ifndef __result_use_check
#define __result_use_check      __attribute__((__warn_unused_result__))
#endif

#ifndef __pure
#define __pure                  __attribute__((__pure__))
#endif

/*
 * The allocator hints.  GCC has had all three since 4.9, and the
 * toolchain here is 14, so the attribute form always applies.
 */
#ifndef __alloc_size
#define __alloc_size(x)         __attribute__((__alloc_size__(x)))
#endif

#ifndef __alloc_size2
#define __alloc_size2(n, x)     __attribute__((__alloc_size__(n, x)))
#endif

#ifndef __alloc_align
#define __alloc_align(x)        __attribute__((__alloc_align__(x)))
#endif

#ifndef __dead2
#define __dead2                 __attribute__((__noreturn__))
#endif

/*
 * _Noreturn is a keyword in C11 and not in C++, so newlib's headers
 * expect a macro when a C++ compiler reads them.
 */
#if defined(__cplusplus) && !defined(_Noreturn)
#define _Noreturn               __attribute__((__noreturn__))
#endif

#endif /* GEMDUINO_COMPAT_SYS_CDEFS_H */
