/*
 * hwstart.c - how much stack HighWire gets
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * HighWire is linked with libcmini's ordinary application start-up
 * (crt0.o), not with the board package's accstart.S: accstart.S is for a
 * program that can also be a desk accessory, and it switches to a fixed
 * 4 KB stack in its own BSS.  Four kilobytes is not a browser.  Layout
 * recurses once per nested element, and a table inside a list inside a
 * frame will go deeper than that on an ordinary page.
 *
 * libcmini's crt0 takes the size from _stksize instead, and the library
 * carries a default for programs that do not care.  HighWire does care:
 * its own Makefile asks the Atari linker for 128 KB (-Wl,-stack,128k),
 * and this is the same number by the route libcmini offers.  Defining it
 * in an object that is named on the link line means it wins over the
 * library's copy.
 */

long _stksize = 128L * 1024L;
