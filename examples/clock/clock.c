/*
 * clock.c - one program, two halves: a clock
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * The real-time half is a cyclic task on the real-time core.  It is
 * entered once a second, counts, and says so.  It draws nothing and
 * knows nothing about clocks: it keeps time.
 *
 * The GEM half draws the dial and the hand, and waits.  It does not
 * poll, and it does not count: it is told.  Between the two lies a
 * message, delivered as an ordinary AES message, so the GEM half waits
 * in evnt_multi() exactly as it would for a keystroke.
 *
 * That is the whole point of the arrangement.  The half that must be
 * punctual has no operating system to be delayed by, and the half that
 * draws has nothing to be punctual about.
 *
 * It runs until Cancel is touched.
 *
 * Without the kernel -- no "_IRK" cookie -- the clock still runs, from
 * the AES timer.  It is then as punctual as a desktop can be, which is
 * the point of comparison.
 */

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include "gem.h"
#include "irk.h"

/* ---- the screen ------------------------------------------------- */

#define CX          160         /* centre of the dial */
#define CY          104
#define R_RIM        88         /* the rim */
#define R_TICK       80         /* where the minute ticks begin */
#define R_TICK5      72         /* the five-second ones are longer */
#define R_HAND       76         /* tip of the second hand */

#define WHITE         0
#define BLACK         1

#define BTN_X       120         /* the Cancel button */
#define BTN_Y       206
#define BTN_W        80
#define BTN_H        26

/* sin(n * 6 degrees) * 1024.  cos is the same table 15 steps on, so one
   table does for both -- a quarter turn is exactly 15 of these steps. */
static const short sintab[60] = {
        0,   107,   213,   316,   416,   512,   602,   685,   761,   828,
      887,   935,   974,  1002,  1018,  1024,  1018,  1002,   974,   935,
      887,   828,   761,   685,   602,   512,   416,   316,   213,   107,
        0,  -107,  -213,  -316,  -416,  -512,  -602,  -685,  -761,  -828,
     -887,  -935,  -974, -1002, -1018, -1024, -1018, -1002,  -974,  -935,
     -887,  -828,  -761,  -685,  -602,  -512,  -416,  -316,  -213,  -107
};

#define SIN(n)  (sintab[(n) % 60])
#define COS(n)  (sintab[((n) + 15) % 60])

static short vdi;               /* our virtual workstation */

/* A point on the dial: second `s`, distance `r` from the centre.  The
   screen counts y downwards, so the cosine is subtracted. */
static void point_at(short s, short r, short *x, short *y)
{
    *x = (short)(CX + ((long)r * SIN(s)) / 1024);
    *y = (short)(CY - ((long)r * COS(s)) / 1024);
}

static void line(short x1, short y1, short x2, short y2)
{
    short xy[4];

    xy[0] = x1; xy[1] = y1; xy[2] = x2; xy[3] = y2;
    v_pline(vdi, 2, xy);
}

static void box(short x, short y, short w, short h, short colour)
{
    short xy[4];

    xy[0] = x; xy[1] = y; xy[2] = (short)(x + w - 1); xy[3] = (short)(y + h - 1);
    vsf_color(vdi, colour);
    vsf_interior(vdi, FIS_SOLID);
    vr_recfl(vdi, xy);
}

static void draw_dial(void)
{
    short s, x1, y1, x2, y2;

    box(0, 0, 320, 240, WHITE);

    vsl_color(vdi, BLACK);

    /* the rim, as sixty short chords -- there is no circle in the VDI
       bindings, and at this size the joins do not show */
    for (s = 0; s < 60; s++)
    {
        point_at(s, R_RIM, &x1, &y1);
        point_at((short)(s + 1), R_RIM, &x2, &y2);
        line(x1, y1, x2, y2);
    }

    /* the ticks: every second short, every fifth long */
    for (s = 0; s < 60; s++)
    {
        point_at(s, (short)((s % 5) ? R_TICK : R_TICK5), &x1, &y1);
        point_at(s, R_RIM, &x2, &y2);
        line(x1, y1, x2, y2);
    }

    /* the button */
    box(BTN_X, BTN_Y, BTN_W, BTN_H, WHITE);
    vsl_color(vdi, BLACK);
    line(BTN_X, BTN_Y, (short)(BTN_X + BTN_W - 1), BTN_Y);
    line((short)(BTN_X + BTN_W - 1), BTN_Y,
         (short)(BTN_X + BTN_W - 1), (short)(BTN_Y + BTN_H - 1));
    line((short)(BTN_X + BTN_W - 1), (short)(BTN_Y + BTN_H - 1),
         BTN_X, (short)(BTN_Y + BTN_H - 1));
    line(BTN_X, (short)(BTN_Y + BTN_H - 1), BTN_X, BTN_Y);
    vst_color(vdi, BLACK);
    v_gtext(vdi, (short)(BTN_X + 20), (short)(BTN_Y + 18), "Cancel");
}

