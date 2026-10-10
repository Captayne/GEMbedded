/*
 * espnet.c - HighWire's network, over AT commands to an ESP module
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * This replaces net/dummynet.c, which answered every address with the
 * same invented page so that the rest of the browser could be brought up
 * without a network. The eight calls are the same eight; what changed is
 * that they now reach a module on UART1.
 *
 * WHAT IT WAS WRITTEN AGAINST
 *
 * An ESP8266 (WROOM-02) with AT firmware 1.7.5, in station mode, already
 * joined to a network -- the clock in GEMduino/sketchbook/WifiTime puts
 * it there and leaves it there. ESPINFO.PRG (GEM/examples/espinfo) is
 * what asked it, and the answer that decided this file's shape was:
 *
 *     AT+CIPRECVMODE?  ->  +CIPRECVMODE:0  OK
 *
 * The command exists. That matters more than it sounds. In the mode the
 * module starts in, received data arrives on its own, as "+IPD,<len>:"
 * followed by bytes, at any moment -- including in the middle of the
 * reply to something else. Reading a socket then means picking payload
 * out of a conversation as it goes past, and every command reply has to
 * be parsed with that in mind.
 *
 * With AT+CIPRECVMODE=1 the module only says how much is waiting, and
 * hands it over when asked. Reading a socket becomes a command with a
 * reply, which is a different and far smaller program -- this one.
 *
 * NO TLS
 *
 * 1.7.5 has AT+CIPSTART="SSL", and it is TLS as it stood in 2018, with
 * buffers to match. Against a server of today it does not complete a
 * handshake. So this speaks plain TCP, http:// works and https:// does
 * not, and the browser will say so rather than appear broken.
 */

#include <stddef.h>
#include <string.h>

#include "hw-types.h"
#include "inet.h"
#include "ovl_sys.h"

#include <osbind.h>
#include <mint/mintbind.h>

#include "uart1.h"

/* ==== the port ======================================================== */

static struct ua1_api *ua1;
static BOOL ready;              /* the module answered, and is set up */

/*
 * ==== the transcript ==================================================
 *
 * Every byte in both directions, appended to C:\ESPLOG.TXT.
 *
 * Not tracing in the usual sense. What goes wrong between a browser and
 * an AT module is almost never visible from either end: HighWire says
 * "the reply was not HTTP" and the module says OK to everything, and
 * neither of them can show what actually crossed the wire. Guessing from
 * those two reports is how an afternoon disappears.
 *
 * Direction is marked, binary is written as it is. Set ESP_LOG to 0 when
 * this stops earning the disk access it costs.
 */
#define ESP_LOG     1

#if ESP_LOG
#include "usbcon.h"

/*
 * To the USB console rather than to a file.
 *
 * The console is UART0 and the module is on UART1, so the two do not
 * meet; and the console can be read from the other machine while this
 * one is still running, with no disk, no sharing and nothing to go
 * wrong on the way. A first attempt wrote C:\ESPLOG.TXT and produced no
 * file at all, which said nothing about why -- the disk had become a
 * second thing that could fail silently.
 */
static void logbytes(char dir, const void *buf, long len)
{
    static struct ucn_api *ucn;
    static int looked;

    if (len <= 0)
        return;

    if (!looked)
    {
        long value = 0;

        looked = 1;
        if (Ssystem(S_GETCOOKIE, UCN_COOKIE, (long)&value) == 0 && value)
            ucn = (struct ucn_api *)value;
    }
    if (!ucn)
        return;

    ucn->write((dir == '>') ? "\r\n>" : "\r\n<", 3);
    ucn->write(buf, len);
}
#else
#define logbytes(d, b, l)   ((void)0)
#endif

/* every read and write goes through these, so nothing escapes the log */
static long esp_write(const void *buf, long len)
{
    logbytes('>', buf, len);
    return ua1->write(buf, len);
}

static long esp_read(void *buf, long len)
{
    long n = ua1->read(buf, len);

    if (n > 0)
        logbytes('<', buf, n);
    return n;
}

#define ESP_BAUD        115200L
#define HZ200           0x4baL  /* the 200 Hz counter, in low memory */

static long now(void)
{
    return (long)Ssystem(S_GETLVAL, HZ200, 0);
}

/*
 * One line from the module, without its CR/LF, or -1 when nothing
 * arrived in time. An empty line is a line: the module sends plenty of
 * them and swallowing them here would lose the shape of a reply.
 */
