/*
 * dummynet.c - a network that answers, so that the browser can be started
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * WHAT THIS IS FOR
 *
 * HighWire compiles and links, and the next question is whether it runs:
 * whether the AES and VDI underneath it are enough, whether the resource
 * loads, whether the renderer draws.  None of that needs a real network,
 * but all of it needs the browser to get past "cannot connect" and have
 * something to lay out.
 *
 * So this answers every request with the same page.  It is nonsense on
 * purpose -- nobody should mistake it for a connection -- but it is
 * well-formed nonsense, with headers, a status line and HTML the renderer
 * has to work through.
 *
 * WHY IT REPLACES ovl_sys.c
 *
 * HighWire expects its network layer in a loadable overlay: load_ovl()
 * finds network.ovl, starts it with Pexec and asks it for a function
 * table.  This machine has no loader for Atari overlays, so the module is
 * built in instead and load_ovl() hands it over without touching a disk.
 * Everything above that -- the function table, the eight calls, the
 * version handshake -- is unchanged, which is the point: when the real
 * module arrives it fills in the same eight functions.
 *
 * THE SHAPE THE REAL ONE WILL HAVE
 *
 *     host_addr   AT+CIPDOMAIN       name -> address
 *     connect     AT+CIPSTART        address, port -> handle
 *     send        AT+CIPSEND         the request
 *     recv        +IPD               what came back
 *     close       AT+CIPCLOSE
 *     instat                         how much is waiting
 *     select                         wait until something is
 *     info                           a line for the About page
 *
 * Nothing else about HighWire has to change for that.
 */

#include <stddef.h>
#include <string.h>

#include "hw-types.h"
#include "inet.h"
#include "ovl_sys.h"

/* ==== the page everybody gets ========================================= */

/*
 * One canned response.  The status line and headers are real, because the
 * browser parses them before it looks at the body: a Content-Type it does
 * not recognise and it will not render at all.
 */
static const char canned[] =
    "HTTP/1.0 200 OK\r\n"
    "Server: GEMbedded dummynet\r\n"
    "Content-Type: text/html; charset=iso-8859-1\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><head><title>GEMbedded</title></head>\n"
    "<body bgcolor=\"#ffffff\" text=\"#000000\">\n"
    "<h1>It renders.</h1>\n"
    "<p>This page did not come from anywhere. There is no network yet:\n"
    "every address answers with this, out of\n"
    "<tt>dummynet.c</tt>.</p>\n"
    "<p>What it proves is the rest of the chain &mdash; that the program\n"
    "started, that the AES gave it a window, that the resource loaded,\n"
    "and that the renderer walked this markup and drew it.</p>\n"
    "<h2>Things worth looking at</h2>\n"
    "<ul>\n"
    "<li>Is the <b>bold</b> bold, the <i>italic</i> italic?</li>\n"
    "<li>Does the list indent?</li>\n"
    "<li>Does the window redraw when you drag another one over it?</li>\n"
    "<li>Does it reflow when you size the window?</li>\n"
    "</ul>\n"
    "<table border=\"1\"><tr><th>column</th><th>column</th></tr>\n"
    "<tr><td>a table, because tables are</td><td>where layout goes wrong"
    "</td></tr></table>\n"
    "<p><a href=\"http://example.invalid/next\">A link.</a> It leads back\n"
    "here, like everything else.</p>\n"
    "</body></html>\n";

/* ==== one connection at a time ======================================== */

/*
 * A handle is an index into this, plus one, so that zero is never a valid
 * handle.  One is enough: the browser opens a connection, reads it and
 * closes it, and there is nothing here that would benefit from more.
 */
#define MAX_CONN    4

static struct {
    BOOL    open;
    size_t  read;               /* how much of canned[] has gone out */
} conn[MAX_CONN];

static long handle_of(int i)    { return (long)(i + 1); }
static int  index_of(long fh)   { return (int)(fh - 1); }

static BOOL valid(long fh)
{
    int i = index_of(fh);
    return (BOOL)(i >= 0 && i < MAX_CONN && conn[i].open);
}

/* ==== the eight calls ================================================= */

/*
 * Every name resolves, to an address that means nothing.  A browser that
 * cannot resolve a name never gets as far as connecting, and then none of
 * what this is for gets tested.
 */
