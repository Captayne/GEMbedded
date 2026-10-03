/*
 * EspAt.cpp - talking to an AT module
 *
 * Copyright (C) 2026 Andreas Keibel
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>              /* sprintf, for the AT commands that carry a number */
#include "GEMduino.h"
#include "EspAt.h"

EspAt Esp;

/* ---- the conversation ---- */

/*
 * Send one command, collect what comes back until the module says how it
 * went.  "OK" and "ERROR" end a command; "FAIL" is what a failed join
 * says, and "ready" is what a module says after it has restarted on its
 * own -- both are answers, and waiting for an OK that will never come
 * would only cost time.
 */
bool EspAt::command(const char *cmd, unsigned long timeout_ms)
{
    unsigned long start = millis();
    int used = 0;

    resp[0] = '\0';

    Serial1.flushInput();
    if (trace)
        trace(cmd, true);
    Serial1.write(cmd);
    Serial1.write("\r\n");

    for (;;)
    {
        char line[128];
        int n;
        unsigned long left;

        if (millis() - start >= timeout_ms)
            return false;
        left = timeout_ms - (millis() - start);

        n = Serial1.readLine(line, (int)sizeof(line), left);
        if (n < 0)
            return false;
        if (n == 0)
            continue;                   /* the blank line after an echo */
        if (trace)
            trace(line, false);

        /* keep it, as much as fits */
        if (used + n + 2 < (int)sizeof(resp))
        {
            memcpy(resp + used, line, (unsigned)n);
            used += n;
            resp[used++] = '\n';
            resp[used] = '\0';
        }

        if (strcmp(line, "OK") == 0)
            return true;
        if (strcmp(line, "ERROR") == 0 || strcmp(line, "FAIL") == 0
            || strcmp(line, "ready") == 0)
            return false;
    }
}

bool EspAt::begin(unsigned long baud)
{
    Serial1.begin(baud);

    /* An "AT" right after power-up may land in the module's own start-up
       chatter, so ask twice before giving up on it. */
    if (command("AT", 1000))
        return true;
    delay(300);
    return command("AT", 1000);
}


/* ---- the network ---- */

bool EspAt::join(const char *ssid, const char *key, unsigned long timeout_ms)
{
    char cmd[128];

    if (!command("AT+CWMODE=1"))        /* station, not access point */
        return false;

    strcpy(cmd, "AT+CWJAP=\"");
    strncat(cmd, ssid, 32);
    strcat(cmd, "\",\"");
    strncat(cmd, key ? key : "", 63);
    strcat(cmd, "\"");

    return command(cmd, timeout_ms);
}

bool EspAt::joined(void)
{
    /* "+CIPSTA:ip:"192.168.1.42"" -- an address that is not 0.0.0.0 */
    if (!command("AT+CIPSTA?"))
        return false;
    return strstr(resp, "0.0.0.0") == 0 && strstr(resp, "+CIPSTA") != 0;
}


/* ---- the time ---- */

bool EspAt::startTime(short tz, const char *server)
{
    char cmd[96];
    char num[8];
    int i = 0, v = tz;

    if (v < 0)
    {
        num[i++] = '-';
        v = -v;
    }
    num[i++] = (char)('0' + v % 10);
    num[i] = '\0';

    /*
     * Firmware differs in what it wants here.  The 1.7 line of the
     * ESP8266 takes the server in quotes; some builds only take the
     * time zone and use their own server; and a few want no quotes.
     * Try them in that order rather than deciding for the module.
     */
    strcpy(cmd, "AT+CIPSNTPCFG=1,");
    strcat(cmd, num);
    strcat(cmd, ",\"");
    strncat(cmd, server, 48);
    strcat(cmd, "\"");
    if (command(cmd))
        return true;

    strcpy(cmd, "AT+CIPSNTPCFG=1,");
    strcat(cmd, num);
    strcat(cmd, ",");
    strncat(cmd, server, 48);
    if (command(cmd))
        return true;

    strcpy(cmd, "AT+CIPSNTPCFG=1,");
    strcat(cmd, num);
    return command(cmd);
}

