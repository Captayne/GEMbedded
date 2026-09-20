/*
 * mathglue.h - use <math.h> in a pTOS program
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Include <math.h> as usual and link with -lm after -lcmini, together
 * with mathglue.o.  Both Cortex-M33 cores have a floating point unit and
 * the tree is built to use it (-mfloat-abi=softfp -mfpu=fpv5-sp-d16),
 * so this is real arithmetic, not emulation.
 */

#ifndef MATHGLUE_H
#define MATHGLUE_H

/* To the nearest whole number, not towards zero -- truncating a
   coordinate moves everything half a pixel towards the origin.  libm has
   lroundf(), but this returns the short that the VDI wants. */
short fround(float v);

#endif /* MATHGLUE_H */
