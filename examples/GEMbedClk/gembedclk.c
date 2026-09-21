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
 */

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include "gem.h"
#include <math.h>
#include "mathglue.h"
#include "irk.h"

#define WHITE         0
#define BLACK         1

#define KIND        (NAME | CLOSER | FULLER | MOVER | SIZER)
#define MIN_WORK     64         /* the smallest work area, either way */

static short vdi;               /* our virtual workstation */
static short win;               /* our window */
static char  title[] = " GEMbedded Clock ";

/* where things are, in screen coordinates: recomputed on every move */
static short wx, wy, ww, wh;    /* the work area */
static short cx, cy, rim;       /* centre and radius of the dial */

static void layout(void)
{
    wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
    cx = (short)(wx + ww / 2);
    cy = (short)(wy + wh / 2);
    rim = (short)(((ww < wh) ? ww : wh) / 2 - 4);
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

    *x = fround(cx + f * rim * sinf(a));
    *y = fround(cy - f * rim * cosf(a));
}

static void draw_dial(void)
{
    short s, x1, y1, x2, y2;

    box(wx, wy, ww, wh, WHITE);
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

static short shown;             /* the second the hand points at */

static void draw_hand(short colour)
{
    short x, y;

    vsl_color(vdi, colour);
    point_at(shown, 0.78f, &x, &y);
    line(cx, cy, x, y);
}


/* ---- the cube ----------------------------------------------------- */
/*
 * Computed on the real-time core around (0, 0), for a cube of half an
 * edge CUBE_R seen from CUBE_D; the GEM half scales it to the dial and
 * moves it to the centre.  Floating point on both cores.
 */

#define CUBE_R       20
#define CUBE_D      160

static const signed char corner[8][3] = {
    { -1, -1, -1 }, {  1, -1, -1 }, {  1,  1, -1 }, { -1,  1, -1 },
    { -1, -1,  1 }, {  1, -1,  1 }, {  1,  1,  1 }, { -1,  1,  1 }
};

static const unsigned char edge[12][2] = {
    { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
    { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
};

/* Two buffers: the real-time half fills the one that is not being read,
   then says which is newest.  One writer, one reader, no lock. */
static volatile struct {
    float x[8], y[8];
} frame[2];

static volatile unsigned char newest;

/* Runs on the real-time core, twenty times a second. */
static void spin(void *arg)
{
    static float ax, ay;
    volatile float *px, *py;
    float sa, ca, sb, cb, x, y, z, y1, z1, x2, z2, d;
    int i;

    (void)arg;

    px = frame[1 - newest].x;
    py = frame[1 - newest].y;

    sa = sinf(ax); ca = cosf(ax);
    sb = sinf(ay); cb = cosf(ay);

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

    newest = (unsigned char)(1 - newest);

    ax += 0.074f;
    ay += 0.049f;
}

/* the square the cube may use: a third of the dial */
static short cube_box(void)
{
    return (short)(rim * 0.34f);
}

static void draw_cube(void)
{
    volatile float *px, *py;
    unsigned char n = newest;
    float scale = cube_box() / (CUBE_R * 1.45f);
    short b = cube_box(), i, a, e;

    box((short)(cx - b), (short)(cy - b), (short)(2 * b), (short)(2 * b), WHITE);

    px = frame[n].x;
    py = frame[n].y;

    vsl_color(vdi, BLACK);
    for (i = 0; i < 12; i++)
    {
        a = edge[i][0];
        e = edge[i][1];
        line(fround(cx + px[a] * scale), fround(cy + py[a] * scale),
             fround(cx + px[e] * scale), fround(cy + py[e] * scale));
    }
}


/* ---- through the rectangle list ------------------------------------ */

/*
 * Call what(), clipped to every visible piece of the window that lies in
 * (x, y, w, h).  With the screen locked, so that nothing moves meanwhile,
 * and the pointer hidden, so that it is not drawn over.
 */
static void each_rect(short x, short y, short w, short h, void (*what)(void))
{
    short rx, ry, rw, rh, clip[4];

    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, 0L);

    wind_get(win, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
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
        wind_get(win, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
    }

    vs_clip(vdi, 0, clip);
    graf_mouse(M_ON, 0L);
    wind_update(END_UPDATE);
}

static int have_cube;

static void draw_all(void)
{
    draw_dial();
    if (have_cube)
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


/* ---- the real-time half ----------------------------------------- */

static struct irk_api *k;       /* the kernel, as this core sees it */
static struct irk_api *rt;      /* as the other core sees it */

static volatile unsigned long seconds;

static void tick(void *arg)
{
    (void)arg;
    seconds++;
    rt->notify(seconds, 0);
}


/* ---- the GEM half ------------------------------------------------ */

static void move_to(short x, short y, short w, short h)
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
    wind_set(win, WF_CURRXYWH, x, y, w, h);
    layout();
}

int main(void)
{
    short wchar, hchar, wbox, hbox;
    short apid, dx, dy, dw, dh, x, y, w, h;
    long  value = 0;
    irk_handle t = IRK_NONE, c = IRK_NONE;
    int   running = 1;

    apid = appl_init();
    if (apid < 0)
        return 1;

    vdi = v_opnvwk(graf_handle(&wchar, &hchar, &wbox, &hbox));
    if (!vdi)
    {
        appl_exit();
        return 1;
    }

    /* a square window in the middle of the desktop */
    wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
    wind_calc(WC_BORDER, KIND, 0, 0, 150, 150, &x, &y, &w, &h);
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

    /* the two halves on the real-time core, if there is one */
    if (Ssystem(S_GETCOOKIE, IRK_COOKIE, (long)&value) == 0 && value)
        k = (struct irk_api *)value;

    if (k && k->cores() > 1)
    {
        rt = k->rt_api();
        k->notify_to((unsigned short)apid);

        t = k->task_new(IRK_CORE_RT, tick, 0, 100, 0, 1024, 0);
        if (t != IRK_NONE)
        {
            k->set_cyclic(t, 1000000UL, 0);     /* once a second */
            k->task_resume(t);
        }
        c = k->task_new(IRK_CORE_RT, spin, 0, 100, 0, 1024, 0);
        if (c != IRK_NONE)
        {
            k->set_cyclic(c, 50000UL, 0);       /* twenty a second */
            k->task_resume(c);
            have_cube = 1;
        }
    }

    while (running)
    {
        short msg[8];
        short ev = evnt_multi_mesag_timer(50, msg);

        if (ev & MU_MESAG)
        {
            switch (msg[0])
            {
            case WM_REDRAW:
                if (msg[3] == win)
                    each_rect(msg[4], msg[5], msg[6], msg[7], draw_all);
                break;
            case WM_TOPPED:
                if (msg[3] == win)
                    wind_set(win, WF_TOP, 0, 0, 0, 0);
                break;
            case WM_MOVED:
            case WM_SIZED:
                if (msg[3] == win)
                {
                    move_to(msg[4], msg[5], msg[6], msg[7]);
                    /* a move alone brings no redraw of what was already
                       visible, and the dial is somewhere else now */
                    each_rect(wx, wy, ww, wh, draw_all);
                }
                break;
            case WM_FULLED:
                if (msg[3] == win)
                {
                    short fx, fy, fw, fh;

                    wind_get(win, WF_CURRXYWH, &x, &y, &w, &h);
                    wind_get(win, WF_FULLXYWH, &fx, &fy, &fw, &fh);
                    if (x == fx && y == fy && w == fw && h == fh)
                        wind_get(win, WF_PREVXYWH, &fx, &fy, &fw, &fh);
                    move_to(fx, fy, fw, fh);
                    each_rect(wx, wy, ww, wh, draw_all);
                }
                break;
            case WM_CLOSED:
                if (msg[3] == win)
                    running = 0;
                break;
            case IRK_MSG:
            {
                /* The second half has counted: the value is in the
                   message, we keep no count of our own. */
                unsigned long n = ((unsigned long)(unsigned short)msg[4] << 16)
                                  | (unsigned short)msg[5];

                each_rect(wx, wy, ww, wh, erase_hand);
                shown = (short)(n % 60);
                each_rect(wx, wy, ww, wh, put_hand);
                break;
            }
            }
        }

        if (have_cube)
        {
            short b = cube_box();

            each_rect((short)(cx - b), (short)(cy - b),
                      (short)(2 * b), (short)(2 * b), draw_middle);
        }
    }

    /* The second half dies with its program: its stack and code are this
       program's memory. */
    if (t != IRK_NONE)
        k->task_kill(t);
    if (c != IRK_NONE)
        k->task_kill(c);

    wind_close(win);
    wind_delete(win);
    v_clsvwk(vdi);
    appl_exit();
    return 0;
}