static short month_of(const char *name)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    short i;

    for (i = 0; i < 12; i++)
        if (strncmp(months + 3 * i, name, 3) == 0)
            return (short)(i + 1);
    return 0;
}

/*
 * The module answers AT+CIPSNTPTIME? the way C's asctime() writes a
 * date:
 *
 *      +CIPSNTPTIME:Sat Sep 26 11:22:33 2026
 *
 * Until it has heard from a time server it says 1970, and that is how we
 * tell "not yet" from "here you are".
 */
bool EspAt::time(AtTime *t)
{
    char *p;

    if (!command("AT+CIPSNTPTIME?"))
        return false;

    p = strstr(resp, "+CIPSNTPTIME:");
    if (!p)
        return false;
    p += 13;

    while (*p == ' ')
        p++;
    if (strlen(p) < 24)
        return false;

    t->month = month_of(p + 4);
    t->day = (short)atoi(p + 8);
    t->hour = (short)atoi(p + 11);
    t->minute = (short)atoi(p + 14);
    t->second = (short)atoi(p + 17);
    t->year = (short)atoi(p + 20);

    if (t->month == 0 || t->year < 2020)
        return false;                   /* still 1970: no time yet */
    return true;
}

/* ==== a TCP connection ================================================= */

/*
 * The module is put into the two modes the rest of this depends on, every
 * time a connection is opened rather than once at the start: another
 * sketch, or the same one after a crash, may have left it in neither.
 *
 * ATE0 matters more than it looks. With the echo on, the module repeats
 * every command back -- and "AT+CIPRECVDATA=512" contains the very text
 * the reply to it is recognised by. A reader looking for the answer finds
 * the question instead.
 */
bool EspAt::tcpOpen(const char *host, unsigned short port,
                    unsigned long timeout_ms)
{
    char cmd[160];

    if (conn)
        tcpClose();

    Serial1.flushInput();

    command("ATE0", 2000);              /* no echo; ignore an old module
                                           that does not know it */
    if (!command("AT+CIPMUX=0", 2000))
        return false;
    if (!command("AT+CIPRECVMODE=1", 2000))
        return false;                   /* without this, data arrives
                                           unasked and cannot be read
                                           reliably at all */

    strcpy(cmd, "AT+CIPSTART=\"TCP\",\"");
    strncat(cmd, host, sizeof(cmd) - 40);
    strcat(cmd, "\",");
    {
        char num[8];
        int n = 0;
        unsigned short v = port;

        if (v == 0)
            num[n++] = '0';
        while (v)
        {
            num[n++] = (char)('0' + v % 10);
            v /= 10;
        }
        while (n)
        {
            char one[2] = { num[--n], '\0' };
            strcat(cmd, one);
        }
    }

    if (!command(cmd, timeout_ms))
        return false;

    conn = true;
    return true;
}

bool EspAt::tcpSend(const char *buf, int len)
{
    char cmd[32];
    unsigned long start;
    bool prompt = false;

    if (!conn || len <= 0)
        return false;

    while (len > 0)
    {
        int chunk = (len > 1024) ? 1024 : len;

        sprintf(cmd, "AT+CIPSEND=%d", chunk);
        if (trace)
            trace(cmd, true);
        Serial1.write(cmd);
        Serial1.write("\r\n");

        /*
         * Wait for "> ", which has no newline after it -- a line reader
         * waits for one that never comes, so this reads characters.
         */
        start = millis();
        while (millis() - start < 5000)
        {
            int c = Serial1.read();

            if (c == '>')
            {
                prompt = true;
                break;
            }
        }
        if (!prompt)
            return false;

        Serial1.write(buf, chunk);

        /*
         * And for SEND OK. Not command(): that would put a command on the
         * wire, and an empty one is still one -- it arrives while the
         * module is still sending and is answered with "busy s...", after
         * which the connection goes.
         */
        start = millis();
        for (;;)
        {
            char line[64];
            int n;

            if (millis() - start >= 20000)
                return false;
            n = Serial1.readLine(line, (int)sizeof(line), 1000);
            if (n <= 0)
                continue;
            if (trace)
                trace(line, false);
            if (strcmp(line, "SEND OK") == 0)
                break;
            if (strcmp(line, "SEND FAIL") == 0 || strcmp(line, "ERROR") == 0)
                return false;
            if (strcmp(line, "CLOSED") == 0)
            {
                conn = false;
                return false;
            }
        }

        buf += chunk;
        len -= chunk;
        prompt = false;
    }

    return true;
}

