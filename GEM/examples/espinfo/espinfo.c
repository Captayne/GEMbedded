/*
 * espinfo.c - ask the attached AT module what it is
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Before a network module can be written against it, three things have
 * to be known, and only the module can say them:
 *
 *   AT+GMR          which firmware. That decides everything below it.
 *   AT+CIPRECVMODE? whether data can be fetched when we ask for it,
 *                   rather than arriving in the middle of the reply to
 *                   something else. With it, reading a socket is a
 *                   command; without it, +IPD has to be picked out of
 *                   the character stream as it goes past, which is a
 *                   different and far more fragile program.
 *   AT+CIPSTATUS    whether it is on a network at all.
 *
 * This prints what comes back, verbatim, and nothing else: a transcript
 * to read, not a verdict to trust.
 *
 * The sketch in GEMduino/sketchbook/WifiTime talks to the same module
 * through the EspAt class and shows the same conversation in a window.
 * This exists beside it because it builds from a Makefile rather than
 * from the IDE, which is what lets it be deployed in one step.
 */

#include <stdio.h>
#include <string.h>

#include <osbind.h>
#include <mint/mintbind.h>      /* Ssystem() and S_GETCOOKIE */

#include "uart1.h"

static struct ua1_api *ua1;

/*
 * Print whatever the module says until it has been quiet for a while.
 *
 * By idling rather than by the clock: a module answers in bursts with
 * gaps inside them, and the end of an answer is silence, not a word --
 * "OK" and "ERROR" both end one, but so does a firmware banner that
 * ends in neither.
 */
static void drain(void)
{
    char buf[128];
    long idle = 0;

    while (idle < 300000L)
    {
        long n = ua1->read(buf, (long)sizeof(buf) - 1);

        if (n > 0)
        {
            buf[n] = '\0';
            fputs(buf, stdout);
            idle = 0;
        }
        else
            idle++;
    }
}

static void ask(const char *cmd)
{
    printf("\r\n>>> %s\r\n", cmd);
    ua1->write(cmd, (long)strlen(cmd));
    ua1->write("\r\n", 2);
    drain();
}

int main(void)
{
    long value = 0;

    if (Ssystem(S_GETCOOKIE, UA1_COOKIE, (long)&value) != 0 || !value)
    {
        printf("No _UA1 cookie: this machine has no second UART.\r\n");
        (void)Cconin();
        return 1;
    }
    ua1 = (struct ua1_api *)value;

    /*
     * 115200, because that is what the module is set to today -- it is
     * what the clock asks of it. If nothing comes back at all, that is
     * the first thing to doubt rather than the module: one that is still
     * at its factory speed answers nothing here and is not broken.
     */
    if (ua1->open(115200L) < 0)
    {
        printf("Cannot open UART1.\r\n");
        (void)Cconin();
        return 1;
    }

    ua1->flush();

    ask("AT");                  /* is anybody there */
    ask("AT+GMR");              /* which firmware */
    ask("AT+CIPRECVMODE?");     /* can data be fetched on request */
    ask("AT+CIPMUX?");          /* one connection, or several */
    ask("AT+CWMODE?");          /* station, access point, or both */
    ask("AT+CIPSTATUS");        /* on a network? */

    printf("\r\nlost: %ld\r\n", ua1->lost());
    printf("-- press a key --\r\n");
    (void)Cconin();

    ua1->close();
    return 0;
}