/*
 * The line being assembled, kept across calls.
 *
 * It has to be. A caller gives up after its own timeout, and whatever
 * had arrived by then was in a local buffer that went with the frame:
 * the next call started in the middle of a line and the one before it
 * was lost. Mostly that line was "+IPD,<len>" -- the only word the
 * module says unasked and the one the whole read path waits for -- so a
 * page loaded or did not depending on whether its announcement happened
 * to arrive in one piece. It arrives in pieces about as often as not.
 */
static char part[160];
static long partlen;

static long at_line(char *buf, long size, long msec)
{
    long deadline = now() + msec / 5;

    for (;;)
    {
        char c;

        if (esp_read(&c, 1) != 1)
        {
            if (now() > deadline)
                return -1;              /* what was read stays in part[] */
            continue;
        }

        if (c == '\n')
        {
            long len = partlen < size - 1 ? partlen : size - 1;

            memcpy(buf, part, len);
            buf[len] = '\0';
            partlen = 0;
            return len;
        }
        if (c == '\r')
            continue;

        if (partlen < (long)sizeof(part) - 1)
            part[partlen++] = c;
    }
}

/*
 * Send a command and read until the module has finished answering.
 *
 * "Finished" is OK, ERROR, FAIL, or SEND OK -- and every line before it
 * is handed to 'want', which picks out what the caller is after. That is
 * why this takes a function rather than returning the lines: only the
 * caller knows which of them matters, and keeping them all would need a
 * buffer sized for the largest reply anybody might ever add.
 */
typedef void (*at_want)(const char *line, void *arg);

static void note_line(const char *line);        /* below */

/*
 * cmd may be NULL, which means "do not send anything, only read the
 * reply to what was sent by hand".
 *
 * That case exists because sending an empty command is not the same as
 * sending nothing: at_cmd("") still puts a CRLF on the wire, and after
 * the payload of an AT+CIPSEND the module reads that as a command while
 * it is still busy. It answers "busy s...", the connection goes, and the
 * rest of the request meets "link is not valid". The transcript showed
 * exactly that, and it had cost a whole request to find.
 */
static BOOL at_cmd(const char *cmd, long msec, at_want want, void *arg)
{
    char line[160];

    if (!ua1)
        return FALSE;

    if (cmd)
    {
        esp_write(cmd, (long)strlen(cmd));
        esp_write("\r\n", 2);
    }

    for (;;)
    {
        if (at_line(line, (long)sizeof(line), msec) < 0)
            return FALSE;                       /* it stopped talking */

        if (strcmp(line, "OK") == 0 || strcmp(line, "SEND OK") == 0)
            return TRUE;
        if (strcmp(line, "ERROR") == 0 || strcmp(line, "FAIL") == 0
         || strcmp(line, "SEND FAIL") == 0)
            return FALSE;

        /*
         * Every line, always, before the caller gets a look at it.
         *
         * "+IPD,<len>" and "CLOSED" are not answers to anything -- the
         * module says them when it has something to say, which includes
         * in the middle of the reply to a command. They used to go to
         * 'want', and 'want' is NULL for most callers, so they were
         * dropped. Whether a page loaded then came down to whether the
         * server answered before or after a SEND OK: once it worked and
         * once the browser sat there asking for data that had been
         * announced to nobody.
         */
        note_line(line);

        if (want)
            want(line, arg);
    }
}

/* ==== one connection ================================================== */

/*
 * The module does one at a time (AT+CIPMUX:0), so this does too. The
 * handle is 1 when it is open and nothing else is ever handed out, which
 * keeps zero free to mean "no connection" the way a handle should.
 */
#define THE_HANDLE  1L

static BOOL open_conn;
static long pending;            /* bytes the module says are waiting */
static BOOL peer_closed;

/* ==== setting the module up =========================================== */

static void grab_status(const char *line, void *arg)
{
    int *status = arg;

    if (strncmp(line, "STATUS:", 7) == 0)
        *status = line[7] - '0';
}

/*
 * Reach the module and put it in the two modes this file assumes.
 *
 * Deliberately not AT+RST. A reset drops the network the clock put the
 * module on, and joining again needs credentials this has no business
 * knowing. If the module is not on a network, that is something to
 * report rather than to fix from here.
 */
