/*
 * GEMduino.h - the Arduino way of touching hardware, on a GEM machine
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * A sketch here is a GEM program: it has its own main() and its own
 * event loop, and it draws with the VDI.  What it did not have so far is
 * the other half of the Arduino world -- time, pins, a second serial
 * port -- and that is what this is.
 *
 * Two things work differently from a bare Arduino, and both on purpose:
 *
 *   delay() gives the processor away.  On a machine with a desktop and
 *   other programs, burning a millisecond is rude; the kernel hands it
 *   to somebody else and comes back.  A busy wait is still available as
 *   delayMicroseconds(), for the short, exact waits that bit-banged
 *   protocols need.
 *
 *   Pins are not all yours.  The display, the touch screen, the SD card
 *   and the console have theirs (see PINS_IN_USE below); GPIO 4 and 5
 *   are the free pair that carries the second serial port.
 */

#ifndef GEMDUINO_H
#define GEMDUINO_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- time ---- */

unsigned long millis(void);
unsigned long micros(void);

/* Waits, and lets everything else run meanwhile (the AES timer). */
void delay(unsigned long ms);

/* Waits without giving anything away: for short, exact waits only. */
void delayMicroseconds(unsigned long us);

/* ---- pins ---- */
/*
 * In use on the Waveshare RP2350-PiZero with the 2.8" display:
 *   0, 1              the console UART of pTOS
 *   6, 7, 8, 9, 20    touch screen
 *   10, 11, 21, 23-25 display
 *   30, 31, 40, 43    SD card
 * Free and brought out on the header: 2, 3, 4, 5, and 12 upwards.
 */
#define PINS_IN_USE     "0,1,6,7,8,9,10,11,20,21,23,24,25,30,31,40,43"

#define INPUT           0
#define OUTPUT          1
#define INPUT_PULLUP    2
#define INPUT_PULLDOWN  3

#define LOW             0
#define HIGH            1

void pinMode(unsigned pin, int mode);
void digitalWrite(unsigned pin, int value);
int  digitalRead(unsigned pin);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include "Uart.h"
#include "EspAt.h"

#endif

#endif /* GEMDUINO_H */
