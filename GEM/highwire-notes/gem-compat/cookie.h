/*
 * cookie.h - where an Atari program expects it, forwarded to where
 * libcmini keeps it.
 *
 * Putting all of libcmini/include/mint on the include path instead is
 * one flag and looks tempting, but it also puts every other header in
 * that directory in front of the ones the program means: two different
 * struct stat then collide inside mintbind.h and the error points at a
 * #define rather than at the cause.
 */
#include <mint/cookie.h>
