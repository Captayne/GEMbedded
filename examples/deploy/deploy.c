/*
 * deploy.c - receive a program over the USB console (GEMbedded)
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Nothing reaches the machine unless this is running: it takes the USB
 * console over (the _UCN cookie, pTOS include/usbcon.h), waits for a
 * transfer, writes it to F:\ and starts it.
 *
 * The same file serves twice, and the basepage says which it is:
 *
 *   DEPLOY.PRG   as a program, full screen, with a Quit button.  It ends
 *                when it starts what it received, so every upload needs
 *                it started again.
 *   DEPLOY.ACC   as a desk accessory in C:\, loaded at boot.  It is an
 *                AES process of its own -- one of the kernel's tasks --
 *                and so it keeps listening while the desktop or a
 *                program is running.  Upload as often as you like.
 *                What arrives is handed to the shell (shel_write), and
 *                whatever runs now makes way for it.
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

#define DEST_DRIVE      "F:\\"        /* programs */
#define ACC_DRIVE       "C:\\"        /* accessories: the boot drive */
#define CMDTAILSIZE     128             /* what GEM hands a program */
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
    set_obj(O_TITLE, O_STATUS, -1, -1, G_STRING, (long)"GEMbedded deploy",
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

static int is_acc;              /* started as a desk accessory */

static void show(const char *text)
{
    strcpy(status, text);
    if (!is_acc)                /* the accessory has no screen of its own */
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

    /* An accessory only counts when it is on the boot drive, so that is
       where it goes -- upload, restart, and it is loaded. */
    strcpy(path, (namelen > 4 && strcmp(name + namelen - 4, ".ACC") == 0)
                 ? ACC_DRIVE : DEST_DRIVE);
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
    if (!is_acc)
        run_after = (tree[O_RUN].ob_state & SELECTED) != 0;
    return (flags & 1) && run_after;
}

/* ---- looking at the console ---- */

/*
 * "PTUP1" announces a transfer; anything else is skipped, so that the
 * leftovers of a broken one do not confuse us.  Returns 1 when a program
 * arrived that is meant to be started.
 */
static int poll_console(void)
{
    static int match;
    unsigned char c;

    while (ucn->status() > 0)
    {
        if (ucn->read(&c, 1) != 1)
            return 0;
        if (c == (unsigned char)"PTUP1"[match])
        {
            if (++match < 5)
                continue;
            match = 0;
            show("Receiving...");
            if (transfer())
                return 1;
            show("Waiting for an upload...");
        }
        else
            match = (c == (unsigned char)'P') ? 1 : 0;
    }
    return 0;
}


/* ---- as a desk accessory ---- */

/*
 * It never ends.  Between uploads it sleeps in evnt_multi() like any
 * other process and costs nothing; the kernel gives it its share when
 * something arrives.
 */
/*
 * The menu entry is our own string: mn_register() keeps the pointer, the
 * way Atari TOS does, so writing into it renames the entry.  The desktop
 * draws it when the menu is pulled down, and that is how the machine
 * itself shows that Deploy is listening -- and how much it has taken.
 */
static char acc_title[24];

static void set_title(const char *state)
{
    strcpy(acc_title, "  Deploy ");
    strcat(acc_title, state);
}

static void acc_main(void)
{
    short msg[8], menu_id, apid;
    char  tail[CMDTAILSIZE];
    long  count = 0;

    apid = appl_init();
    set_title("[waiting]");
    menu_id = menu_register(apid, acc_title);

    if (Ssystem(S_GETCOOKIE, UCN_COOKIE, (long)&ucn) != 0 || !ucn
        || ucn->version < UCN_VERSION)
    {
        set_title("[no console]");
        for (;;)                /* an accessory must not end */
            evnt_multi_mesag_timer(1000, msg);
    }

    ucn->set_raw(1);            /* ours for good: nobody else listens */
    strcpy(status, "Waiting for an upload...");

    for (;;)
    {
        short ev = evnt_multi_mesag_timer(50, msg);

        if ((ev & MU_MESAG) && msg[0] == AC_OPEN && msg[4] == menu_id)
        {
            char text[128];

            strcpy(text, "[0][GEMbedded deploy|");
            strcat(text, status);
            strcat(text, "][ OK ]");
            form_alert(1, text);
        }

        if (poll_console())
        {
            /*
             * Hand it to the shell.  It starts when what is running now
             * ends -- and the desktop of pTOS ends for us when it sees
             * that somebody asked.  The message afterwards is only to
             * wake it from its wait.
             */
            count++;
            {   /* "[3]": three programs taken since the machine started */
                char n[8];
                int  i = 0, d = (int)(count % 100);

                n[i++] = '[';
                if (d >= 10)
                    n[i++] = (char)('0' + d / 10);
                n[i++] = (char)('0' + d % 10);
                n[i++] = ']';
                n[i] = '\0';
                set_title(n);
            }
            tail[0] = '\0';
            shel_write(SHW_EXEC, 1, 0, path, tail);

            msg[0] = 0;         /* nothing the desktop knows: just a nudge */
            msg[1] = apid;
            msg[2] = 0;
            appl_write(0, 16, msg);

            strcpy(status, "Waiting for an upload...");
        }
    }
}


/* ---- as a program ---- */

static void app_main(BASEPAGE *bp)
{
    short msg[8], mx, my, button, kstate, kret, bret;
    short which;

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

        if (poll_console())
        {
            ucn->set_raw(0);
            graf_mouse(M_ON, 0);
            wind_update(END_UPDATE);
            appl_exit();
            Pexec(0, path, "", 0L);     /* run it, and end with it */
            Pterm0();
        }
    }

    ucn->set_raw(0);
    wind_update(END_UPDATE);
    appl_exit();
    Pterm0();
}


/* ---- which of the two ---- */

void start_main(BASEPAGE *bp);

void start_main(BASEPAGE *bp)
{
    /* An accessory is loaded by the AES itself and has no parent. */
    is_acc = (bp->p_parent == 0);

    if (is_acc)
        acc_main();             /* never returns */
    else
        app_main(bp);
}
