/*
 * stdio.h - libcmini's, plus the name other headers look for
 *
 * HighWire's Location.h declares its two cache functions only when
 * <stdio.h> has been seen, and it tests for that by the guard macro:
 *
 *     #if defined(_STDIO_H) || defined(__STDIO)
 *
 * libcmini's guard is _STDIO_H_, with a trailing underscore, so the test
 * fails, the declarations never appear, and cache.c fails on two
 * functions that are right there in the header it included.
 *
 * Nothing is wrong with either file.  They simply come from different
 * C libraries, and this is the sentence that introduces them.
 */

#ifndef GEMBEDDED_STDIO_H
#define GEMBEDDED_STDIO_H

#include_next <stdio.h>

/*
 * __STDIO, not _STDIO_H.  Location.h takes either -- it tests
 *
 *     #if defined(_STDIO_H) || defined(__STDIO)
 *
 * -- and _STDIO_H turns out to be tested inside libcmini's own
 * mint/mintbind.h as well, where it brings a second struct stat into
 * view and the two collide on st_atime.  __STDIO is Pure C's spelling;
 * nothing in libcmini looks for it.
 */
#ifndef __STDIO
#define __STDIO 1
#endif

#endif /* GEMBEDDED_STDIO_H */
