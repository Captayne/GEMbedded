/*
 * WifiTime - fetch the time over WLAN and set the machine's clock
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * The machine has no clock that survives being switched off: every start
 * begins at midnight until somebody says otherwise.  A radio module on
 * the second serial port can ask the internet, so that is what this
 * does, and then it sets the system clock -- so the desktop shows the
 * right time and files get the right date, not just this window.
 *
 * The network and its key come from the desktop: Options -> "Wifi
 * settings...", saved with "Save desktop".  No password in a sketch.
 *
 * Wiring, with the pins that are free on this machine:
 *
 *      ESP-01              RP2350
 *      TXD                 GPIO 5   (UART1 in)
 *      RXD                 GPIO 4   (UART1 out)
 *      CH_PD / EN          3.3 V    (or it stays silent)
 *      VCC                 3.3 V, from its own supply
 *      GND                 GND
 *
 * The supply is what usually goes wrong: the module pulls some 300 mA
 * while it transmits, which no GPIO pin can give, and a module that
 * browns out behaves exactly like one that is not there.
 */

#include <Arduino.h>            /* AES, VDI, and the real-time core */
#include <GEMduino.h>           /* time, pins, UART1, the AT module */
#include "wifi.h"               /* the _WIF cookie: network and key */
#include <string.h>
#include <mint/osbind.h>
#include <mint/mintbind.h>

#define KIND        (NAME | CLOSER | MOVER | SIZER)
#define LINES       9           /* what the window keeps of the log */

static short vdi, win;
static char  title[] = " Wifi time ";
static short wx, wy, ww, wh;

static char  log_line[LINES][40];
static short log_used;
static char  clock_text[24];    /* "00:00:00  2026-09-26" and its NUL */

/* ---- the window ---- */

static void layout(void)
{
    wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
}

static void draw(void)
{
    short xy[4], i;

    xy[0] = wx; xy[1] = wy;
    xy[2] = (short)(wx + ww - 1); xy[3] = (short)(wy + wh - 1);
    vsf_color(vdi, 0);
    vsf_interior(vdi, FIS_SOLID);
    vr_recfl(vdi, xy);

    vst_color(vdi, 1);
    for (i = 0; i < log_used; i++)
        v_gtext(vdi, (short)(wx + 4), (short)(wy + 12 + 10 * i), log_line[i]);

    if (clock_text[0])
        v_gtext(vdi, (short)(wx + 4), (short)(wy + wh - 6), clock_text);
}

