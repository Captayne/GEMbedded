/*
 * EspAt.cpp - talking to an AT module
 *
 * Copyright (C) 2026 Andreas Keibel
 */

#include <string.h>
#include <stdlib.h>
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

    strcpy(cmd, "AT+CIPSNTPCFG=1,");
    strcat(cmd, num);
    strcat(cmd, ",\"");
    strncat(cmd, server, 48);
    strcat(cmd, "\"");

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
