/*
 * irk_port_rp2350.c - IRKernel platform functions for the pTOS real-time
 *                     core (RP2350, core 1, bare metal)
 *
 * Context switching, critical sections and interrupt detection come from
 * IRKernel's own port/irk_port_cortexm.c; this adds the rest, including
 * what two cores sharing one kernel need: which core is asking, and a
 * lock between them.
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

/* SIO CPUID: 0 on core 0, 1 on core 1 */
#define SIO_CPUID       (*(volatile uint32_t *)0xd0000000UL)

uint8_t irk_port_core_id(void)
{
    return (uint8_t)SIO_CPUID;
}

/*
 * The lock between the cores.  Not the SIO spinlocks: erratum RP2350-E2
 * lets writes to neighbouring SIO registers release them, which is why
 * the Pico SDK does not use them on this chip either.  An exclusive
 * load/store on a word in SRAM does the same job.
 *
 * Nestable per core, and with interrupts off while held: an interrupt
 * on the same core that asked for the lock again would otherwise walk
 * into the middle of a change.
 */
static volatile uint32_t xlock_word;
static uint32_t xlock_depth[2];
static uint32_t xlock_primask[2];

void irk_port_xlock(void)
{
    uint32_t pm, c, fail;

    __asm__ volatile ("mrs %0, primask" : "=r" (pm));
    __asm__ volatile ("cpsid i" ::: "memory");
    c = SIO_CPUID;
    if (xlock_depth[c]++ != 0)
        return;
    xlock_primask[c] = pm;

    do {
        __asm__ volatile (
            "1: ldaex   %0, [%1]    \n"
            "   cmp     %0, #0      \n"
            "   bne     1b          \n"
            "   strex   %0, %2, [%1]\n"
            : "=&r" (fail) : "r" (&xlock_word), "r" (1u) : "memory", "cc");
    } while (fail);
    __asm__ volatile ("dmb" ::: "memory");
}

void irk_port_xunlock(void)
{
    uint32_t c = SIO_CPUID;

    if (xlock_depth[c] == 0 || --xlock_depth[c] != 0)
        return;
    __asm__ volatile ("dmb" ::: "memory");
    xlock_word = 0;
    if (xlock_primask[c] == 0)
        __asm__ volatile ("cpsie i" ::: "memory");
}

void irk_port_error(int code, int detail)
{
    rtcore_error(code, detail);
}
