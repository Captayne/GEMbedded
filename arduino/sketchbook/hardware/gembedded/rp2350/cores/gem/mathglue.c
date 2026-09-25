/*
 * mathglue.c - what newlib's maths needs and libcmini does not provide
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * The toolchain already ships a complete libm -- the same one the
 * Arduino cores use on ARM -- built for exactly the flags this tree
 * uses.  There is no reason to hand-carve a sine when sqrtf, atan2f and
 * the rest are sitting there already.
 *
 * It wants one thing libcmini has no notion of: the place to put an
 * error number.  A single static one does here, because a program on
 * this machine has one thread of its own and the real-time half must
 * not call the maths library anyway -- it has no C library at all.
 *
 * Link with -lm, after -lcmini.
 */

#include "mathglue.h"

int *__errno(void);

int *__errno(void)
{
    static int e;

    return &e;
}

short fround(float v)
{
    return (short)(v < 0.0f ? v - 0.5f : v + 0.5f);
}
