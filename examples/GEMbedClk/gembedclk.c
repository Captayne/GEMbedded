/*
 * gembedclk.c - GEMbedClk: the two-halved clock, in a window
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * The same two halves as examples/clock: a cyclic task on the real-time
 * core counts the seconds and says so, another turns a cube twenty times
 * a second; the GEM half draws.  But the GEM half now behaves like any
 * other program on the desktop: it lives in a window that can be moved,
 * sized, filled out to the whole screen, sent to the back and closed,
 * and it draws only where its window can be seen -- through the
 * rectangle list, as GEM wants it.
 *
 * Everything scales with the window: the dial takes the largest circle
 * that fits, the cube sits in its middle.
 *
 * Built with NCLOCKS=2 it is DUALCLK.PRG: two windows, two clocks, four
 * tasks on the real-time core -- the second cube turning the other way,
 * the second clock ticking half a second after the first.
 */

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include "gem.h"
#include <math.h>
#include "mathglue.h"
#include "irk.h"

#ifndef NCLOCKS
#define NCLOCKS       1
#endif

#define WHITE         0
#define BLACK         1

#define KIND        (NAME | CLOSER | FULLER | MOVER | SIZER)
#define MIN_WORK     64         /* the smallest work area, either way */

#define CUBE_R       20         /* the cube, as the real-time half sees it */
#define CUBE_D      160

struct clock
{
    short win;                  /* its window, -1 once closed */
    char  title[20];

    short wx, wy, ww, wh;       /* the work area, in screen coordinates */
    short cx, cy, rim;          /* centre and radius of the dial */
    short shown;                /* the second the hand points at */

    irk_handle tick, spin;      /* its two tasks on the real-time core */
    volatile unsigned long seconds;

    /* Two buffers: the real-time half fills the one that is not being
       read, then says which is newest.  One writer, one reader. */
    volatile struct {
        float x[8], y[8];
    } frame[2];
    volatile unsigned char newest;
    float ax, ay, dax, day;     /* the cube's angles and how they turn */
};

static struct clock clocks[NCLOCKS];
static struct clock *cur;       /* the one being drawn */

static short vdi;               /* our virtual workstation */
static struct irk_api *k;       /* the kernel, as this core sees it */
static struct irk_api *rt;      /* as the other core sees it */

static void layout(struct clock *c)
{
    wind_get(c->win, WF_WORKXYWH, &c->wx, &c->wy, &c->ww, &c->wh);
    c->cx = (short)(c->wx + c->ww / 2);
    c->cy = (short)(c->wy + c->wh / 2);
    c->rim = (short)(((c->ww < c->wh) ? c->ww : c->wh) / 2 - 4);
}


/* ---- drawing ------------------------------------------------------ */

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

/* A point on the dial: second s, at a fraction f of the radius.  The
   screen counts y downwards, so the cosine is subtracted. */
static void point_at(short s, float f, short *x, short *y)
{
    float a = s * (2.0f * (float)M_PI / 60.0f);

    *x = fround(cur->cx + f * cur->rim * sinf(a));
    *y = fround(cur->cy - f * cur->rim * cosf(a));
}

static void draw_dial(void)
{
    short s, x1, y1, x2, y2;

    box(cur->wx, cur->wy, cur->ww, cur->wh, WHITE);
    vsl_color(vdi, BLACK);

    /* the rim, as sixty chords */
    for (s = 0; s < 60; s++)
    {
        point_at(s, 1.0f, &x1, &y1);
        point_at((short)(s + 1), 1.0f, &x2, &y2);
        line(x1, y1, x2, y2);
    }

    /* the ticks: every second short, every fifth long */
    for (s = 0; s < 60; s++)
    {
        point_at(s, (s % 5) ? 0.91f : 0.82f, &x1, &y1);
        point_at(s, 1.0f, &x2, &y2);
        line(x1, y1, x2, y2);
    }
}

static void draw_hand(short colour)
{
    short x, y;

    vsl_color(vdi, colour);
    point_at(cur->shown, 0.78f, &x, &y);
    line(cur->cx, cur->cy, x, y);
}


