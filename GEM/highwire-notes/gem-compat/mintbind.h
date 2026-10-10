/*
 * mintbind.h - where an Atari program expects it, forwarded to where
 * libcmini keeps it -- and then a collision undone.
 *
 * libcmini's mint/mintbind.h declares a struct stat with a nanosecond
 * timestamp and papers over the old field names with macros:
 *
 *     #define st_atime   st_atim.u.tv_sec
 *
 * Its own sys/stat.h and ext.h declare `long st_atime;` as an ordinary
 * member.  Whichever is read second is then rewritten by the macro and
 * the compiler reports a syntax error inside the #define, which points
 * nowhere near the cause.  HighWire includes both, through file_sys.h.
 *
 * Undoing the macros here leaves both headers usable: a program that
 * reads st_atime of an ordinary struct stat gets the member, and one
 * that wants the nanosecond form can say st_atim.u.tv_sec itself.
 */

#include <mint/mintbind.h>

#undef st_atime
#undef st_mtime
#undef st_ctime