static void redraw(short x, short y, short w, short h)
{
    short rx, ry, rw, rh, clip[4];

    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, 0L);

    wind_get(win, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
    while (rw && rh)
    {
        short x1 = (rx > x) ? rx : x;
        short y1 = (ry > y) ? ry : y;
        short x2 = ((rx + rw) < (x + w)) ? (short)(rx + rw) : (short)(x + w);
        short y2 = ((ry + rh) < (y + h)) ? (short)(ry + rh) : (short)(y + h);

        if (x1 < x2 && y1 < y2)
        {
            clip[0] = x1; clip[1] = y1;
            clip[2] = (short)(x2 - 1); clip[3] = (short)(y2 - 1);
            vs_clip(vdi, 1, clip);
            draw();
        }
        wind_get(win, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
    }

    vs_clip(vdi, 0, clip);
    graf_mouse(M_ON, 0L);
    wind_update(END_UPDATE);
}

/* a line in the window, and the older ones move up */
static void say(const char *text)
{
    short i;

    if (log_used == LINES)
    {
        for (i = 1; i < LINES; i++)
            strcpy(log_line[i - 1], log_line[i]);
        log_used--;
    }
    strncpy(log_line[log_used], text, sizeof(log_line[0]) - 1);
    log_line[log_used][sizeof(log_line[0]) - 1] = '\0';
    log_used++;
    redraw(wx, wy, ww, wh);
}

/* ---- the clock ---- */

static void two(char *p, short v)
{
    p[0] = (char)('0' + (v / 10) % 10);
    p[1] = (char)('0' + v % 10);
}

static void show_clock(void)
{
    unsigned short t = (unsigned short)Tgettime();
    unsigned short d = (unsigned short)Tgetdate();

    /* GEMDOS packs them: time is hhhhhmmmmmmsssss (seconds in twos),
       date is yyyyyyymmmmddddd counted from 1980 */
    strcpy(clock_text, "00:00:00  ....-..-..");
    two(clock_text + 0, (short)(t >> 11));
    two(clock_text + 3, (short)((t >> 5) & 0x3f));
    two(clock_text + 6, (short)((t & 0x1f) * 2));
    two(clock_text + 10, (short)((1980 + (d >> 9)) / 100));
    two(clock_text + 12, (short)((1980 + (d >> 9)) % 100));
    two(clock_text + 15, (short)((d >> 5) & 0x0f));
    two(clock_text + 18, (short)(d & 0x1f));
    clock_text[14] = '-';
    clock_text[17] = '-';
    redraw(wx, wy, ww, wh);
}

static void set_clock(const AtTime *t)
{
    unsigned short date = (unsigned short)(((t->year - 1980) << 9)
                                           | (t->month << 5) | t->day);
    unsigned short time = (unsigned short)((t->hour << 11)
                                           | (t->minute << 5)
                                           | (t->second / 2));
    Tsetdate(date);
    Tsettime(time);
}

/* ---- the module ---- */

static const char *ssid = "";
static const char *key = "";

static bool find_settings(void)
{
    long value = 0;
    struct wif_api *w;

    if (Ssystem(S_GETCOOKIE, WIF_COOKIE, (long)&value) != 0 || !value)
        return false;
    w = (struct wif_api *)value;
    ssid = w->ssid();
    key = w->key();
    return ssid[0] != '\0';
}

static void get_time(void)
{
    AtTime t;
    short tries;

    say("Waking the module...");
    if (!Esp.begin())
    {
        say("No answer on GPIO 4/5.");
        say("Power? CH_PD high? 115200?");
        return;
    }
    say("Module is there.");

    say("Joining the network...");
    if (!Esp.join(ssid, key))
    {
        say("Could not join.");
        return;
    }
    say("Joined.");

    if (!Esp.startTime(1))              /* whole hours from UTC */
    {
        say("This firmware has no SNTP.");
        return;
    }

    say("Asking for the time...");
    for (tries = 0; tries < 15; tries++)
    {
        if (Esp.time(&t))
        {
            set_clock(&t);
            say("The clock is set.");
            show_clock();
            return;
        }
        delay(1000);                    /* the others keep running */
    }
    say("No time came back.");
}

/* ---- the program ---- */

int main(void)
{
    short wchar, hchar, wbox, hbox;
    short msg[8], dx, dy, dw, dh, x, y, w, h;
    int running = 1;
    unsigned short last = 0xffff;

    if (appl_init() < 0)
        return 1;

    vdi = v_opnvwk(graf_handle(&wchar, &hchar, &wbox, &hbox));
    if (!vdi)
    {
        appl_exit();
        return 1;
    }

    wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
    wind_calc(WC_BORDER, KIND, 0, 0, 34 * 8, 13 * 10, &x, &y, &w, &h);
    win = wind_create(KIND, dx, dy, dw, dh);
    if (win < 0)
    {
        form_alert(1, "[1][No window left.][ OK ]");
        v_clsvwk(vdi);
        appl_exit();
        return 1;
    }
    wind_set_name(win, title);
    wind_open(win, (short)(dx + (dw - w) / 2), (short)(dy + (dh - h) / 2), w, h);
    layout();

    if (!find_settings())
    {
        say("No network is set up.");
        say("Options -> Wifi settings...");
    }
    else
    {
        say(ssid);
        get_time();
    }

    while (running)
    {
        short ev = evnt_multi_mesag_timer(500, msg);

        if (ev & MU_MESAG)
        {
            if (msg[3] != win)
                continue;
            switch (msg[0])
            {
            case WM_REDRAW:
                redraw(msg[4], msg[5], msg[6], msg[7]);
                break;
            case WM_TOPPED:
                wind_set(win, WF_TOP, 0, 0, 0, 0);
                break;
            case WM_MOVED:
            case WM_SIZED:
                wind_set(win, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
                layout();
                redraw(wx, wy, ww, wh);
                break;
            case WM_CLOSED:
                running = 0;
                break;
            }
        }

        /* keep the clock line honest, once a second */
        if (clock_text[0])
        {
            unsigned short now = (unsigned short)Tgettime();

            if (now != last)
            {
                last = now;
                show_clock();
            }
        }
    }

    wind_close(win);
    wind_delete(win);
    v_clsvwk(vdi);
    appl_exit();
    return 0;
}