long EspAt::tcpAvail(void)
{
    const char *p;

    if (!conn)
        return -1;

    if (!command("AT+CIPRECVLEN?", 3000))
    {
        conn = false;
        return -1;
    }

    p = strstr(resp, "+CIPRECVLEN:");
    if (!p)
        return 0;

    return atol(p + 12);
}

int EspAt::tcpRead(char *buf, int max)
{
    char cmd[32];
    char hdr[64];
    unsigned long start;
    int i = 0, actual = 0, got = 0;
    long avail;

    if (!conn || max <= 0)
        return 0;

    avail = tcpAvail();
    if (avail <= 0)
        return 0;
    if (avail > max)
        avail = max;
    if (avail > 1460)
        avail = 1460;                   /* the module's limit per fetch */

    sprintf(cmd, "AT+CIPRECVDATA=%ld", avail);
    if (trace)
        trace(cmd, true);
    Serial1.write(cmd);
    Serial1.write("\r\n");

    /*
     * The header up to its separator, a character at a time: everything
     * after it is payload and may hold CR and LF of its own, so a line
     * reader would eat into the data.
     *
     *     +CIPRECVDATA,<len>:<data>      on 1.7.x
     *     +CIPRECVDATA:<len>,<data>      on 2.x
     *
     * Found by looking for the name, then digits, then one separator --
     * rather than by matching either spelling, which would work on one
     * firmware and quietly return nothing on the other.
     */
    hdr[0] = '\0';
    start = millis();
    for (;;)
    {
        int c = Serial1.read();
        const char *q;

        if (millis() - start >= 10000)
            return 0;
        if (c < 0)
            continue;

        if (i >= (int)sizeof(hdr) - 1)
        {                               /* slide, never stop */
            memmove(hdr, hdr + 1, sizeof(hdr) - 2);
            i--;
        }
        hdr[i++] = (char)c;
        hdr[i] = '\0';

        if (strstr(hdr, "ERROR"))
            return 0;

        q = strstr(hdr, "+CIPRECVDATA");
        if (!q)
            continue;
        q += 12;
        while (*q && (*q < '0' || *q > '9'))
            q++;
        if (!*q)
            continue;                   /* the digits are not all here */
        actual = atoi(q);
        while (*q >= '0' && *q <= '9')
            q++;
        if (*q == ':' || *q == ',')
            break;
    }

    if (actual > max)
        actual = max;

    start = millis();
    while (got < actual)
    {
        int c = Serial1.read();

        if (c < 0)
        {
            if (millis() - start >= 10000)
                break;
            continue;
        }
        buf[got++] = (char)c;
        start = millis();
    }

    /* the OK that closes the reply, and any CLOSED that came with it */
    start = millis();
    while (millis() - start < 2000)
    {
        char line[64];
        int n = Serial1.readLine(line, (int)sizeof(line), 300);

        if (n <= 0)
            continue;
        if (trace)
            trace(line, false);
        if (strcmp(line, "CLOSED") == 0)
            conn = false;
        if (strcmp(line, "OK") == 0)
            break;
    }

    return got;
}

void EspAt::tcpClose(void)
{
    if (conn)
        command("AT+CIPCLOSE", 5000);
    conn = false;
}
