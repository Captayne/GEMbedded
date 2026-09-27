/*
 * GEMbedClk - a clock in a GEM window, and its second half on the
 *             real-time core
 *
 * The program is in gembedclk.c, next to this file, in plain C: that is
 * what GEM programs are written in, and the Arduino IDE compiles every
 * .c file of a sketch as C.  This file, the .ino, would be compiled as
 * C++; it is left as a note so that nothing gets in the way of the
 * program's own main().
 *
 * So there is no setup() and no loop() here.  The program calls
 * appl_init(), creates its window and runs its own evnt_multi() loop,
 * exactly as it would on an Atari.  What is new is the second half:
 * irk.h starts tasks on the other core, where they keep time while GEM
 * draws.
 *
 * Press Upload with DEPLOY running on the machine.
 */
