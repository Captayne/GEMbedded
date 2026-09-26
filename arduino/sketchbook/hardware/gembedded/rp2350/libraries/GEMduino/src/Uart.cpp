/*
 * Uart.cpp - UART1 of the RP2350, driven by polling
 *
 * Copyright (C) 2026 Andreas Keibel
 */

#include "GEMduino.h"
#include "Uart.h"

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
#define RESETS_CLR      REG(RESETS_BASE + 0x3000)    /* atomic clear alias */
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

Uart Serial1;

void Uart::begin(unsigned long baud)
{
    unsigned long baud16 = baud * 16;
    unsigned long intdiv = CLK_PERI_HZ / baud16;
    unsigned long frac2 = (CLK_PERI_HZ % baud16) * 8 / baud;
    unsigned long frac = frac2 / 2 + frac2 % 2;

    /* pTOS only takes UART0 out of reset; this one is still asleep */
    RESETS_CLR = RESET_UART1;
    while (!(RESETS_DONE & RESET_UART1))
        ;

    UART1_CR = 0;
    UART1_IMSC = 0;                     /* no interrupts: we poll */
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
}

void Uart::end(void)
{
    UART1_CR = 0;
}

int Uart::available(void)
{
    return (UART1_FR & FR_RXFE) ? 0 : 1;
}

int Uart::read(void)
{
    if (UART1_FR & FR_RXFE)
        return -1;
    return (int)(UART1_DR & 0xff);
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
    while (!(UART1_FR & FR_RXFE))
        (void)UART1_DR;
}

/*
 * A line, with a deadline.  The wait is the interesting part: rather than
 * spinning, it asks the AES for a millisecond at a time, so the desktop
 * and everything else keep running while a module thinks.
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
                return (n > 0) ? n : -1;
            delay(1);                   /* give way while nothing happens */
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
