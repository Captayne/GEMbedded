/*
 * irk_port_rp2350.c - IRKernel platform functions for the pTOS real-time
 *                     core (RP2350, core 1, bare metal)
 *
 * Context switching, critical sections and interrupt detection come from
 * IRKernel's own port/irk_port_cortexm.c; this adds the rest.
 */

#include <stdint.h>
#include "irk_port.h"
#include "rtcore.h"

/* TIMER0 counts microseconds (pTOS sets up its tick generator).  The
 * raw registers are read without the latch, which core 0 may be using at
 * the same time: read high, low, high again until they agree. */
#define TIMER0_TIMERAWH (*(volatile uint32_t *)0x400b0024UL)
#define TIMER0_TIMERAWL (*(volatile uint32_t *)0x400b0028UL)

void irk_port_init(void)
{
}

irk_time_t irk_port_micros(void)
{
    uint32_t hi, lo, hi2;

    do {
        hi = TIMER0_TIMERAWH;
        lo = TIMER0_TIMERAWL;
        hi2 = TIMER0_TIMERAWH;
    } while (hi != hi2);

    return ((irk_time_t)hi << 32) | lo;
}

void irk_port_idle(void)
{
    /* nothing to sleep on without a timer interrupt: poll */
}

void irk_port_error(int code, int detail)
{
    rtcore_error(code, detail);
}
