/*
 * GEMduino.cpp - time and pins
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * The registers are the ones in the RP2350 datasheet, and a program may
 * reach them: pTOS leaves the memory protection unit out of the picture,
 * so a program is unprivileged but not walled in.  That is a deliberate
 * choice of the port, not an accident -- and it is what lets an Arduino
 * library work here without asking the operating system for anything.
 */

#include "GEMduino.h"
#include "gem.h"

#define REG(a)          (*(volatile unsigned long *)(a))

#define TIMER0_BASE     0x400b0000UL
#define TIMER0_RAWH     REG(TIMER0_BASE + 0x24)
#define TIMER0_RAWL     REG(TIMER0_BASE + 0x28)

#define IO_BANK0_BASE   0x40028000UL
#define PADS_BANK0_BASE 0x40038000UL
#define SIO_BASE        0xd0000000UL

#define GPIO_CTRL(n)    REG(IO_BANK0_BASE + 0x04 + 8 * (n))
#define PAD(n)          REG(PADS_BANK0_BASE + 0x04 + 4 * (n))

#define PAD_ISO         0x100UL         /* the latch that holds a fresh pin */
#define PAD_OD          0x080UL         /* output disable */
#define PAD_IE          0x040UL         /* input enable */
#define PAD_PUE         0x008UL
#define PAD_PDE         0x004UL

#define FUNC_SIO        5

/* GPIO 0..31 and 32..47 have their own set of SIO registers */
#define SIO_OUT_SET(n)  REG(SIO_BASE + ((n) < 32 ? 0x18 : 0x1c))
#define SIO_OUT_CLR(n)  REG(SIO_BASE + ((n) < 32 ? 0x20 : 0x24))
#define SIO_OE_SET(n)   REG(SIO_BASE + ((n) < 32 ? 0x38 : 0x3c))
#define SIO_OE_CLR(n)   REG(SIO_BASE + ((n) < 32 ? 0x40 : 0x44))
#define SIO_IN(n)       REG(SIO_BASE + ((n) < 32 ? 0x04 : 0x08))
#define BIT(n)          (1UL << ((n) & 31))


/* ---- time ---- */

/*
 * TIMER0 counts microseconds and pTOS keeps it running.  Read the high
 * word, the low word and the high word again: without the latched
 * registers -- which the system may be using at that moment -- that is
 * how one reads a counter that ticks between the two halves.
 */
unsigned long micros(void)
{
    unsigned long hi, lo, hi2;

    do {
        hi = TIMER0_RAWH;
        lo = TIMER0_RAWL;
        hi2 = TIMER0_RAWH;
    } while (hi != hi2);

    (void)hi;
    return lo;                  /* wraps every 71 minutes, as on Arduino */
}

unsigned long millis(void)
{
    return micros() / 1000UL;
}

void delay(unsigned long ms)
{
    /* The AES timer: this process sleeps and the others run.  Under a
       millisecond there is nothing to sleep on, so spin instead. */
    if (ms == 0)
        return;
    evnt_timer(ms);
}

void delayMicroseconds(unsigned long us)
{
    unsigned long start = micros();

    while (micros() - start < us)
        ;
}


/* ---- pins ---- */

void pinMode(unsigned pin, int mode)
{
    unsigned long pad = PAD(pin);

    pad &= ~(PAD_ISO | PAD_OD | PAD_PUE | PAD_PDE);
    pad |= PAD_IE;
    if (mode == INPUT_PULLUP)
        pad |= PAD_PUE;
    else if (mode == INPUT_PULLDOWN)
        pad |= PAD_PDE;
    PAD(pin) = pad;

    GPIO_CTRL(pin) = FUNC_SIO;

    if (mode == OUTPUT)
        SIO_OE_SET(pin) = BIT(pin);
    else
        SIO_OE_CLR(pin) = BIT(pin);
}

void digitalWrite(unsigned pin, int value)
{
    if (value)
        SIO_OUT_SET(pin) = BIT(pin);
    else
        SIO_OUT_CLR(pin) = BIT(pin);
}

int digitalRead(unsigned pin)
{
    return (SIO_IN(pin) & BIT(pin)) ? HIGH : LOW;
}
