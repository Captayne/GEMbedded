/*
 * InetFetch - type a URL, see what comes back
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * A window with a URL on the top line and the reply underneath, as text,
 * exactly as the server sent it -- headers and all, nothing rendered and
 * nothing hidden. That is the point: a browser shows you its opinion of
 * a page, and when a page does not appear there is no way to tell from
 * the screen whether the fault was in the fetching or in the showing.
 *
 * Which network, which password: whatever was entered under
 * Options -> Connecty. This does not join a network itself -- the clock
 * in ../WifiTime does that and leaves the module there.
 *
 * Only http://. The AT firmware on an ESP-01 has TLS from 2018 with
 * buffers to match, and no server of today will finish a handshake with
 * it. https:// is refused here rather than left to fail obscurely.
 *
 * Keys: anything printable goes into the URL, Backspace rubs out,
 * Return fetches, the arrows scroll the reply.
 */

#include <Arduino.h>            /* AES, VDI, and the real-time core */
#include <GEMduino.h>           /* UART1 and the AT module */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KIND        (NAME | CLOSER | MOVER | SIZER)
#define BODY        2048        /* of the reply, kept to show */
#define COLS        40
#define LINES       64          /* of wrapped text, kept to scroll */

static short vdi, win;
static char  title[] = " Fetch ";
static short wx, wy, ww, wh;

static char  url[128] = "http://example.com/";
static char  body[LINES][COLS + 1];
static short body_used, body_top;
static char  status[COLS + 1] = "Return fetches.";

/* ---- the window ---- */

static void layout(void)
{
    wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
}

/* how many lines of reply fit under the URL and above the status line */
static short visible(void)
{
    short n = (short)((wh - 34) / 10);

    return (n < 1) ? 1 : n;
}