static BOOL esp_setup(void)
{
    long value = 0;
    int status = 0;

    if (ready)
        return TRUE;

    if (Ssystem(S_GETCOOKIE, UA1_COOKIE, (long)&value) != 0 || !value)
        return FALSE;
    ua1 = (struct ua1_api *)value;

    if (ua1->open(ESP_BAUD) < 0)
        return FALSE;
    ua1->flush();

    if (!at_cmd("AT", 2000, NULL, NULL))
        return FALSE;

    /*
     * No echo. The module repeats every command back, and
     * "AT+CIPRECVDATA=30" contains the very text the reply to it is
     * recognised by -- so the reader found the echo, took the 30 out of
     * "=30", failed to find the separator that follows a real header,
     * and read until it gave up. Switching the echo off is simpler than
     * teaching the reader to tell one from the other, and it halves what
     * crosses the wire.
     */
    at_cmd("ATE0", 2000, NULL, NULL);

    /* one connection, and data only when asked for */
    if (!at_cmd("AT+CIPMUX=0", 2000, NULL, NULL))
        return FALSE;
    if (!at_cmd("AT+CIPRECVMODE=1", 2000, NULL, NULL))
        return FALSE;

    /* 2 is "joined, with an address"; 3 and 4 are a connection that is
       still open or has just closed, which is also joined */
    if (!at_cmd("AT+CIPSTATUS", 5000, grab_status, &status))
        return FALSE;
    if (status < 2 || status > 4)
        return FALSE;

    ready = TRUE;
    return TRUE;
}

/* ==== addresses ======================================================= */

static void grab_domain(const char *line, void *arg)
{
    const char *p = strstr(line, "+CIPDOMAIN:");
    long *addr = arg;
    long part = 0, out = 0;
    int dots = 0;

    if (!p)
        return;
    p += 11;
    while (*p == '"' || *p == ' ')
        p++;

    for (; *p; p++)
    {
        if (*p >= '0' && *p <= '9')
            part = part * 10 + (*p - '0');
        else if (*p == '.')
        {
            out = (out << 8) | (part & 0xff);
            part = 0;
            dots++;
        }
        else
            break;
    }
    if (dots == 3)
        *addr = (out << 8) | (part & 0xff);
}

static short __CDECL esp_host_addr(const char *host, long *addr)
{
    char cmd[160];
    long out = 0;

    /* a mark of its own, so that "nothing in the log" can be told apart
       from "HighWire never asked" */
    logbytes('>', "[host_addr]", 11);

    if (!esp_setup())
    {
        logbytes('>', "[setup failed]", 14);
        return -1;
    }

    strcpy(cmd, "AT+CIPDOMAIN=\"");
    strncat(cmd, host, sizeof(cmd) - 20);
    strcat(cmd, "\"");

    if (!at_cmd(cmd, 15000, grab_domain, &out) || out == 0)
        return -1;

    if (addr)
        *addr = out;
    return 0;                   /* E_OK */
}

/* ==== connecting ====================================================== */

static char *put_num(char *p, long v)
{
    char tmp[12];
    int n = 0;

    if (v == 0)
        *p++ = '0';
    while (v > 0)
    {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n)
        *p++ = tmp[--n];
    *p = '\0';
    return p;
}

static long __CDECL esp_connect(long addr, long port, long tout_sec)
{
    char cmd[80];
    char *p;

    if (!esp_setup())
        return -1;
    if (open_conn)
        return -35;             /* EMFILE: the module does one at a time */

    /* start clean: whatever the last connection left behind belongs to
       the last connection, not to this one */
    ua1->flush();
    partlen = 0;
    pending = 0;
    peer_closed = FALSE;

    /* AT+CIPSTART="TCP","a.b.c.d",port -- the module wants it dotted,
       and host_addr() above packed it into a long for HighWire */
    p = cmd;
    strcpy(p, "AT+CIPSTART=\"TCP\",\"");
    p += strlen(p);
    p = put_num(p, (addr >> 24) & 0xff);  *p++ = '.';
    p = put_num(p, (addr >> 16) & 0xff);  *p++ = '.';
    p = put_num(p, (addr >> 8) & 0xff);   *p++ = '.';
    p = put_num(p, addr & 0xff);
    strcpy(p, "\",");
    p += 2;
    p = put_num(p, port);

    if (!at_cmd(cmd, (tout_sec > 0 ? tout_sec : 15) * 1000, NULL, NULL))
        return -1;

    open_conn = TRUE;
    peer_closed = FALSE;
    pending = 0;
    return THE_HANDLE;
}

static void __CDECL esp_close(long fh)
{
    if (fh != THE_HANDLE || !open_conn)
        return;

    at_cmd("AT+CIPCLOSE", 5000, NULL, NULL);
    open_conn = FALSE;
    pending = 0;
}

