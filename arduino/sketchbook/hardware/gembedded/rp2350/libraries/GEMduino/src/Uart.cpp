/*
 * Uart.cpp - UART1 of the RP2350, with the real-time core as its reader
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * The port itself is simple: a handful of registers, no interrupts -- a
 * program on this machine cannot take one.  What is not simple is being
 * sure of every character while a desktop, a screen manager and other
 * programs share the processor.
 *
 * At 115200 baud a character arrives every 87 us and the hardware holds
 * 32 of them: 2.8 milliseconds of grace.  A GEM program that politely
 * gives the processor away gets it back after the next system tick, some
 * 20 milliseconds later, and by then the port has thrown away what it
 * could not keep.  That is not a theory; it is what the log of the first
 * try looked like, with holes in the middle of lines.
 *
 * So the reading is done by a cyclic task on the real-time core, every
 * 200 us, which never sleeps and never waits for anything: it empties
 * the port into a ring buffer.  The GEM half reads lines out of the ring
 * at its leisure and may sleep as much as it likes.  This is the same
 * division of labour as the clock example, only here it is a necessity
 * rather than a demonstration.
 *
 * Without the kernel -- no "_IRK" cookie -- the port still works: then
 * the reading happens in whatever process asks, and one must not sleep
 * in the middle of an answer.
 */

#include <string.h>
#include "GEMduino.h"
#include "Uart.h"
#include "irk.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>

#define REG(a)          (*(volatile unsigned long *)(a))

#define UART1_BASE      0x40078000UL
#define UART1_DR        REG(UART1_BASE + 0x00)
#define UART1_FR        REG(UART1_BASE + 0x18)
#define UART1_IBRD      REG(UART1_BASE + 0x24)
#define UART1_FBRD      REG(UART1_BASE + 0x28)
#define UART1_LCRH      REG(UART1_BASE + 0x2c)
#define UART1_CR        REG(UART1_BASE + 0x30)
#define UART1_IMSC      REG(UART1_BASE + 0x38)
#define UART1_ICR       REG(UART1_BASE + 0x44)

#define FR_RXFE         0x10UL          /* receive FIFO empty */
#define FR_TXFF         0x20UL          /* transmit FIFO full */

#define RESETS_BASE     0x40020000UL
#define RESETS_CLR      REG(RESETS_BASE + 0x3000)    /* the atomic-clear alias */
#define RESETS_DONE     REG(RESETS_BASE + 0x08)
#define RESET_UART1     (1UL << 27)

#define IO_BANK0_BASE   0x40028000UL
#define PADS_BANK0_BASE 0x40038000UL
#define GPIO_CTRL(n)    REG(IO_BANK0_BASE + 0x04 + 8 * (n))
#define PAD(n)          REG(PADS_BANK0_BASE + 0x04 + 4 * (n))
#define PAD_ISO         0x100UL
#define PAD_OD          0x080UL
#define PAD_IE          0x040UL
#define FUNC_UART       2

#define TX_PIN          4
#define RX_PIN          5

#define CLK_PERI_HZ     150000000UL     /* what pTOS sets up */

/*
 * The ring the two cores share.  One writer (the task on core 1), one
 * reader (this program): no lock is needed, only the order of the two
 * writes -- the character first, the index afterwards.
 */
#define RING_SIZE       2048
static volatile unsigned char ring[RING_SIZE];
static volatile unsigned short ring_head;   /* written by core 1 */
static volatile unsigned short ring_tail;   /* written by us */

/* Runs on the real-time core, every 200 us: empty the port. */
static void uart_feeder(void *arg)
{
    (void)arg;

    while (!(UART1_FR & FR_RXFE))
    {
        unsigned short next = (unsigned short)((ring_head + 1) % RING_SIZE);
        unsigned char c = (unsigned char)(UART1_DR & 0xff);

        if (next == ring_tail)
            break;                      /* full: leave it in the port */
        ring[ring_head] = c;
        __asm__ volatile ("dmb" ::: "memory");
        ring_head = next;
    }
}

Uart Serial1;