static void draw(void)
{
    short xy[4], i, n;
    char line[COLS + 4];

    xy[0] = wx; xy[1] = wy;
    xy[2] = (short)(wx + ww - 1); xy[3] = (short)(wy + wh - 1);
    vsf_color(vdi, 0);
    vsf_interior(vdi, FIS_SOLID);
    vr_recfl(vdi, xy);

    vst_color(vdi, 1);

    /* the URL, with a cursor after it so that typing is visible */
    strncpy(line, url, COLS);
    line[COLS] = '\0';
    strcat(line, "_");
    v_gtext(vdi, (short)(wx + 4), (short)(wy + 12), line);

    /* a rule under it */
    xy[0] = (short)(wx + 2);  xy[1] = (short)(wy + 17);
    xy[2] = (short)(wx + ww - 3); xy[3] = (short)(wy + 17);
    v_pline(vdi, 2, xy);

    n = visible();
    for (i = 0; i < n && body_top + i < body_used; i++)
        v_gtext(vdi, (short)(wx + 4), (short)(wy + 30 + 10 * i),
                body[body_top + i]);

    v_gtext(vdi, (short)(wx + 4), (short)(wy + wh - 6), status);
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

static void refresh(void)
{
    redraw(wx, wy, ww, wh);
}

static void say(const char *text)
{
    strncpy(status, text, sizeof(status) - 1);
    status[sizeof(status) - 1] = '\0';
    refresh();
}

/* ---- the reply, wrapped into lines ---- */

static void body_clear(void)
{
    body_used = body_top = 0;
}

/*
 * One character at a time into the current line, breaking at the newline
 * the server sent or at the edge of the window, whichever comes first.
 * Tabs become a space and everything else unprintable is dropped: this
 * is for reading, and a raw control character in a GEM window is a glyph
 * that means nothing.
 */
static void body_put(char c)
{
    short len;

    if (body_used == 0)
    {
        body[0][0] = '\0';
        body_used = 1;
    }
    len = (short)strlen(body[body_used - 1]);

    if (c == '\r')
        return;
    if (c == '\t')
        c = ' ';

    if (c == '\n' || len >= COLS)
    {
        if (body_used >= LINES)
        {
            short i;

            for (i = 1; i < LINES; i++)
                strcpy(body[i - 1], body[i]);
            body_used--;
        }
        body[body_used][0] = '\0';
        body_used++;
        if (c == '\n')
            return;
        len = 0;
    }

    if (c < ' ')
        return;

    body[body_used - 1][len] = c;
    body[body_used - 1][len + 1] = '\0';
}

/* ---- fetching ---- */

/*
 * Split "http://host[:port]/path" apart. Returns false for anything that
 * is not plain http, which includes https:// -- see the note at the top
 * about why that is refused rather than attempted.
 */
static bool split_url(const char *u, char *host, unsigned short *port,
                      char *path)
{
    const char *p = u;
    short i = 0;

    if (strncmp(p, "https://", 8) == 0)
        return false;
    if (strncmp(p, "http://", 7) == 0)
        p += 7;

    *port = 80;
    while (*p && *p != '/' && *p != ':' && i < 63)
        host[i++] = *p++;
    host[i] = '\0';
    if (i == 0)
        return false;

    if (*p == ':')
    {
        p++;
        *port = (unsigned short)atoi(p);
        while (*p && *p != '/')
            p++;
    }

    strcpy(path, (*p == '/') ? p : "/");
    return true;
}

static void fetch(void)
{
    char host[64], path[128], req[256], buf[256];
    unsigned short port;
    long total = 0;
    short quiet = 0;
    int n;

    body_clear();

    if (!split_url(url, host, &port, path))
    {
        say("Only http:// -- no TLS here.");
        return;
    }

    say("Asking the module...");
    if (!Esp.begin())
    {
        say("No module on UART1.");
        return;
    }
    if (!Esp.joined())
    {
        say("Not on a network. Connecty?");
        return;
    }

    say("Connecting...");
    if (!Esp.tcpOpen(host, port))
    {
        say("Could not connect.");
        return;
    }

    sprintf(req,
            "GET %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "User-Agent: GEMbedded\r\n"
            "Connection: close\r\n"
            "\r\n", path, host);

    say("Sending...");
    if (!Esp.tcpSend(req, (int)strlen(req)))
    {
        say("Sending failed.");
        Esp.tcpClose();
        return;
    }

    say("Reading...");

    /*
     * Until the other end goes away and nothing is left. A zero is "none
     * yet" rather than the end, so this counts empty rounds: that is also
     * what gets it out of here if the module stops answering altogether.
     */
    while (quiet < 400)
    {
        n = Esp.tcpRead(buf, (int)sizeof(buf));

        if (n <= 0)
        {
            if (!Esp.tcpOpened())
                break;
            quiet++;
            continue;
        }

        quiet = 0;
        total += n;
        {
            int i;

            for (i = 0; i < n; i++)
                body_put(buf[i]);
        }
        refresh();
    }

    Esp.tcpClose();

    sprintf(status, "%ld bytes.", total);
    refresh();
}

/* ---- typing ---- */

static void key(short scan, short ascii)
{
    short len = (short)strlen(url);

    switch (scan)
    {
    case 0x1c:                          /* Return */
    case 0x72:                          /* Enter, on the keypad */
        fetch();
        return;
    case 0x0e:                          /* Backspace */
        if (len)
            url[len - 1] = '\0';
        refresh();
        return;
    case 0x48:                          /* up */
        if (body_top)
            body_top--;
        refresh();
        return;
    case 0x50:                          /* down */
        if (body_top + visible() < body_used)
            body_top++;
        refresh();
        return;
    case 0x49:                          /* page up, keypad 9 */
        body_top = (short)((body_top > visible()) ? body_top - visible() : 0);
        refresh();
        return;
    case 0x51:                          /* page down, keypad 3 */
        if (body_top + 2 * visible() <= body_used)
            body_top = (short)(body_top + visible());
        refresh();
        return;
    }

    if (ascii >= ' ' && ascii < 127 && len < (short)sizeof(url) - 1)
    {
        url[len] = (char)ascii;
        url[len + 1] = '\0';
        refresh();
    }
}

/* ---- the program ---- */

int main(void)
{
    short wchar, hchar, wbox, hbox;
    short msg[8], dx, dy, dw, dh, x, y, w, h;
    int running = 1;

    if (appl_init() < 0)
        return 1;

    vdi = v_opnvwk(graf_handle(&wchar, &hchar, &wbox, &hbox));
    if (!vdi)
    {
        appl_exit();
        return 1;
    }

    wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
    wind_calc(WC_BORDER, KIND, 0, 0, COLS * 8, 20 * 10, &x, &y, &w, &h);
    win = wind_create(KIND, dx, dy, dw, dh);
    if (win < 0)
    {
        form_alert(1, "[1][No window left.][ OK ]");
        v_clsvwk(vdi);
        appl_exit();
        return 1;
    }
    wind_set_name(win, title);
    wind_open(win, (short)(dx + (dw - w) / 2), (short)(dy + (dh - h) / 2),
              w, h);
    layout();
    refresh();

    while (running)
    {
        short kret = 0, bret = 0, mx, my, button, kstate;
        short ev = evnt_multi_button_timer(500, &mx, &my, &button, &kstate,
                                           &kret, &bret, msg);

        if (ev & MU_KEYBD)
            key((short)((kret >> 8) & 0xff), (short)(kret & 0xff));

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
                refresh();
                break;
            case WM_CLOSED:
                running = 0;
                break;
            }
        }
    }

    Esp.tcpClose();
    wind_close(win);
    wind_delete(win);
    v_clsvwk(vdi);
    appl_exit();
    return 0;
}
