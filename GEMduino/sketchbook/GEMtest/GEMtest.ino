/*
 * gemtest.c - GEMtest: exercise the AES and VDI bindings and say what
 *             they did
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Every binding has to put its arguments where the operating system looks
 * for them, and a wrong slot is silent: the call returns, something
 * happens, and it is not what was asked for.  This program asks.
 *
 * Page 0 checks what arithmetic can check and prints expected against
 * actual.  The other pages draw things whose shape is the answer -- an
 * arc whose radius came from the wrong slot does not come out the wrong
 * size, it does not come out at all.
 */
