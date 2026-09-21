/*
 * boot.c - core 0 enters pTOS; core 1 starts when pTOS asks
 *
 * boot_core0() runs once, from SRAM, on the small boot stack in crt0.S.
 * kapi_start_core1() is start_core1 in the kernel's call table (kapi.c).
 */

#include <stdint.h>
#include "rtx_rp2350.h"
#include "kapi.h"

#define SIO_FIFO_ST     (*(volatile uint32_t *)0xd0000050UL)
#define SIO_FIFO_WR     (*(volatile uint32_t *)0xd0000054UL)
#define SIO_FIFO_RD     (*(volatile uint32_t *)0xd0000058UL)
#define SIO_FIFO_VLD    0x1UL
#define SIO_FIFO_RDY    0x2UL

extern uint32_t vectors[];
extern uint32_t __stack_top[];
void core1_entry(void);
void boot_core0(void);

/* One word out, its echo back.  Bounded: core 1 may not be listening
   yet this early, and a boot that waits for ever shows nothing at all. */
static int fifo_exchange(uint32_t value, uint32_t *echo)
{
    uint32_t n;

    for (n = 0; !(SIO_FIFO_ST & SIO_FIFO_RDY); n++)
        if (n > 100000u)
            return 0;
    SIO_FIFO_WR = value;
    __asm__ volatile ("sev");
    for (n = 0; !(SIO_FIFO_ST & SIO_FIFO_VLD); n++)
        if (n > 100000u)
            return 0;
    *echo = SIO_FIFO_RD;
    return 1;
}

/*
 * The bootrom's protocol (RP2350 datasheet, 5.3 "Launching code on
 * processor core 1"): core 1 waits in the bootrom for the sequence 0, 0,
 * 1, vector table, stack pointer, entry point on the inter-core FIFO,
 * echoing every word; on a mismatch start over.  Gives up after a
 * number of rounds and says so: pTOS then runs without core 1.
 */
static int launch_core1(uint32_t vtor, uint32_t sp, uint32_t entry)
{
    uint32_t seq[6], echo;
    int i = 0, rounds = 0;

    seq[0] = 0;
    seq[1] = 0;
    seq[2] = 1;
    seq[3] = vtor;
    seq[4] = sp;
    seq[5] = entry;

    while (i < 6)
    {
        if (seq[i] == 0)
        {
            while (SIO_FIFO_ST & SIO_FIFO_VLD)  /* drain stale words */
                (void)SIO_FIFO_RD;
            __asm__ volatile ("sev");
        }
        if (fifo_exchange(seq[i], &echo) && echo == seq[i])
            i++;
        else
        {
            i = 0;
            if (++rounds > 50)
                return 0;
        }
    }
    return 1;
}

/*
 * Core 1 is started when pTOS asks, not at boot.  Started any earlier,
 * while pTOS is still coming up, the machine stops before the screen
 * shows anything -- even with core 1 doing nothing but sleep.  Why is
 * not yet understood; until the kernel sets up the clocks itself, pTOS
 * says when.
 */
long kapi_start_core1(void)
{
    return launch_core1((uint32_t)vectors, (uint32_t)__stack_top,
                        (uint32_t)core1_entry | 1u) ? 0 : -1;
}

void boot_core0(void)
{
    const uint32_t *ptos = (const uint32_t *)PTOS_IMAGE_ADDR;

    /* Into pTOS as if the bootrom had started it: its own main stack,
       its reset handler.  It sets VTOR, CONTROL and the rest itself.
       The stack limits go first: the bootrom leaves MSPLIM guarding the
       stack it gave us, and pTOS's stack lies far below that. */
    __asm__ volatile (
        "msr    msplim, %2 \n"
        "msr    psplim, %2 \n"
        "msr    msp, %0 \n"
        "bx     %1      \n"
        : : "r" (ptos[0]), "r" (ptos[1] | 1u), "r" (0u) : "memory");
    for (;;)
        ;
}