/* ==== sending ========================================================= */

/*
 * AT+CIPSEND=<len>, then the bytes once the module has answered with its
 * prompt. Not at_cmd(): the prompt is "> " with no newline after it, so
 * a line reader waits for one that never comes.
 */
static long __CDECL esp_send(long fh, const char *buf, size_t len)
{
    char cmd[32];
    char *p;
    long deadline;

    if (fh != THE_HANDLE || !open_conn)
        return -37;             /* EBADF */
    if (len == 0)
        return 0;
    if (len > 2048)
        len = 2048;             /* what the module takes in one go */

    p = cmd;
    strcpy(p, "AT+CIPSEND=");
    p += strlen(p);
    put_num(p, (long)len);

    esp_write(cmd, (long)strlen(cmd));
    esp_write("\r\n", 2);

    /* wait for the prompt */
    deadline = now() + 5000 / 5;
    for (;;)
    {
        char c;

        if (esp_read(&c, 1) == 1)
        {
            if (c == '>')
                break;
        }
        else if (now() > deadline)
            return -1;
    }

    esp_write(buf, (long)len);

    /* and for SEND OK, which at_cmd() recognises as an ending */
    if (!at_cmd(NULL, 20000, NULL, NULL))
        return -1;

    return (long)len;
}

/* ==== receiving ======================================================= */

/*
 * What the module says on its own: how much has arrived, and whether the
 * other end has gone. In AT+CIPRECVMODE=1 these are the only unasked-for
 * lines, which is the whole point of that mode.
 */
static void note_line(const char *line)
{
    if (strncmp(line, "+IPD,", 5) == 0)
    {
        long n = 0;
        const char *p = line + 5;

        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        pending += n;
    }
    else if (strcmp(line, "CLOSED") == 0)
    {
        /*
         * Only while there is something to close.
         *
         * A CLOSED left over from the page before used to be thrown
         * away with every other line nobody asked for; now that they are
         * all heeded, a stale one arriving during AT+CIPSTART or the
         * first AT+CIPSEND shut a connection that had only just been
         * made, and the browser reported a bad file descriptor on the
         * second half of its own request.
         */
        if (open_conn)
        {
            peer_closed = TRUE;
            open_conn = FALSE;
        }
    }
}

/* Take in whatever the module has volunteered, without waiting for it. */
static void esp_poll(void)
{
    char line[160];

    while (ua1 && ua1->status() > 0)
    {
        if (at_line(line, (long)sizeof(line), 200) < 0)
            return;
        note_line(line);
    }
}

static long __CDECL esp_instat(long fh)
{
    if (fh != THE_HANDLE)
        return -37;

    esp_poll();
    return pending;
}

/*
 * AT+CIPRECVDATA=<len>, whose answer carries the bytes themselves:
 *
 *     +CIPRECVDATA,<actual>:<data>      (1.7.x)
 *     +CIPRECVDATA:<actual>,<data>      (2.x and later)
 *
 * Both are handled by looking for the name, then the first run of
 * digits, then exactly one separator -- rather than by matching either
 * spelling, which would work on one firmware and silently return
 * nothing on the other.
 */