static short __CDECL dn_host_addr(const char *host, long *addr)
{
    (void)host;
    if (addr)
        *addr = 0x7f000001L;    /* 127.0.0.1, which is at least honest */
    return 0;                   /* E_OK */
}

static long __CDECL dn_connect(long addr, long port, long tout_sec)
{
    int i;

    (void)addr; (void)port; (void)tout_sec;

    for (i = 0; i < MAX_CONN; i++) {
        if (!conn[i].open) {
            conn[i].open = TRUE;
            conn[i].read = 0;
            return handle_of(i);
        }
    }
    return -35;                 /* EMFILE: no handle left */
}

/*
 * The request goes nowhere, and saying so would be a lie: a browser that
 * cannot send does not wait for an answer.  So this accepts all of it.
 */
static long __CDECL dn_send(long fh, const char *buf, size_t len)
{
    (void)buf;
    if (!valid(fh))
        return -37;             /* EBADF */
    return (long)len;
}

/*
 * The page, handed out in whatever pieces the caller asks for, and then
 * end of file.  Reading past the end returns 0, which is how a closed
 * connection reads.
 */
static long __CDECL dn_recv(long fh, char *buf, size_t len)
{
    size_t left;
    int i;

    if (!valid(fh))
        return -37;

    i = index_of(fh);
    left = sizeof(canned) - 1 - conn[i].read;
    if (left == 0)
        return 0;
    if (len > left)
        len = left;

    memcpy(buf, canned + conn[i].read, len);
    conn[i].read += len;

    return (long)len;
}

static void __CDECL dn_close(long fh)
{
    if (valid(fh))
        conn[index_of(fh)].open = FALSE;
}

/* How much is waiting.  All of what is left, immediately. */
static long __CDECL dn_instat(long fh)
{
    if (!valid(fh))
        return -37;
    return (long)(sizeof(canned) - 1 - conn[index_of(fh)].read);
}

/*
 * Wait until something can be read.  There is never anything to wait for
 * here, so this says "ready" at once and hands back the set it was given.
 * A real one would wait for +IPD from the module, or for the timeout.
 */
static long __CDECL dn_select(long timeout, long *rfds, long *wfds)
{
    (void)timeout;
    return (long)((rfds && *rfds ? 1 : 0) + (wfds && *wfds ? 1 : 0));
}

static const char * __CDECL dn_info(void)
{
    return "GEMbedded dummynet -- every address answers the same page";
}

/* ==== the module ====================================================== */

static INET_FTAB dummy_ftab = {
    dn_host_addr,
    dn_connect,
    dn_send,
    dn_recv,
    dn_close,
    dn_instat,
    dn_select,
    dn_info
};

static struct ovl_info_t dummy_info = {
    "dummynet",
    "0.1",
    "2026-09-28",
    "GEMbedded"
};

static long __CDECL dn_ovl_init(void)
{
    memset(conn, 0, sizeof(conn));
    return INET_VERSION;        /* the handshake HighWire checks */
}

static struct ovl_info_t * __CDECL dn_ovl_version(void)
{
    return &dummy_info;
}

static void * __CDECL dn_ovl_getftab(void)
{
    return &dummy_ftab;
}

static long __CDECL dn_ovl_free(void)
{
    return 0;
}

static long dummy_magic = OVL_MAGIC;

static OVL_METH dummy_meth = {
    OVL_MAGIC,
    OVL_REVISION,
    OF_SIMPLE,
    FTAB_NETWORK,
    dn_ovl_init,
    dn_ovl_version,
    dn_ovl_getftab,
    dn_ovl_free,
    &dummy_magic
};

/* ==== what ovl_sys.c used to do ======================================= */

/*
 * There is no Pexec here and no overlay on a disk.  A request for
 * "network.ovl" is answered with the module above; anything else is
 * refused, which is what load_ovl() does for a file it cannot find.
 */
OVL_METH *load_ovl(const char *ovl_name, void (*handler)(void *))
{
    (void)handler;

    if (ovl_name && strcmp(ovl_name, "network.ovl") == 0)
        return &dummy_meth;

    return NULL;
}

void kill_ovl(void *this_N_all)
{
    (void)this_N_all;           /* nothing was loaded, nothing to free */
}

static struct s_module_info dummy_modules[1] = {
    { "network.ovl", &dummy_meth }
};

size_t module_info(MODULINF *p_info)
{
    if (p_info)
        *p_info = dummy_modules;
    return 1;
}
