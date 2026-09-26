/*
 * core0lat.c - how long can core 0 be away?
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * pTOS gives the processor away at the end of every AES call, and
 * IRKernel hands it to whoever is furthest behind.  That makes the
 * question "how long does another task have to wait" answerable, and this
 * measures it instead of arguing about it.
 *
 * The trick is who does the measuring.  The GEM half cannot: it would
 * only see the gaps it happens to look at.  So a cyclic task on the
 * real-time core -- which nothing on this machine can delay -- watches a
 * timestamp that the GEM half refreshes as often as it can, and keeps the
 * worst gap it ever sees.
 *
 * Three rounds, because the answer depends on what core 0 is doing:
 *
 *   1. waiting          evnt_multi(), the ordinary state of a program
 *   2. drawing          VDI calls in a loop, which do not yield
 *   3. writing to F:    the flash, which cannot be interrupted at all
 *
 * The third is the one that matters: a flash sector erase takes some 45
 * milliseconds and nothing can shorten it.  That is the floor for
 * anything on core 0 -- and the reason real-time work belongs on core 1.
 */

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <string.h>
#include "gem.h"
#include "irk.h"

#define ROUND_MS    3000            /* how long each round lasts */

/* What the two halves share.  One writer each, so no lock is needed. */
static volatile unsigned long alive_us;     /* the GEM half was here */
static volatile unsigned long worst_us;     /* the longest gap core 1 saw */
static volatile unsigned long gaps;         /* gaps over a millisecond */
static volatile unsigned char watching;

static struct irk_api *k;
static struct irk_api *rt;

/* Runs on the real-time core, every millisecond. */
static void watcher(void *arg)
{
    unsigned long now, gap;

    (void)arg;

    if (!watching)
        return;

    now = (unsigned long)rt->now_us();
    gap = now - alive_us;
    if (gap > worst_us)
        worst_us = gap;
    if (gap > 1000)
        gaps++;
}

/* ---- the GEM half ---- */

static short vdi;
static char msg[256];

static void add(const char *s) { strcat(msg, s); }

static void add_num(unsigned long v)
{
    char n[12];
    int i = 0, j;

    do { n[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (j = i - 1; j >= 0; j--)
    {
        char one[2];
        one[0] = n[j]; one[1] = 0;
        add(one);
    }
}

static long ticks(void) { return (long)Ssystem(S_GETLVAL, 0x4baL, 0L); }

/* "I am here": the timestamp core 1 watches */
static void alive(void)
{
    alive_us = (unsigned long)k->now_us();
}

static void start_round(void)
{
    alive();
    worst_us = 0;
    gaps = 0;
    watching = 1;
}

static void end_round(const char *what)
{
    watching = 0;
    msg[0] = 0;
    add("[0][");
    add(what);
    add(" ");
    add_num(worst_us);
    add(" us|over 1ms: ");
    add_num(gaps);
    add("][ OK ]");
    form_alert(1, msg);
}

/* 1: the ordinary state of a program -- waiting for something to happen */
static void round_waiting(void)
{
    long until = ticks() + ROUND_MS / 5;
    short msgbuf[8];

    start_round();
    while (ticks() < until)
    {
        alive();
        (void)msgbuf;
        appl_yield();                   /* give way, come back at once */
    }
    end_round("yielding:");
}

/* 2: drawing.  VDI calls do not give the processor away. */
static void round_drawing(void)
{
    long until = ticks() + ROUND_MS / 5;
    short xy[4];
    short y = 0;

    start_round();
    while (ticks() < until)
    {
        xy[0] = 0; xy[1] = y; xy[2] = 319; xy[3] = y;
        vsl_color(vdi, 1);
        v_pline(vdi, 2, xy);
        alive();                        /* after every single line */
        if (++y >= 240)
            y = 0;
    }
    end_round("one VDI line:");
}

/* 3: the flash.  A sector erase cannot be interrupted or shortened. */
static void round_flash(void)
{
    long until = ticks() + ROUND_MS / 5;
    static unsigned char block[512];
    long fh;
    int n = 0;

    memset(block, 'x', sizeof(block));
    fh = Fcreate("F:\\LATENCY.TMP", 0);
    if (fh < 0)
    {
        form_alert(1, "[1][No file on F:.][ OK ]");
        return;
    }

    start_round();
    while (ticks() < until)
    {
        alive();
        if (Fwrite((short)fh, (long)sizeof(block), block) != (long)sizeof(block))
        {
            watching = 0;
            form_alert(1, "[1][F: would not take it.][ OK ]");
            break;
        }
        if (++n >= 128)                 /* 64 KB, then round again */
        {
            n = 0;
            Fseek(0L, (short)fh, 0);
        }
    }
    if (watching)
        end_round("flash write:");

    Fclose((short)fh);
    Fdelete("F:\\LATENCY.TMP");
}

int main(void)
{
    short wchar, hchar, wbox, hbox, apid;
    long value = 0;
    irk_handle w = IRK_NONE;

    apid = appl_init();
    if (apid < 0)
        return 1;

    if (Ssystem(S_GETCOOKIE, IRK_COOKIE, (long)&value) != 0 || !value)
    {
        form_alert(1, "[1][No kernel to measure with.][ OK ]");
        appl_exit();
        return 1;
    }
    k = (struct irk_api *)value;
    if (k->cores() < 2)
    {
        form_alert(1, "[1][Only one core here.][ OK ]");
        appl_exit();
        return 1;
    }
    rt = k->rt_api();

    vdi = v_opnvwk(graf_handle(&wchar, &hchar, &wbox, &hbox));

    w = k->task_new(IRK_CORE_RT, watcher, 0, 200, 0, 1024, 0);
    if (w == IRK_NONE)
    {
        form_alert(1, "[1][No task on core 1.][ OK ]");
        v_clsvwk(vdi);
        appl_exit();
        return 1;
    }
    k->set_cyclic(w, 1000, 0);          /* every millisecond */
    k->task_resume(w);

    round_waiting();
    round_drawing();
    round_flash();

    k->task_kill(w);

    v_clsvwk(vdi);
    appl_exit();
    return 0;
}
