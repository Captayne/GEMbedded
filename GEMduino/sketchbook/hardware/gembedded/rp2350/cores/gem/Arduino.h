/*
 * Arduino.h - what the IDE puts at the top of every sketch
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * On this board it does not lead to pinMode() and digitalWrite(), but to
 * GEM: the AES and the VDI (gem.h) and the other core (irk.h).  The name
 * is the IDE's convention -- it adds the line itself, whether a sketch
 * asks for it or not -- so this is where our world begins.
 *
 * A sketch is an ordinary GEM program with its own main(): there is no
 * setup() and no loop() behind this header, and nothing is started for
 * you.  See File > Examples > GEM > Window, or docs/gemduino.md.
 */

#ifndef ARDUINO_H
#define ARDUINO_H

#include "gem.h"        /* AES and VDI */
#include "irk.h"        /* cyclic tasks on the real-time core */
#include "mathglue.h"   /* fround(), and what libm needs from us */

#endif /* ARDUINO_H */
