/*
 * EspAt.h - a radio module that speaks AT commands
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * The machine has no radio.  A module on the second serial port has one,
 * and it is driven the way modems have been driven since the eighties:
 * a line of text goes out, lines of text come back, and the last one is
 * "OK" or "ERROR".  An ESP-01 with the AT firmware of Espressif is such
 * a module.
 *
 * What this is not: a network stack.  There is no stack here, and that
 * is the point -- the module has one, and we ask it for what we need.
 * Getting the time is two commands.
 *
 * Every call has a deadline and gives the processor away while it waits,
 * so a GEM program stays a good citizen: the desktop keeps drawing and
 * the clock keeps ticking while the module thinks.
 */

#ifndef GEMDUINO_ESPAT_H
#define GEMDUINO_ESPAT_H

struct AtTime
{
    short year, month, day;         /* 2026, 1..12, 1..31 */
    short hour, minute, second;
};

class EspAt
{
public:
    /*
     * Wake the port and see whether a module answers "AT" with "OK".
     * false means: nothing there, wrong baud rate, or no power -- and a
     * module whose supply sags under load answers exactly like one that
     * is not connected at all.
     */
    bool begin(unsigned long baud = 115200);

    /* Send a command and wait for OK.  The lines that came back are in
       response(), the newest last. */
    bool command(const char *cmd, unsigned long timeout_ms = 2000);

    /*
     * Watch the conversation.  Every line that goes out and every line
     * that comes back is handed to fn, with sent saying which way it
     * went.  Nothing else tells you as much about a module that will not
     * do what it is told.
     */
    void onTrace(void (*fn)(const char *line, bool sent)) { trace = fn; }

    const char *response(void) const { return resp; }

    /* "AT version:1.7.5.0(...)" and the rest of AT+GMR */
    bool version(void) { return command("AT+GMR"); }

    /* Join a network.  Takes its time: ten seconds is normal, twenty is
       not unusual on a busy band. */
    bool join(const char *ssid, const char *key,
              unsigned long timeout_ms = 25000);

    bool joined(void);              /* AT+CIPSTA?: is there an address? */

    /*
     * Ask the module to keep the time by itself (SNTP).  tz is whole
     * hours from UTC: 1 for Central European Time, 2 in summer.  The
     * module needs a few seconds afterwards before the time is right.
     */
    bool startTime(short tz, const char *server = "pool.ntp.org");

    /* The time, as the module has it.  false while it still says 1970. */
    bool time(AtTime *t);

private:
    void (*trace)(const char *line, bool sent) = 0;
    char resp[320];
};

extern EspAt Esp;

#endif /* GEMDUINO_ESPAT_H */