static long __CDECL esp_recv(long fh, char *buf, size_t len)
{
    char hdr[48];
    long want, got = 0, actual = 0;
    long deadline;
    int i = 0;
    char cmd[32];
    char *p;

    if (fh != THE_HANDLE)
        return -37;

    esp_poll();
    {   /* what was asked, so that the log shows the caller too */
        char mark[48];
        char *m = mark;

        strcpy(m, "[recv len=");
        m += 10;
        m = put_num(m, (long)len);
        strcpy(m, " pend=");
        m += 6;
        m = put_num(m, pending);
        *m++ = ']';
        logbytes('>', mark, m - mark);
    }

    if (pending == 0)
        return peer_closed ? 0 : 0;     /* nothing, and that is not an error */

    want = ((long)len < pending) ? (long)len : pending;
    if (want > 1460)
        want = 1460;            /* the module's own limit per fetch */

    p = cmd;
    strcpy(p, "AT+CIPRECVDATA=");
    p += strlen(p);
    put_num(p, want);

    esp_write(cmd, (long)strlen(cmd));
    esp_write("\r\n", 2);

    /*
     * Read the header a character at a time up to the separator. A line
     * reader cannot be used: everything after the separator is payload
     * and may hold CR and LF of its own.
     */
    deadline = now() + 10000 / 5;
    for (;;)
    {
        char c;

        if (esp_read(&c, 1) != 1)
        {
            if (now() > deadline)
                return -1;
            continue;
        }
        /*
         * A window that slides rather than a buffer that fills.
         *
         * Whatever the module volunteered before the answer -- "+IPD",
         * "CLOSED", blank lines -- comes first, and there is no limit on
         * how much of it there can be. This used to stop appending once
         * it was full, so if the header had not appeared by then it
         * never would, and the read ran to its timeout and reported a
         * broken connection on a connection that was fine.
         */
        if (i >= (int)sizeof(hdr) - 1)
        {
            memmove(hdr, hdr + 1, sizeof(hdr) - 2);
            i--;
        }
        hdr[i++] = c;
        hdr[i] = '\0';

        if (strstr(hdr, "+CIPRECVDATA"))
        {
            const char *q = strstr(hdr, "+CIPRECVDATA") + 12;

            while (*q && (*q < '0' || *q > '9'))
                q++;
            if (!*q)
                continue;                       /* digits not here yet */
            actual = 0;
            while (*q >= '0' && *q <= '9')
                actual = actual * 10 + (*q++ - '0');
            if (*q == ':' || *q == ',')
                break;                          /* payload starts next */
        }
        if (strstr(hdr, "ERROR"))
            return -1;
    }

    while (got < actual)
    {
        long n = esp_read(buf + got, actual - got);

        if (n > 0)
        {
            got += n;
            deadline = now() + 10000 / 5;
        }
        else if (now() > deadline)
            break;
    }

    pending -= got;
    if (pending < 0)
        pending = 0;

    /* the OK that closes the reply, and anything volunteered with it */
    at_cmd(NULL, 3000, NULL, NULL);

    return got;
}

/*
 * Wait until there is something to read, or the time is up. HighWire
 * uses this to sleep rather than spin; there is nothing to wait on here
 * but the module, so this asks it and gives the machine back in between.
 */
static long __CDECL esp_select(long timeout, long *rfds, long *wfds)
{
    long deadline = now() + timeout / 5;

    if (wfds && *wfds)
        return 1;               /* the module is always ready to be sent to */

    if (!rfds || !*rfds)
        return 0;

    for (;;)
    {
        esp_poll();
        if (pending > 0 || peer_closed)
            return 1;
        if (now() > deadline)
            return 0;
    }
}

static const char * __CDECL esp_info(void)
{
    return "GEMbedded: TCP over AT commands, no TLS";
}

/* ==== the module ====================================================== */

static INET_FTAB esp_ftab = {
    esp_host_addr,
    esp_connect,
    esp_send,
    esp_recv,
    esp_close,
    esp_instat,
    esp_select,
    esp_info
};

static struct ovl_info_t esp_info_block = {
    "espnet",
    "0.1",
    "2026-10-02",
    "GEMbedded"
};

static long __CDECL esp_ovl_init(void)
{
    return INET_VERSION;
}

static struct ovl_info_t * __CDECL esp_ovl_version(void)
{
    return &esp_info_block;
}

static void * __CDECL esp_ovl_getftab(void)
{
    return &esp_ftab;
}

static long __CDECL esp_ovl_free(void)
{
    if (open_conn)
        esp_close(THE_HANDLE);
    return 0;
}

static long esp_magic = OVL_MAGIC;

static OVL_METH esp_meth = {
    OVL_MAGIC,
    OVL_REVISION,
    OF_SIMPLE,
    FTAB_NETWORK,
    esp_ovl_init,
    esp_ovl_version,
    esp_ovl_getftab,
    esp_ovl_free,
    &esp_magic
};

/* ==== what ovl_sys.c used to do ======================================= */

/*
 * There is no Pexec here and no overlay on a disk: the module is built
 * in, and load_ovl() hands it over without touching one. Everything
 * above this -- the function table, the eight calls, the version
 * handshake -- is what HighWire already expects.
 */
OVL_METH *load_ovl(const char *ovl_name, void (*handler)(void *))
{
    (void)handler;

    if (ovl_name && strcmp(ovl_name, "network.ovl") == 0)
        return &esp_meth;

    return NULL;
}

void kill_ovl(void *this_N_all)
{
    (void)this_N_all;
}

static struct s_module_info esp_modules[1] = {
    { "network.ovl", &esp_meth }
};

size_t module_info(MODULINF *p_info)
{
    if (p_info)
        *p_info = esp_modules;
    return 1;
}