/* ---- the cube ----------------------------------------------------- */

static const signed char corner[8][3] = {
    { -1, -1, -1 }, {  1, -1, -1 }, {  1,  1, -1 }, { -1,  1, -1 },
    { -1, -1,  1 }, {  1, -1,  1 }, {  1,  1,  1 }, { -1,  1,  1 }
};

static const unsigned char edge[12][2] = {
    { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
    { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
};

/* Runs on the real-time core, twenty times a second, for one clock:
   computed around (0, 0), scaled and placed by the GEM half. */
static void spin(void *arg)
{
    struct clock *c = arg;
    volatile float *px, *py;
    float sa, ca, sb, cb, x, y, z, y1, z1, x2, z2, d;
    int i;

    px = c->frame[1 - c->newest].x;
    py = c->frame[1 - c->newest].y;

    sa = sinf(c->ax); ca = cosf(c->ax);
    sb = sinf(c->ay); cb = cosf(c->ay);

    for (i = 0; i < 8; i++)
    {
        x = corner[i][0] * (float)CUBE_R;
        y = corner[i][1] * (float)CUBE_R;
        z = corner[i][2] * (float)CUBE_R;

        y1 = y * ca - z * sa;
        z1 = y * sa + z * ca;
        x2 = x * cb + z1 * sb;
        z2 = z1 * cb - x * sb;

        d = (float)CUBE_D + z2;
        px[i] = x2 * (float)CUBE_D / d;
        py[i] = -y1 * (float)CUBE_D / d;
    }

    c->newest = (unsigned char)(1 - c->newest);

    c->ax += c->dax;
    c->ay += c->day;
}

/* the square the cube may use: a third of the dial */
static short cube_box(void)
{
    return (short)(cur->rim * 0.34f);
}

static void draw_cube(void)
{
    volatile float *px, *py;
    unsigned char n = cur->newest;
    float scale = cube_box() / (CUBE_R * 1.45f);
    short b = cube_box(), i, a, e;

    box((short)(cur->cx - b), (short)(cur->cy - b),
        (short)(2 * b), (short)(2 * b), WHITE);

    px = cur->frame[n].x;
    py = cur->frame[n].y;

    vsl_color(vdi, BLACK);
    for (i = 0; i < 12; i++)
    {
        a = edge[i][0];
        e = edge[i][1];
        line(fround(cur->cx + px[a] * scale), fround(cur->cy + py[a] * scale),
             fround(cur->cx + px[e] * scale), fround(cur->cy + py[e] * scale));
    }
}


/* ---- through the rectangle list ------------------------------------ */

/*
 * Call what() for clock c, clipped to every visible piece of its window
 * that lies in (x, y, w, h).  With the screen locked, so that nothing
 * moves meanwhile, and the pointer hidden, so that it is not drawn over.
 */
static void each_rect(struct clock *c, short x, short y, short w, short h,
                      void (*what)(void))
{
    short rx, ry, rw, rh, clip[4];

    cur = c;
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, 0L);

    wind_get(c->win, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
    while (rw && rh)
    {
        /* the piece, cut down to the area asked for */
        short x1 = (rx > x) ? rx : x;
        short y1 = (ry > y) ? ry : y;
        short x2 = ((rx + rw) < (x + w)) ? (short)(rx + rw) : (short)(x + w);
        short y2 = ((ry + rh) < (y + h)) ? (short)(ry + rh) : (short)(y + h);

        if (x1 < x2 && y1 < y2)
        {
            clip[0] = x1; clip[1] = y1;
            clip[2] = (short)(x2 - 1); clip[3] = (short)(y2 - 1);
            vs_clip(vdi, 1, clip);
            what();
        }
        wind_get(c->win, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
    }

    vs_clip(vdi, 0, clip);
    graf_mouse(M_ON, 0L);
    wind_update(END_UPDATE);
}

static void draw_all(void)
{
    draw_dial();
    if (cur->spin != IRK_NONE)
        draw_cube();
    draw_hand(BLACK);
}

/* the cube, and the hand over it again */
static void draw_middle(void)
{
    draw_cube();
    draw_hand(BLACK);
}

static void erase_hand(void)
{
    draw_hand(WHITE);
}

static void put_hand(void)
{
    draw_hand(BLACK);
}

static void redraw(struct clock *c)
{
    each_rect(c, c->wx, c->wy, c->ww, c->wh, draw_all);
}


/* ---- the real-time half ----------------------------------------- */

/* Once a second, for one clock.  The GEM half tells the clocks apart by
   the task that sent the message. */
static void tick(void *arg)
{
    struct clock *c = arg;

    c->seconds++;
    rt->notify(c->seconds, 0);
}


/* ---- the GEM half ------------------------------------------------ */

static struct clock *by_window(short handle)
{
    int i;

    for (i = 0; i < NCLOCKS; i++)
        if (clocks[i].win >= 0 && clocks[i].win == handle)
            return &clocks[i];
    return 0;
}

static struct clock *by_task(short task)
{
    int i;

    for (i = 0; i < NCLOCKS; i++)
        if (clocks[i].win >= 0 && clocks[i].tick == (irk_handle)task)
            return &clocks[i];
    return 0;
}

static void move_to(struct clock *c, short x, short y, short w, short h)
{
    short ax, ay, aw, ah;

    /* not smaller than MIN_WORK in either direction */
    wind_calc(WC_WORK, KIND, x, y, w, h, &ax, &ay, &aw, &ah);
    if (aw < MIN_WORK || ah < MIN_WORK)
    {
        wind_calc(WC_BORDER, KIND, ax, ay,
                  (aw < MIN_WORK) ? MIN_WORK : aw,
                  (ah < MIN_WORK) ? MIN_WORK : ah, &x, &y, &w, &h);
    }
    wind_set(c->win, WF_CURRXYWH, x, y, w, h);
    layout(c);
}

/* The window goes, and its two tasks with it: their code and their data
   are this program's memory. */
static void close_clock(struct clock *c)
{
    if (c->tick != IRK_NONE)
        k->task_kill(c->tick);
    if (c->spin != IRK_NONE)
        k->task_kill(c->spin);
    c->tick = c->spin = IRK_NONE;

    wind_close(c->win);
    wind_delete(c->win);
    c->win = -1;
}

static void start_tasks(struct clock *c, int i)
{
    c->dax = i ? -0.110f : 0.074f;      /* the second one the other way */
    c->day = i ?  0.083f : 0.049f;

    /* Born suspended, so that everything is set before they run.  The
       second clock ticks half a second after the first: the way up to
       the GEM half has room for one message at a time, and two sent at
       the same moment would leave only the later one. */
    c->tick = k->task_new(IRK_CORE_RT, tick, c, 100, 0, 1024, 0);
    if (c->tick != IRK_NONE)
    {
        k->set_cyclic(c->tick, 1000000UL, i ? 500000UL : 0);
        k->task_resume(c->tick);
    }
    c->spin = k->task_new(IRK_CORE_RT, spin, c, 100, 0, 1024, 0);
    if (c->spin != IRK_NONE)
    {
        k->set_cyclic(c->spin, 50000UL, 0);
        k->task_resume(c->spin);
    }
}

int main(void)
{
    short wchar, hchar, wbox, hbox;
    short apid, dx, dy, dw, dh, x, y, w, h;
    long  value = 0;
    int   i, open = 0;

    apid = appl_init();
    if (apid < 0)
        return 1;

    vdi = v_opnvwk(graf_handle(&wchar, &hchar, &wbox, &hbox));
    if (!vdi)
    {
        appl_exit();
        return 1;
    }

    if (Ssystem(S_GETCOOKIE, IRK_COOKIE, (long)&value) == 0 && value)
        k = (struct irk_api *)value;
    if (k && k->cores() > 1)
    {
        rt = k->rt_api();
        k->notify_to((unsigned short)apid);
    }
    else
        k = 0;

    /* One window: in the middle.  Two: top left and bottom right, a
       little over each other. */
    wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
    wind_calc(WC_BORDER, KIND, 0, 0, (NCLOCKS == 1) ? 150 : 120,
              (NCLOCKS == 1) ? 150 : 120, &x, &y, &w, &h);

    for (i = 0; i < NCLOCKS; i++)
    {
        struct clock *c = &clocks[i];

        c->tick = c->spin = IRK_NONE;
        c->win = wind_create(KIND, dx, dy, dw, dh);
        if (c->win < 0)
            break;

        if (NCLOCKS == 1)
        {
            x = (short)(dx + (dw - w) / 2);
            y = (short)(dy + (dh - h) / 2);
        }
        else
        {
            x = i ? (short)(dx + dw - w - 4) : (short)(dx + 4);
            y = i ? (short)(dy + dh - h - 4) : (short)(dy + 4);
        }

        {
            static const char base[] = " GEMbedded Clock ";
            int n;

            for (n = 0; base[n]; n++)
                c->title[n] = base[n];
            if (NCLOCKS > 1)
            {
                c->title[n - 1] = ' ';
                c->title[n++] = (char)('1' + i);
                c->title[n++] = ' ';
            }
            c->title[n] = 0;
        }
        wind_set_name(c->win, c->title);
        wind_open(c->win, x, y, w, h);
        layout(c);
        open++;

        if (k)
            start_tasks(c, i);
    }

    if (open == 0)
    {
        form_alert(1, "[1][No window left.][ OK ]");
        v_clsvwk(vdi);
        appl_exit();
        return 1;
    }

    while (open > 0)
    {
        short msg[8];
        short ev = evnt_multi_mesag_timer(50, msg);
        struct clock *c;

        if (ev & MU_MESAG)
        {
            switch (msg[0])
            {
            case WM_REDRAW:
                if ((c = by_window(msg[3])) != 0)
                    each_rect(c, msg[4], msg[5], msg[6], msg[7], draw_all);
                break;
            case WM_TOPPED:
                if ((c = by_window(msg[3])) != 0)
                    wind_set(c->win, WF_TOP, 0, 0, 0, 0);
                break;
            case WM_MOVED:
            case WM_SIZED:
                if ((c = by_window(msg[3])) != 0)
                {
                    move_to(c, msg[4], msg[5], msg[6], msg[7]);
                    /* a move alone brings no redraw of what was already
                       visible, and the dial is somewhere else now */
                    redraw(c);
                }
                break;
            case WM_FULLED:
                if ((c = by_window(msg[3])) != 0)
                {
                    short fx, fy, fw, fh;

                    wind_get(c->win, WF_CURRXYWH, &x, &y, &w, &h);
                    wind_get(c->win, WF_FULLXYWH, &fx, &fy, &fw, &fh);
                    if (x == fx && y == fy && w == fw && h == fh)
                        wind_get(c->win, WF_PREVXYWH, &fx, &fy, &fw, &fh);
                    move_to(c, fx, fy, fw, fh);
                    redraw(c);
                }
                break;
            case WM_CLOSED:
                if ((c = by_window(msg[3])) != 0)
                {
                    close_clock(c);
                    open--;
                }
                break;
            case IRK_MSG:
                /* A clock's second half has counted: the value is in the
                   message, we keep no count of our own. */
                if ((c = by_task(msg[3])) != 0)
                {
                    unsigned long n =
                        ((unsigned long)(unsigned short)msg[4] << 16)
                        | (unsigned short)msg[5];

                    each_rect(c, c->wx, c->wy, c->ww, c->wh, erase_hand);
                    c->shown = (short)(n % 60);
                    each_rect(c, c->wx, c->wy, c->ww, c->wh, put_hand);
                }
                break;
            }
        }

        /* every open clock's cube, twenty times a second */
        for (i = 0; i < NCLOCKS; i++)
        {
            c = &clocks[i];
            if (c->win >= 0 && c->spin != IRK_NONE)
            {
                short b;

                cur = c;
                b = cube_box();
                each_rect(c, (short)(c->cx - b), (short)(c->cy - b),
                          (short)(2 * b), (short)(2 * b), draw_middle);
            }
        }
    }

    v_clsvwk(vdi);
    appl_exit();
    return 0;
}
