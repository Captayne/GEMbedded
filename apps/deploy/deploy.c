/*
 * deploy.c - receive a program over the USB console (pTOS3000)
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Nothing reaches the machine unless this program is running: it takes
 * the USB console over (the _UCN cookie, pTOS include/usbcon.h), waits
 * for a transfer, writes it to F:\ and, if "run after upload" is ticked,
 * starts it.  Quitting hands the console back to the keyboard.
 *
 * The transfer, sent by tools/ptosdeploy.py:
 *
 *      "PTUP1"   5 bytes
 *      flags     2 bytes         bit 0: the sender would like it run
 *      namelen   2 bytes
 *      name      namelen bytes   e.g. "SKETCH.PRG"
 *      size      4 bytes
 *      crc32     4 bytes         of the data that follows
 *      data      size bytes
 *
 * all numbers little-endian.  The answer is a line of text, "+ ..." when
 * it worked and "- ..." when it did not.
 */

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include <string.h>
#include "usbcon.h"
#include "gem.h"

#define DEST_DRIVE      "F:\\"
#define CHUNK           512
#define TIMEOUT_TICKS   1000            /* 5 s, in 200 Hz ticks */

static struct ucn_api *ucn;
static char name[64];
static char path[80];
static unsigned char buf[CHUNK];

/* ---- the screen ---- */

enum { O_ROOT, O_TITLE, O_STATUS, O_RUN, O_QUIT, O_NUM };
static OBJECT tree[O_NUM];
static char status[64];
static short scr_w = 320, scr_h = 240;
static short run_after = 1;

static void set_obj(short i, short next, short head, short tail, unsigned short type,
                    long spec, short x, short y, short w, short h)
{
    OBJECT *o = &tree[i];

    o->ob_next = next;
    o->ob_head = head;
    o->ob_tail = tail;
    o->ob_type = type;
    o->ob_flags = 0;
    o->ob_state = 0;
    o->ob_spec = spec;
    o->ob_x = x;
    o->ob_y = y;
    o->ob_width = w;
    o->ob_height = h;
}

static void build(void)
{
    set_obj(O_ROOT, -1, O_TITLE, O_QUIT, G_BOX, 0x00021100L,
            0, 0, scr_w, scr_h);
    set_obj(O_TITLE, O_STATUS, -1, -1, G_STRING, (long)"pTOS3000 deploy",
            16, 16, 8 * 15, 16);
    set_obj(O_STATUS, O_RUN, -1, -1, G_STRING, (long)status,
            16, 48, scr_w - 32, 16);
    set_obj(O_RUN, O_QUIT, -1, -1, G_BUTTON, (long)"Run after upload",
            16, scr_h - 80, 8 * 18, 20);
    set_obj(O_QUIT, O_ROOT, -1, -1, G_BUTTON, (long)"Quit",
            16, scr_h - 48, 8 * 8, 20);
    tree[O_QUIT].ob_flags = LASTOB | SELECTABLE | EXIT;
    tree[O_RUN].ob_flags = SELECTABLE;
    tree[O_RUN].ob_state = run_after ? SELECTED : 0;
}

static void show(const char *text)
{
    strcpy(status, text);
    objc_draw(tree, O_ROOT, 8, 0, 0, scr_w, scr_h);
}

/* ---- the transfer ---- */

static unsigned long crc32_update(unsigned long crc, const unsigned char *p, long n)
{
    long i;
    int k;

    for (i = 0; i < n; i++)
    {
        crc ^= p[i];
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xedb88320UL & -(long)(crc & 1));
    }
    return crc;
}

/* read exactly n bytes, or fail after TIMEOUT_TICKS without any */
static int recv(void *dest, long n)
{
    unsigned char *p = dest;
    long got = 0;
    long deadline = (long)Ssystem(S_GETLVAL, 0x4ba, 0) + TIMEOUT_TICKS;

    while (got < n)
    {
        long r = ucn->read(p + got, n - got);

        if (r > 0)
        {
            got += r;
            deadline = (long)Ssystem(S_GETLVAL, 0x4ba, 0) + TIMEOUT_TICKS;
        }
        else if ((long)Ssystem(S_GETLVAL, 0x4ba, 0) > deadline)
            return -1;
    }
    return 0;
}

static void reply(const char *s)
{
    ucn->write(s, strlen(s));
    ucn->write("\r\n", 2);
}