void Uart::begin(unsigned long baud)
{
    unsigned long baud16 = baud * 16;
    unsigned long intdiv = CLK_PERI_HZ / baud16;
    unsigned long frac2 = (CLK_PERI_HZ % baud16) * 8 / baud;
    unsigned long frac = frac2 / 2 + frac2 % 2;
    long value = 0;

    /* pTOS only takes UART0 out of reset; this one is still asleep */
    RESETS_CLR = RESET_UART1;
    while (!(RESETS_DONE & RESET_UART1))
        ;

    UART1_CR = 0;
    UART1_IMSC = 0;                     /* no interrupts: we are polled */
    UART1_ICR = 0x7ff;
    UART1_IBRD = intdiv;
    UART1_FBRD = frac;
    UART1_LCRH = (3 << 5) | (1 << 4);   /* 8 bits, no parity, FIFOs on */
    UART1_CR = 0x301;                   /* UARTEN | TXE | RXE */

    /* the two pins: out of their reset latch, then handed to the UART */
    PAD(TX_PIN) = (PAD(TX_PIN) & ~(PAD_ISO | PAD_OD)) | PAD_IE;
    PAD(RX_PIN) = (PAD(RX_PIN) & ~(PAD_ISO | PAD_OD)) | PAD_IE;
    GPIO_CTRL(TX_PIN) = FUNC_UART;
    GPIO_CTRL(RX_PIN) = FUNC_UART;

    ring_head = ring_tail = 0;

    /* and the reader on the other core, if there is one */
    if (Ssystem(S_GETCOOKIE, IRK_COOKIE, (long)&value) == 0 && value)
        k = (struct irk_api *)value;
    if (k && k->cores() > 1)
    {
        feeder = k->task_new(IRK_CORE_RT, uart_feeder, 0, 200, 0, 1024, 0);
        if (feeder != IRK_NONE)
        {
            k->set_cyclic(feeder, 200, 0);   /* every 200 us */
            k->task_resume(feeder);
        }
        else
            k = 0;
    }
    else
        k = 0;
}

void Uart::end(void)
{
    if (k && feeder != IRK_NONE)
    {
        k->task_kill(feeder);
        feeder = IRK_NONE;
    }
    UART1_CR = 0;
}

int Uart::available(void)
{
    if (!k)
        return (UART1_FR & FR_RXFE) ? 0 : 1;
    return ring_head != ring_tail;
}

int Uart::read(void)
{
    if (!k)
    {
        if (UART1_FR & FR_RXFE)
            return -1;
        return (int)(UART1_DR & 0xff);
    }

    if (ring_head == ring_tail)
        return -1;
    {
        unsigned char c = ring[ring_tail];

        ring_tail = (unsigned short)((ring_tail + 1) % RING_SIZE);
        return (int)c;
    }
}

void Uart::write(unsigned char c)
{
    while (UART1_FR & FR_TXFF)
        ;
    UART1_DR = c;
}

void Uart::write(const char *s)
{
    while (*s)
        write((unsigned char)*s++);
}

void Uart::write(const void *data, unsigned long len)
{
    const unsigned char *p = (const unsigned char *)data;

    while (len--)
        write(*p++);
}

void Uart::flushInput(void)
{
    while (read() >= 0)
        ;
}

/*
 * A line, with a deadline.  Whether it is safe to sleep while waiting
 * depends on who is reading the port: with the task on the other core,
 * nothing is lost while this process sleeps.  Without it, sleeping would
 * cost characters, so it spins instead.
 */
int Uart::readLine(char *buf, int size, unsigned long timeout_ms)
{
    unsigned long start = millis();
    int n = 0;

    for (;;)
    {
        int c = read();

        if (c < 0)
        {
            if (millis() - start >= timeout_ms)
            {
                buf[n] = '\0';          /* always terminated, even half a line */
                return (n > 0) ? n : -1;
            }
            if (k)
                delay(1);               /* the other core keeps listening */
            continue;
        }
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            buf[n] = '\0';
            return n;
        }
        if (n < size - 1)
            buf[n++] = (char)c;
    }
}
