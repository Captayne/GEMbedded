/*
 * GEMtest - does what the bindings say they do?
 *
 * The program is in gemtest.c, next to this file, in plain C: that is
 * what GEM programs are written in, and the Arduino IDE compiles every
 * .c file of a sketch as C.  This file, the .ino, would be compiled as
 * C++; it is left as a note so that nothing gets in the way of the
 * program's own main().
 *
 * The AES and VDI bindings in the board's core are hand-written, and each
 * one has to put its arguments in the slot the operating system reads
 * them from.  Get a slot wrong and nothing complains: the call returns,
 * something is drawn, and it is the wrong thing -- or the right thing by
 * accident, which is worse.  So this program asks.
 *
 * The first page checks what can be checked by arithmetic and prints
 * expected against actual.  The pages after it draw things whose shape
 * says whether they worked: a circle with the radius taken from the wrong
 * slot is not a circle of the wrong size, it is missing.
 *
 * Tap the window to turn the page.  Press Upload with DEPLOY running.
 */