static short recv_u16(int *err)
{
    unsigned char b[2];

    if (recv(b, 2) < 0)
        *err = 1;
    return (short)(b[0] | (b[1] << 8));
}

static long recv_u32(int *err)
{
    unsigned char b[4];

    if (recv(b, 4) < 0)
        *err = 1;
    return (long)b[0] | ((long)b[1] << 8) | ((long)b[2] << 16) | ((long)b[3] << 24);
}

/* the header is announced by "PTUP1"; returns 1 when a file arrived */
static int transfer(void)
{
    unsigned long crc = 0xffffffffUL, want;
    long size, done;
    short flags, namelen;
    int err = 0;
    long fh;

    flags = recv_u16(&err);
    namelen = recv_u16(&err);
    if (err || namelen <= 0 || namelen >= (short)sizeof(name))
    {
        reply("- bad header");
        return 0;
    }
    if (recv(name, namelen) < 0)
    {
        reply("- truncated name");
        return 0;
    }
    name[namelen] = '\0';
    size = recv_u32(&err);
    want = (unsigned long)recv_u32(&err);
    if (err || size < 0)
    {
        reply("- bad header");
        return 0;
    }

    strcpy(path, DEST_DRIVE);
    strcat(path, name);
    show(path);

    fh = Fcreate(path, 0);
    if (fh < 0)
    {
        reply("- cannot create the file");
        return 0;
    }

    for (done = 0; done < size; )
    {
        long n = size - done;

        if (n > CHUNK)
            n = CHUNK;
        if (recv(buf, n) < 0)
        {
            Fclose((short)fh);
            reply("- transfer broke off");
            return 0;
        }
        if (Fwrite((short)fh, n, buf) != n)
        {
            Fclose((short)fh);
            reply("- cannot write (disk full?)");
            return 0;
        }
        crc = crc32_update(crc, buf, n);
        done += n;
    }
    Fclose((short)fh);
    crc ^= 0xffffffffUL;

    if (crc != want)
    {
        Fdelete(path);
        reply("- checksum mismatch");
        return 0;
    }

    reply("+ ok");
    run_after = (tree[O_RUN].ob_state & SELECTED) != 0;
    return (flags & 1) && run_after;
}

/* ---- main ---- */

void start_main(BASEPAGE *bp);

void start_main(BASEPAGE *bp)
{
    short msg[8], mx, my, button, kstate, kret, bret;
    short which;
    unsigned char c;

    Mshrink(bp, sizeof(BASEPAGE) + bp->p_tlen + bp->p_dlen + bp->p_blen);
    appl_init();

    if (Ssystem(S_GETCOOKIE, UCN_COOKIE, (long)&ucn) != 0 || !ucn
        || ucn->version < UCN_VERSION)
    {
        form_alert(1, "[3][No USB console|(_UCN cookie missing).][ OK ]");
        appl_exit();
        Pterm0();
    }

    build();
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, 0);
    show("Waiting for an upload...");
    graf_mouse(M_ON, 0);

    ucn->set_raw(1);

    for (;;)
    {
        which = evnt_multi_button_timer(50, &mx, &my, &button, &kstate,
                                        &kret, &bret, msg);
        if (which & MU_BUTTON)
        {
            short obj = objc_find(tree, O_ROOT, 8, mx, my);

            if (obj == O_QUIT)
                break;
            if (obj == O_RUN)
            {
                tree[O_RUN].ob_state ^= SELECTED;
                objc_draw(tree, O_RUN, 1, 0, 0, scr_w, scr_h);
            }
        }

        /* "PTUP1" announces a transfer; everything else is skipped, so
         * that leftovers of a broken transfer do not confuse us */
        while (ucn->status() > 0)
        {
            static const char magic[] = "PTUP1";
            static int match;

            if (ucn->read(&c, 1) != 1)
                break;
            if (c == (unsigned char)magic[match])
            {
                if (++match < 5)
                    continue;
                match = 0;
                show("Receiving...");
                if (transfer())
                {
                    ucn->set_raw(0);
                    graf_mouse(M_ON, 0);
                    wind_update(END_UPDATE);
                    appl_exit();
                    Pexec(0, path, "", 0L);     /* run it */
                    Pterm0();
                }
                show("Waiting for an upload...");
            }
            else
                match = (c == (unsigned char)magic[0]) ? 1 : 0;
        }
    }

    ucn->set_raw(0);
    wind_update(END_UPDATE);
    appl_exit();
    Pterm0();
}