static void draw_hand(short s, short colour)
{
    short x, y;

    vsl_color(vdi, colour);
    point_at(s, R_HAND, &x, &y);
    line(CX, CY, x, y);
}

/* The line below the dial, saying where the seconds come from. */
static void draw_source(const char *text)
{
    box(0, BTN_Y - 22, 320, 16, WHITE);
    vst_color(vdi, BLACK);
    v_gtext(vdi, 40, (short)(BTN_Y - 10), text);
}


/* ---- the real-time half ----------------------------------------- */
/*
 * Runs on the real-time core, entered once a second.  No GEMDOS, no
 * BIOS, no AES: it counts, and it tells.
 */

static struct irk_api *k;       /* the kernel, as this core sees it */
static struct irk_api *rt;      /* as the other core sees it */

static volatile unsigned long seconds;

static void tick(void *arg)
{
    (void)arg;
    seconds++;
    rt->notify(seconds, 0);     /* the GEM half is waiting for this */
}


/* ---- the GEM half ------------------------------------------------ */

static int touched_cancel(void)
{
    short mx, my, mstate, kstate;

    graf_mkstate(&mx, &my, &mstate, &kstate);
    return (mstate & 1)
        && mx >= BTN_X && mx < BTN_X + BTN_W
        && my >= BTN_Y && my < BTN_Y + BTN_H;
}

int main(void)
{
    short wchar, hchar, wbox, hbox;
    short apid, shown = 0;
    long  value = 0;
    irk_handle t = IRK_NONE;

    apid = appl_init();
    if (apid < 0)
        return 1;

    vdi = v_opnvwk(graf_handle(&wchar, &hchar, &wbox, &hbox));
    if (!vdi)
    {
        appl_exit();
        return 1;
    }

    graf_mouse(256, 0L);        /* M_OFF: the hand is ours to draw */
    draw_dial();

    /* Is there a kernel?  No cookie means no second half -- and the
       clock then keeps its own time, from the AES timer. */
    if (Ssystem(S_GETCOOKIE, IRK_COOKIE, (long)&value) == 0 && value)
        k = (struct irk_api *)value;

    if (k && k->cores() > 1)
    {
        rt = k->rt_api();
        k->notify_to((unsigned short)apid);

        /* Born suspended, so that `rt` is set before it runs. */
        t = k->task_new(IRK_CORE_RT, tick, 0, 100, 0, 1024, 0);
        if (t != IRK_NONE)
        {
            k->set_cyclic(t, 1000000UL, 0);     /* once a second */
            k->task_resume(t);
            draw_source("seconds from the real-time core");
        }
    }
    if (t == IRK_NONE)
        draw_source("seconds from the AES timer");

    draw_hand(0, BLACK);

    for (;;)
    {
        short msg[8], mx, my, button, kstate, key, clicks;
        short ev = evnt_multi_button_timer(200, &mx, &my, &button,
                                           &kstate, &key, &clicks, msg);

        if ((ev & MU_MESAG) && msg[0] == IRK_MSG)
        {
            /* The second half has counted.  The value is in the
               message; we do not keep our own. */
            unsigned long n = ((unsigned long)(unsigned short)msg[4] << 16)
                              | (unsigned short)msg[5];
            draw_hand(shown, WHITE);
            shown = (short)(n % 60);
            draw_hand(shown, BLACK);
        }
        else if (t == IRK_NONE && (ev & MU_TIMER))
        {
            /* No kernel: count here, five ticks to the second. */
            static short ticks;

            if (++ticks >= 5)
            {
                ticks = 0;
                draw_hand(shown, WHITE);
                shown = (short)((shown + 1) % 60);
                draw_hand(shown, BLACK);
            }
        }

        if (touched_cancel())
            break;
    }

    /* The second half dies with its program, and not a moment later:
       its stack and its code are this program's memory. */
    if (t != IRK_NONE)
        k->task_kill(t);

    graf_mouse(257, 0L);        /* M_ON */
    v_clsvwk(vdi);
    appl_exit();
    return 0;
}
