/*
 * Uart.h - the second serial port (UART1, GPIO 4 and 5)
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * UART0 belongs to pTOS: it is the console that prints while the machine
 * starts.  UART1 is free, and GPIO 4 (out) and 5 (in) carry it on the
 * header -- the pins a radio module wants.
 *
 * The port is driven by polling, without interrupts: a program cannot
 * take an interrupt on this machine, and it does not need to.  At 115200
 * baud a character takes 87 us and the hardware holds 32 of them, so
 * anything that comes back for a look every few milliseconds keeps up.
 * What matters is not to wait: readLine() has a deadline, and while it
 * waits it gives the processor away.
 */

#ifndef GEMDUINO_UART_H
#define GEMDUINO_UART_H

class Uart
{
public:
    /* Wakes UART1, sets the baud rate and hands GPIO 4/5 to it. */
    void begin(unsigned long baud = 115200);
    void end(void);

    int  available(void);           /* is there a character? */
    int  read(void);                /* -1 when there is none */
    void write(unsigned char c);
    void write(const char *s);      /* until the NUL */
    void write(const void *data, unsigned long len);

    /* Everything that is there, thrown away. */
    void flushInput(void);

    /*
     * A line, without its CR/LF, into buf.  Returns its length, 0 for an
     * empty line, or -1 when nothing arrived before the deadline.  While
     * it waits, other programs run.
     */
    int  readLine(char *buf, int size, unsigned long timeout_ms);
};

extern Uart Serial1;

#endif /* GEMDUINO_UART_H */
