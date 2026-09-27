/*
 * Uart.cpp - the second serial port, by whichever means is available
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Three ways, and it takes the best one it finds:
 *
 *   1. The system driver (the "_UA1" cookie).  The interrupt fills a
 *      buffer inside pTOS, so a program may take its time and miss
 *      nothing.  This is the right way, and the one to use.
 *
 *   2. A cyclic task on the real-time core, which empties the port into a
 *      ring of ours every 200 us.  For a system without that driver.
 *      It works, but it spends a task on something the interrupt should
 *      be doing.
 *
 *   3. The registers, read by whoever asks.  Then one must not give the
 *      processor away in the middle of an answer: at 115200 baud the
 *      hardware holds 2.8 milliseconds of characters, and a GEM process
 *      that sleeps is gone for a system tick -- twenty.  That is how the
 *      first attempt lost the middle of every reply.
 */

#include <string.h>
#include "GEMduino.h"
#include "Uart.h"
#include "irk.h"
#include "uart1.h"
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

#define FR_RXFE         0x10UL
#define FR_TXFF         0x20UL

#define RESETS_BASE     0x40020000UL
#define RESETS_CLR      REG(RESETS_BASE + 0x3000)
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

#define CLK_PERI_HZ     150000000UL

/* the ring the task on the other core fills (way 2) */
#define RING_SIZE       2048
static volatile unsigned char ring[RING_SIZE];
static volatile unsigned short ring_head;   /* the other core writes this */
static volatile unsigned short ring_tail;   /* and we write this */

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

/* the port itself, for ways 2 and 3 */
static void raw_setup(unsigned long baud)
{
    unsigned long baud16 = baud * 16;
    unsigned long intdiv = CLK_PERI_HZ / baud16;
    unsigned long frac2 = (CLK_PERI_HZ % baud16) * 8 / baud;
    unsigned long frac = frac2 / 2 + frac2 % 2;

    RESETS_CLR = RESET_UART1;           /* pTOS only wakes UART0 */
    while (!(RESETS_DONE & RESET_UART1))
        ;

    UART1_CR = 0;
    UART1_IMSC = 0;
    UART1_ICR = 0x7ff;
    UART1_IBRD = intdiv;
    UART1_FBRD = frac;
    UART1_LCRH = (3 << 5) | (1 << 4);   /* 8 bits, no parity, FIFOs on */
    UART1_CR = 0x301;                   /* UARTEN | TXE | RXE */

    PAD(TX_PIN) = (PAD(TX_PIN) & ~(PAD_ISO | PAD_OD)) | PAD_IE;
    PAD(RX_PIN) = (PAD(RX_PIN) & ~(PAD_ISO | PAD_OD)) | PAD_IE;
    GPIO_CTRL(TX_PIN) = FUNC_UART;
    GPIO_CTRL(RX_PIN) = FUNC_UART;
}

void Uart::begin(unsigned long baud)
{
    long value = 0;

    /* 1: the system's own driver */
    if (Ssystem(S_GETCOOKIE, UA1_COOKIE, (long)&value) == 0 && value)
    {
        drv = (struct ua1_api *)value;
        if (drv->version >= UA1_VERSION && drv->open(baud) == 0)
            return;
        drv = 0;
    }

    /* 2 and 3 need the port set up here */
    raw_setup(baud);
    ring_head = ring_tail = 0;

    value = 0;
    if (Ssystem(S_GETCOOKIE, IRK_COOKIE, (long)&value) == 0 && value)
        k = (struct irk_api *)value;
    if (k && k->cores() > 1)
    {
        feeder = k->task_new(IRK_CORE_RT, uart_feeder, 0, 200, 0, 1024, 0);
        if (feeder != IRK_NONE)
        {
            k->set_cyclic(feeder, 200, 0);      /* every 200 us */
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
    if (drv)
    {
        drv->close();
        drv = 0;
        return;
    }
    if (k && feeder != IRK_NONE)
    {
        k->task_kill(feeder);
        feeder = IRK_NONE;
    }
    UART1_CR = 0;
}

int Uart::available(void)
{
    if (drv)
        return drv->status() > 0;
    if (k)
        return ring_head != ring_tail;
    return (UART1_FR & FR_RXFE) ? 0 : 1;
}

int Uart::read(void)
{
    if (drv)
    {
        unsigned char c;

        return (drv->read(&c, 1) == 1) ? (int)c : -1;
    }
    if (k)
    {
        unsigned char c;

        if (ring_head == ring_tail)
            return -1;
        c = ring[ring_tail];
        ring_tail = (unsigned short)((ring_tail + 1) % RING_SIZE);
        return (int)c;
    }
    if (UART1_FR & FR_RXFE)
        return -1;
    return (int)(UART1_DR & 0xff);
}

void Uart::write(unsigned char c)
{
    if (drv)
    {
        drv->write(&c, 1);
        return;
    }
    while (UART1_FR & FR_TXFF)
        ;
    UART1_DR = c;
}

void Uart::write(const char *s)
{
    write(s, (unsigned long)strlen(s));
}

void Uart::write(const void *data, unsigned long len)
{
    const unsigned char *p = (const unsigned char *)data;

    if (drv)
    {
        drv->write(data, (long)len);
        return;
    }
    while (len--)
        write(*p++);
}

void Uart::flushInput(void)
{
    if (drv)
    {
        drv->flush();
        return;
    }
    while (read() >= 0)
        ;
}

/* Characters that arrived with nowhere to go -- only the driver counts them. */
long Uart::lost(void)
{
    return drv ? drv->lost() : 0;
}

/*
 * A line, with a deadline.  Whether it is safe to sleep while waiting
 * depends on who is doing the listening: with the driver or with the task
 * on the other core, nothing is lost while this process sleeps.  Reading
 * the registers ourselves, sleeping would cost characters, so it spins.
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
                buf[n] = '\0';          /* terminated even when cut short */
                return (n > 0) ? n : -1;
            }
            if (drv || k)
                delay(1);               /* somebody else is listening */
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
