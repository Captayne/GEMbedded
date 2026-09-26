/*
 * fractals.c - three computations sharing the real-time core
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * The GEM half owns four windows and does nothing but draw.  The other
 * half is three tasks on the real-time core, each filling a canvas of
 * RGB565 pixels in memory both halves can see; the GEM half copies the
 * canvas into the window with one raster call.
 *
 * Two of the tasks share the processor by priority, which in IRKernel is
 * a share and not a rank -- the fourth window's sliders set those shares
 * and the panel reports the shares actually granted.  The third task is
 * cyclic instead, because a waving figure wants an even tempo rather than
 * as much processor as it can get.  Together they show what the kernel
 * does: the sliders change the fractals and leave the figure alone.
 *
 * Nothing here preempts anything.  A task holds the core until it gives
 * way, so each of them yields after every single line of its picture --
 * that is the bargain, and it is why the pictures grow downwards in front
 * of you instead of appearing whole.
 */

#include <mint/osbind.h>
#include <mint/mintbind.h>    /* Ssystem(), for the cookie */
#include "gem.h"
#include <math.h>
#include "mathglue.h"
#include "irk.h"

#define WHITE       0
#define BLACK       1

#define KIND        (NAME | MOVER | SIZER)
#define PKIND       (NAME | CLOSER | MOVER | SIZER)

#define LISSA_MIN_US 10000UL            /* what the third slider can ask */
#define LISSA_MAX_US 100000UL

#define NJOBS       3
#define MAXW        320         /* the canvas: the whole screen, so that a  */
#define MAXH        240         /*  window may be sized up to it           */
#define PAL         256         /* colours in the ramp */
#define ITER_MIN    96          /* escape iterations at the widest view  */
#define ITER_MAX    255         /*  and at the deepest                   */

/*
 * How deep a zoom may go.  Every coordinate here is a float, which
 * carries about seven decimal digits; once the view is narrow enough
 * that neighbouring pixels round to the same number the picture turns to
 * blocks.  Stop before that and begin again.
 */
#define SPAN_MIN    2.0e-5f
#define ZOOM_STEP   0.94f       /* per finished frame */

#define PRIO_MIN    10          /* what the sliders can ask for */
#define PRIO_MAX    400

/*
 * How often the GEM half looks.  Short against every rate it has to keep
 * up with, because it cannot be in step with any of them: the display
 * puts out about nineteen frames a second (a frame is 49 ms of SPI, and
 * the next one starts on the next 20 ms tick), the figure finishes one
 * every period_us, and the fractals finish one whenever they finish one.
 * Looking twice as often as the fastest of those costs a few AES calls
 * and removes the beat between them.
 */
#define TICK_MS     20
#define STAT_TICKS  25          /* half a second, in ticks */

/* The figure's default period: the same twenty a second the display
   manages, so that every figure computed is a figure shown, once. */
#define LISSA_US    50000UL

/*
 * One computation: its window, its canvas, and its task.
 *
 * cw/ch and the volatile members are the whole conversation between the
 * halves.  The GEM half writes cw/ch when the window is sized; the task
 * reads them when it starts a picture and starts over if they changed
 * underneath it.  Nothing is locked, because nothing needs to be: a
 * half-written picture is a picture in progress, which is what it is.
 */
struct job
{
    const char     *name;
    short           win;                /* -1 once closed */
    char            title[24];

    short           wx, wy, ww, wh;     /* work area, screen coordinates */

    /*
     * Two canvases.  The task fills the back one and swaps when the
     * picture is whole; the GEM half always copies the front one.  With
     * a single canvas the window showed the picture being built -- white
     * below the last line computed, and white over the whole of it at
     * the start of every frame.  That is where the flicker came from,
     * not from clearing anything.  The cost is another 150 KB of PSRAM
     * per picture, out of sixteen megabytes.
     */
    unsigned short *pix[2];             /* MAXW * MAXH each, in Alt-RAM */
    MFDB            canvas[2];
    volatile short  front;              /* the one worth showing */

    volatile short  cw, ch;             /* what to compute: from GEM */
    volatile short  lines;              /* how much of it is done */
    volatile unsigned long frames;

    /*
     * Bumped at the top of every turn, before the canvas is touched.
     * A task that is idle for want of a window and one that died on its
     * first pixel both show 0% of the processor and an empty window; this
     * is what tells them apart.
     */
    volatile unsigned short turns;

    irk_handle      task;
    unsigned short  prio;               /* what the slider asks for */
    short           held;               /* suspended */

    unsigned long   runtime;            /* at the last statistics tick */
    short           share;              /* per mille of the core, measured */
    long            stack;              /* bytes of stack never touched */

    /*
     * Whether its slider sets a share or a period.  A cyclic task has no
     * use for a share -- it is given its turn when it is due, however
     * hungry the others are -- so its slider changes its tempo instead.
     * A slider that did nothing would teach the wrong lesson.
     */
    short           cyclic;
    unsigned long   period_us;

    /* what this fractal is looking at, in the complex plane */
    float           cx, cy, span;       /* centre, and width of the view */
    float           span0;              /* where the zoom starts over */
    short           iter;               /* deeper view, more iterations */
};

static short vdi;                       /* our virtual workstation */
static short panel = -1;                /* the fourth window */
static short px, py, pw, ph;            /* its work area */

static struct job jobs[NJOBS];
static struct irk_api *k;               /* the kernel, as this core sees it */
static struct irk_api *rt;              /* as the other core sees it */

static unsigned short pal[PAL];

/*
 * Worst lateness of the cyclic task since the panel last looked.  Written
 * on the real-time core, read and cleared on this one: a plain race, and
 * the right one -- the number is for the eye, and losing one reading to
 * the clearing costs nothing.
 */
static volatile unsigned long late_us;

/*
 * The screen, as a bitmap.  fd_addr == 0 is how the VDI is told "the
 * screen", but the MFDB itself must exist: the destination pointer is
 * dereferenced before that field is looked at.
 */
static MFDB screen;

/* the Julia parameter, which drifts, and the Lissajous phase, which waves */
static volatile float julia_cr = -0.74f, julia_ci = 0.16f;

/* ---- colour ------------------------------------------------------- */

static unsigned short rgb565(int r, int g, int b)
{
    return (unsigned short)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

/*
 * Iterations to a colour, once, into a table: a lookup keeps the inner
 * loop of the fractals to arithmetic, which is the point of a table.
 *
 * Inside the set is black.  Outside, the ramp runs blue -> cyan -> yellow
 * -> white, which separates the bands near the edge where they are
 * crowded rather than spending its range on the smooth outside.
 */
static void build_palette(void)
{
    int n;

    for (n = 0; n < PAL; n++)
    {
        float t = (float)n / (float)PAL;
        float u = sqrtf(t);             /* crowd the ramp towards the edge */
        int   r, g, b;

        if (u < 0.5f)
        {
            float v = u * 2.0f;                  /* blue -> cyan */
            r = 0;
            g = (int)(255.0f * v);
            b = 255;
        }
        else
        {
            float v = (u - 0.5f) * 2.0f;         /* cyan -> yellow -> white */
            r = (int)(255.0f * v);
            g = 255;
            b = (int)(255.0f * (1.0f - v) + 255.0f * v * v);
        }
        pal[n] = rgb565(r, g, b);
    }
    pal[0] = 0;                         /* inside the set */
}

/* ---- the real-time halves ----------------------------------------- */

/*
 * Every one of these runs on the other core.  They may not call GEMDOS,
 * BIOS, XBIOS, VDI or AES -- the operating system belongs to core 0 --
 * and they reach the kernel through `rt`, never through `k`.
 */

static void escape_line(struct job *j, short y, short w, short h, int julia)
{
    unsigned short *row = j->pix[j->front ^ 1] + (long)y * MAXW;
    /* the view is square in the plane whatever the window's shape, so
       the height follows the width rather than being given its own */
    float half = j->span * 0.5f;
    float step = j->span / (float)w;
    float x0 = j->cx - half;
    float ci = j->cy - step * (float)h * 0.5f + step * (float)y;
    float jr = julia_cr, ji = julia_ci;
    short limit = j->iter;
    short x;

    for (x = 0; x < w; x++)
    {
        float cr = x0 + step * (float)x;
        float zr, zi, zr2, zi2;
        short n;

        float ar, ai;                   /* what is added every iteration */

        if (julia)
        {
            zr = cr; zi = ci;           /* the point is where z starts */
            ar = jr; ai = ji;           /*  and the constant is fixed   */
        }
        else
        {
            zr = 0.0f; zi = 0.0f;       /* and for Mandelbrot, what c is */
            ar = cr; ai = ci;
        }
        zr2 = zr * zr;
        zi2 = zi * zi;

        /* Which of the two this is cannot change while a pixel is being
           computed, so it is decided above rather than in here: the test
           was costing a compare and a branch on every one of up to 255
           iterations, for an answer settled before the picture began. */
        for (n = 0; n < limit && zr2 + zi2 <= 4.0f; n++)
        {
            zi = zr * zi;
            zi = zi + zi + ai;          /* 2*zr*zi, without the multiply */
            zr = zr2 - zi2 + ar;
            zr2 = zr * zr;
            zi2 = zi * zi;
        }
        /* the ramp is a fixed length and the iteration limit is not, so
           the escape count is stretched over it rather than indexing it */
        row[x] = (n >= limit) ? pal[0]
                              : pal[1 + (int)n * (PAL - 2) / limit];
    }
}

/*
 * A picture, a line at a time, for ever.  The yield after each line is
 * what lets anything else on this core run at all; without it this task
 * would own the core until the whole picture was finished, and the figure
 * in the third window would stop waving.
 */
static void escape_task(struct job *j, int julia)
{
    for (;;)
    {
        short w = j->cw, h = j->ch;
        short y;

        j->turns++;
        if (w < 4 || h < 4)
        {
            rt->delay_us(20000);        /* no window worth filling yet */
            continue;
        }

        /* more iterations the narrower the view, or the detail that the
           zoom uncovers is all one colour by the time you reach it */
        {
            float t = j->span0 / ((j->span > SPAN_MIN) ? j->span : SPAN_MIN);
            short extra = (short)(logf(t) * 12.0f);

            j->iter = (short)(ITER_MIN + ((extra > 0) ? extra : 0));
            if (j->iter > ITER_MAX)
                j->iter = ITER_MAX;
        }

        j->lines = 0;
        for (y = 0; y < h; y++)
        {
            escape_line(j, y, w, h, julia);
            j->lines = (short)(y + 1);
            rt->yield();

            if (j->cw != w || j->ch != h)
                break;                  /* sized underneath us: start over */
        }

        if (y >= h)                     /* whole: show it, fill the other */
        {
            j->front ^= 1;
            j->frames++;
        }

        if (julia)
        {
            /* let the parameter walk a circle slowly, so the picture is
               alive and the slider changes how fast it lives */
            float a = (float)(j->frames % 720) * 0.0087266f;   /* 0.5 deg */

            julia_cr = -0.74f + 0.12f * cosf(a);
            julia_ci =  0.16f + 0.12f * sinf(a);
        }
        else
        {
            /* and the Mandelbrot falls towards its point until a float
               can no longer tell one pixel from the next */
            j->span *= ZOOM_STEP;
            if (j->span < SPAN_MIN)
                j->span = j->span0;
        }
    }
}

static void mandel_body(void *arg) { escape_task((struct job *)arg, 0); }
static void julia_body(void *arg)  { escape_task((struct job *)arg, 1); }

/* a line into the canvas, for the figure; clipped to the canvas, not the
   window, because the window's business is the GEM half's */
static void canvas_line(struct job *j, short x0, short y0, short x1, short y1,
                        unsigned short col)
{
    short dx = (short)((x1 > x0) ? x1 - x0 : x0 - x1);
    short dy = (short)((y1 > y0) ? y1 - y0 : y0 - y1);
    short sx = (short)((x0 < x1) ? 1 : -1);
    short sy = (short)((y0 < y1) ? 1 : -1);
    short err = (short)(dx - dy);

    for (;;)
    {
        if (x0 >= 0 && x0 < MAXW && y0 >= 0 && y0 < MAXH)
            j->pix[j->front ^ 1][(long)y0 * MAXW + x0] = col;
        if (x0 == x1 && y0 == y1)
            return;
        if (err * 2 > -dy) { err = (short)(err - dy); x0 = (short)(x0 + sx); }
        else if (err * 2 < dx) { err = (short)(err + dx); y0 = (short)(y0 + sy); }
    }
}

/*
 * The figure.  Cyclic, not share-scheduled: it has a tempo to keep, and
 * the kernel gives it its turn at a fixed period whatever the sliders
 * say about the other two.  It must therefore finish inside that period,
 * and the panel shows whether it did.
 *
 * x = sin(a*t + phase), y = sin(b*t): closed when a and b are whole, and
 * it waves because the phase creeps.
 */
static void lissa_body(void *arg)
{
    struct job *j = (struct job *)arg;
    static unsigned long prev;          /* when this task last ran */
    unsigned long now = rt->now_us();
    short w = j->cw, h = j->ch;
    float phase = 0.0f;
    short lx = 0, ly = 0;
    long  i;
    short cx, cy, rx, ry;
    const long STEPS = 400;

    /*
     * How much later than its own tempo this activation came.
     *
     * Measured as the gap since the last one, not against a grid: the
     * kernel does not say when a task was due, and taking the first
     * activation as the origin -- which is what this did before -- means
     * measuring every lateness relative to the first one's.  A steady
     * lateness then reads as zero, and an activation that happened to be
     * punctual than the first reads as almost a whole period.  That was
     * the constant 0 with the occasional spike.
     *
     * Nothing here can be preempted, so a deadline cannot be enforced --
     * only measured, and a number nobody looks at is not a guarantee.
     */
    if (prev)
    {
        unsigned long gap = now - prev;

        if (gap > j->period_us)
        {
            unsigned long over = gap - j->period_us;

            /* a period changed by the slider, or a task just resumed,
               gives one meaningless gap; it is not worth reporting */
            if (over > late_us && over < j->period_us * 4)
                late_us = over;
        }
    }
    prev = now;

    j->turns++;
    if (w < 8 || h < 8)
        return;                         /* nothing to draw into this turn */

    /* clear only what this picture uses, not the whole canvas -- and the
       back one, so that nobody ever sees it empty */
    for (i = 0; i < h; i++)
    {
        unsigned short *row = j->pix[j->front ^ 1] + i * MAXW;
        short x;
        for (x = 0; x < w; x++)
            row[x] = 0;
    }

    cx = (short)(w / 2);
    cy = (short)(h / 2);
    rx = (short)(w / 2 - 2);
    ry = (short)(h / 2 - 2);
    phase = (float)(j->frames % 360) * 0.0174533f;

    for (i = 0; i <= STEPS; i++)
    {
        float t = (float)i * 6.2831853f / (float)STEPS;
        short x = fround((float)cx + (float)rx * sinf(3.0f * t + phase));
        short y = fround((float)cy + (float)ry * sinf(2.0f * t));

        if (i)
            canvas_line(j, lx, ly, x, y,
                        pal[1 + (short)(i * (PAL - 2) / (STEPS + 1))]);
        lx = x;
        ly = y;
    }

    j->lines = h;
    j->front ^= 1;                      /* whole: show it */
    j->frames++;
}

/* ---- the GEM half: drawing ---------------------------------------- */

/*
 * A window's picture.  One raster copy, from the canvas in Alt-RAM to the
 * screen -- the only way to move a computed picture that does not cost a
 * VDI call per pixel.  Both are 16 bits per pixel, so the VDI copies
 * words and touches no colour at all.
 */
static void draw_job(struct job *j)
{
    short xy[8];
    short w = j->cw, h = j->ch;
    short f = j->front;

    if (w < 1 || h < 1)
        return;

    if (!j->frames)                     /* nothing finished to show yet */
    {
        short r[4];

        r[0] = j->wx; r[1] = j->wy;
        r[2] = (short)(j->wx + w - 1); r[3] = (short)(j->wy + h - 1);
        vsf_color(vdi, WHITE);
        vsf_interior(vdi, FIS_SOLID);
        vr_recfl(vdi, r);
        return;
    }

    xy[0] = 0;  xy[1] = 0;                              /* from the canvas */
    xy[2] = (short)(w - 1); xy[3] = (short)(h - 1);
    xy[4] = j->wx; xy[5] = j->wy;                       /* to the window */
    xy[6] = (short)(j->wx + w - 1);
    xy[7] = (short)(j->wy + h - 1);
    vro_cpyfm(vdi, S_ONLY, xy, &j->canvas[f], &screen);
}

/* where a job's slider track lies inside the panel */
static void slider_rect(int i, short *x, short *y, short *w, short *h)
{
    short row = (short)(ph / NJOBS);

    *x = (short)(px + 60);
    *y = (short)(py + i * row + row / 3);
    *w = (short)(pw - 60 - 46);
    *h = 8;
}

static void draw_text(short x, short y, const char *s)
{
    vst_color(vdi, BLACK);
    v_gtext(vdi, x, y, s);
}

/* a number, right-aligned at x, without printf */
static void draw_num(short x, short y, long v, const char *suffix)
{
    char buf[16];
    int  n = 0, i;

    if (v < 0) { v = 0; }
    do { buf[n++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v && n < 10);
    for (i = 0; i < n / 2; i++)
    {
        char t = buf[i];
        buf[i] = buf[n - 1 - i];
        buf[n - 1 - i] = t;
    }
    for (i = 0; suffix && suffix[i] && n < 15; i++)
        buf[n++] = suffix[i];
    buf[n] = 0;
    draw_text((short)(x - n * 8), y, buf);
}

/*
 * The panel.  Per task: its name, a track with a knob, the share it was
 * given.  Tapping the name suspends and resumes it; tapping the track
 * sets its share.
 */
static void draw_panel(void)
{
    short r[4];
    int   i;

    r[0] = px; r[1] = py;
    r[2] = (short)(px + pw - 1); r[3] = (short)(py + ph - 1);
    vsf_color(vdi, WHITE);
    vsf_interior(vdi, FIS_SOLID);
    vr_recfl(vdi, r);

    for (i = 0; i < NJOBS; i++)
    {
        struct job *j = &jobs[i];
        short sx, sy, sw, sh, kx;
        short row = (short)(ph / NJOBS);
        short base = (short)(py + i * row);

        draw_text((short)(px + 2), (short)(base + 10),
                  j->held ? "(held)" : j->name);

        slider_rect(i, &sx, &sy, &sw, &sh);

        /* the track */
        r[0] = sx; r[1] = sy;
        r[2] = (short)(sx + sw - 1); r[3] = (short)(sy + sh - 1);
        vsf_color(vdi, WHITE);
        vr_recfl(vdi, r);
        vsl_color(vdi, BLACK);
        {
            short box[10];
            box[0] = sx;            box[1] = sy;
            box[2] = (short)(sx + sw - 1); box[3] = sy;
            box[4] = (short)(sx + sw - 1); box[5] = (short)(sy + sh - 1);
            box[6] = sx;            box[7] = (short)(sy + sh - 1);
            box[8] = sx;            box[9] = sy;
            v_pline(vdi, 5, box);
        }

        /* the knob, where this row's value sits between the two ends */
        if (j->cyclic)
            kx = (short)(sx + (long)(j->period_us - LISSA_MIN_US) * (sw - 7)
                              / (long)(LISSA_MAX_US - LISSA_MIN_US));
        else
            kx = (short)(sx + (long)(j->prio - PRIO_MIN) * (sw - 7)
                              / (PRIO_MAX - PRIO_MIN));
        r[0] = kx; r[1] = (short)(sy - 2);
        r[2] = (short)(kx + 6); r[3] = (short)(sy + sh + 1);
        vsf_color(vdi, BLACK);
        vr_recfl(vdi, r);

        /*
         * What it actually got.  For the two that share, the per cent of
         * the core they were granted -- to be read against the ratio of
         * the two knobs above.  For the cyclic one there is nothing to
         * compare: it gets what it needs, so its own figure is its tempo.
         */
        if (j->cyclic)
            draw_num((short)(px + pw - 2), (short)(base + 10),
                     (long)(j->period_us / 1000UL), "ms");
        else
            draw_num((short)(px + pw - 2), (short)(base + 10),
                     (j->share + 5) / 10, "%");

        /*
         * Turns taken, and how far down the picture it has got, against
         * how tall the picture is meant to be.  Three numbers that say
         * which of the ways to show nothing this is.
         */
        {
            char d[20];
            int  n = 0, k;
            long v[3];
            v[0] = (long)j->turns; v[1] = (long)j->lines; v[2] = (long)j->ch;
            for (k = 0; k < 3; k++)
            {
                long x = v[k];
                char t[8];
                int  m = 0;
                do { t[m++] = (char)('0' + (int)(x % 10)); x /= 10; }
                while (x && m < 7);
                while (m && n < 18) d[n++] = t[--m];
                if (k < 2 && n < 18) d[n++] = (k == 0) ? ' ' : '/';
            }
            d[n] = 0;
            draw_text((short)(px + 2), (short)(base + 20), d);
        }
    }

    /*
     * The two numbers that say whether the arrangement holds: the worst
     * lateness of the cyclic task, and the smallest stack any of the
     * three has left.  A guessed stack that is never looked at is
     * superstition; this is the looking.
     */
    {
        long room = -1;
        int  n;

        for (n = 0; n < NJOBS; n++)
            if (jobs[n].task != IRK_NONE
                && (room < 0 || jobs[n].stack < room))
                room = jobs[n].stack;

        draw_text((short)(px + 2), (short)(py + ph - 4), "late");
        draw_num((short)(px + pw / 2), (short)(py + ph - 4),
                 (long)late_us, "us");
        draw_num((short)(px + pw - 2), (short)(py + ph - 4),
                 (room < 0) ? 0 : room, "b");
    }
}

/*
 * draw(), clipped to each visible piece of a window, with the screen
 * already held.  Separate from redraw_win() so that a round over several
 * windows can hold it once instead of once each.
 */
static void clip_and_draw(short win, short x, short y, short w, short h,
                          struct job *j)
{
    short rx, ry, rw, rh, clip[4];

    wind_get(win, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
    while (rw && rh)
    {
        short x1 = (short)((rx > x) ? rx : x);
        short y1 = (short)((ry > y) ? ry : y);
        short x2 = (short)(((rx + rw) < (x + w)) ? (rx + rw) : (x + w));
        short y2 = (short)(((ry + rh) < (y + h)) ? (ry + rh) : (y + h));

        if (x1 < x2 && y1 < y2)
        {
            clip[0] = x1; clip[1] = y1;
            clip[2] = (short)(x2 - 1); clip[3] = (short)(y2 - 1);
            vs_clip(vdi, 1, clip);
            if (j)
                draw_job(j);
            else
                draw_panel();
        }
        wind_get(win, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
    }
}

/* the same, holding the screen for the duration */
static void redraw_win(short win, short x, short y, short w, short h,
                       struct job *j)
{
    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, 0L);
    clip_and_draw(win, x, y, w, h, j);
    graf_mouse(M_ON, 0L);
    wind_update(END_UPDATE);
}

/* ---- the GEM half: keeping up with the windows -------------------- */

/*
 * Read a window's work area and tell its task how big a picture to
 * compute.  Changing cw/ch is what makes the task start over, so it is
 * only written when it really changed.
 */
static void relayout(struct job *j)
{
    short w, h;

    if (j->win < 0)
        return;
    wind_get(j->win, WF_WORKXYWH, &j->wx, &j->wy, &j->ww, &j->wh);

    w = (short)((j->ww > MAXW) ? MAXW : j->ww);
    h = (short)((j->wh > MAXH) ? MAXH : j->wh);
    if (w != j->cw || h != j->ch)
    {
        j->lines = 0;
        j->cw = w;
        j->ch = h;
    }
}

static void statistics(void)
{
    unsigned long now[NJOBS], total = 0;
    int i;

    for (i = 0; i < NJOBS; i++)
    {
        now[i] = (jobs[i].task == IRK_NONE)
               ? 0UL : k->runtime_us(jobs[i].task);
        total += now[i] - jobs[i].runtime;
    }

    for (i = 0; i < NJOBS; i++)
    {
        unsigned long used = now[i] - jobs[i].runtime;

        jobs[i].share = total ? (short)(used * 1000UL / total) : 0;
        jobs[i].runtime = now[i];
        if (jobs[i].task != IRK_NONE)
            jobs[i].stack = k->stack_free(jobs[i].task);
    }
}

/* a tap in the panel */
static void panel_click(short mx, short my)
{
    short row = (short)(ph / NJOBS);
    int   i = (row > 0) ? (my - py) / row : 0;
    short sx, sy, sw, sh;

    if (i < 0 || i >= NJOBS)
        return;
    slider_rect(i, &sx, &sy, &sw, &sh);

    if (mx < sx)                        /* the name: hold and let go */
    {
        struct job *j = &jobs[i];

        if (j->task == IRK_NONE)
            return;
        if (j->held)
            k->task_resume(j->task);
        else
            k->task_suspend(j->task);
        j->held = (short)!j->held;
    }
    else if (mx < sx + sw)              /* the track */
    {
        struct job *j = &jobs[i];
        long pos = (long)(mx - sx);
        long span = (sw > 1) ? sw - 1 : 1;

        if (pos < 0) pos = 0;
        if (pos > span) pos = span;

        if (j->cyclic)
        {
            /* the tempo, left fast and right slow, so that the knob moves
               the way the figure does */
            j->period_us = LISSA_MIN_US
                         + (unsigned long)pos
                           * (LISSA_MAX_US - LISSA_MIN_US) / (unsigned long)span;
            if (j->task != IRK_NONE)
                k->set_cyclic(j->task, j->period_us, 0UL);
            late_us = 0;                /* a new grid, a new worst case */
        }
        else
        {
            j->prio = (unsigned short)(PRIO_MIN
                        + pos * (PRIO_MAX - PRIO_MIN) / span);
            if (j->task != IRK_NONE)
                k->set_prio(j->task, j->prio);
        }
    }
    redraw_win(panel, px, py, pw, ph, 0);
}

/* ---- setting up --------------------------------------------------- */

static int start_tasks(void)
{
    int i;

    for (i = 0; i < NJOBS; i++)
        jobs[i].task = IRK_NONE;

    /*
     * Stack size 0: the default, which on the real-time core means the
     * one the runtime's pool hands out and nothing else -- ask for more
     * and the request is refused outright, not trimmed (RT_STACK in
     * rtcore.c).  Whether it is enough is not a matter of opinion: the
     * panel shows the smallest stack_free() of the three, so the answer
     * arrives by itself within a second of starting.
     */
    jobs[0].task = k->task_new(IRK_CORE_RT, mandel_body, &jobs[0],
                               jobs[0].prio, 0, 0, "mandel");
    jobs[1].task = k->task_new(IRK_CORE_RT, julia_body, &jobs[1],
                               jobs[1].prio, 0, 0, "julia");
    jobs[2].task = k->task_new(IRK_CORE_RT, lissa_body, &jobs[2],
                               jobs[2].prio, 0, 0, "lissa");

    for (i = 0; i < NJOBS; i++)
        if (jobs[i].task == IRK_NONE)
            return 0;

    /* the two fractals share what is left over; the figure keeps time */
    k->set_normal(jobs[0].task, jobs[0].prio);
    k->set_normal(jobs[1].task, jobs[1].prio);
    k->set_cyclic(jobs[2].task, jobs[2].period_us, 0UL);

    /*
     * A task is born suspended (cmd_task_new(), rtcore.c), so that the
     * program can finish arranging things before it looks at them --
     * and so that a period can be put on a body that would otherwise
     * have run and ended already.  The arranging is done.
     */
    for (i = 0; i < NJOBS; i++)
        k->task_resume(jobs[i].task);
    return 1;
}

static void stop_tasks(void)
{
    int i;

    for (i = 0; i < NJOBS; i++)
        if (jobs[i].task != IRK_NONE)
        {
            k->task_kill(jobs[i].task);
            jobs[i].task = IRK_NONE;
        }
}

/*
 * The canvases.  MX_TTRAM, not MX_PREFTTRAM: three of these are 450 KB
 * and the ST-RAM has nothing like that free once the colour framebuffer
 * has had its 150 KB.  If the PSRAM is not there, this demo has no room
 * and says so rather than limping.
 */
static int allocate(void)
{
    long size = (long)MAXW * MAXH * 2;
    int  i, b;

    for (i = 0; i < NJOBS; i++)
        for (b = 0; b < 2; b++)
        {
            MFDB *m = &jobs[i].canvas[b];
            long p = Mxalloc(size, MX_TTRAM);

            if (p <= 0)
                return 0;
            jobs[i].pix[b] = (unsigned short *)p;

            m->fd_addr = jobs[i].pix[b];
            m->fd_w = MAXW;
            m->fd_h = MAXH;
            m->fd_wdwidth = MAXW;       /* 16 bpp: a word is a pixel */
            m->fd_stand = 0;            /* already the screen's format */
            m->fd_nplanes = 16;
            m->fd_r1 = m->fd_r2 = m->fd_r3 = 0;
        }
    return 1;
}

int main(void)
{
    short wchar, hchar, wbox, hbox;
    short apid, dx, dy, dw, dh;
    short bw, bh, ox, oy;
    long  value = 0;
    int   i, tick = 0, running = 1;
    unsigned long shown[NJOBS];         /* the frame each window shows */

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
    if (!k || k->cores() < 2)
    {
        form_alert(1, "[1][This needs the real-time core.][ OK ]");
        v_clsvwk(vdi);
        appl_exit();
        return 1;
    }
    rt = k->rt_api();
    k->notify_to((unsigned short)apid);

    jobs[0].name = "mandel";
    jobs[1].name = "julia";
    jobs[2].name = "lissa";
    for (i = 0; i < NJOBS; i++)
    {
        jobs[i].win = -1;
        jobs[i].prio = 100;
        jobs[i].task = IRK_NONE;
        shown[i] = 0;
    }
    jobs[2].cyclic = 1;
    jobs[2].period_us = LISSA_US;

    /*
     * Where each one looks.  The Mandelbrot starts on the whole set and
     * falls towards the mouth of the seahorse valley, which has detail
     * at every scale a float can still tell apart.  The Julia keeps its
     * view and moves its parameter instead -- two different kinds of
     * motion beside each other say more than two of the same.
     */
    jobs[0].cx = -0.743644f; jobs[0].cy = 0.131826f;
    jobs[0].span = jobs[0].span0 = 3.0f;
    jobs[1].cx = 0.0f;       jobs[1].cy = 0.0f;
    jobs[1].span = jobs[1].span0 = 3.2f;
    jobs[0].iter = jobs[1].iter = ITER_MIN;

    if (!allocate())
    {
        form_alert(1, "[1][No Alt-RAM: this needs|the PSRAM.][ OK ]");
        v_clsvwk(vdi);
        appl_exit();
        return 1;
    }

    build_palette();

    /* four windows, tiled two by two: a demonstration of sharing has to
       show all three computations at once, so none of them may hide */
    wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
    bw = (short)(dw / 2);
    bh = (short)(dh / 2);

    for (i = 0; i < NJOBS + 1; i++)
    {
        short win;

        ox = (short)(dx + (i & 1) * bw);
        oy = (short)(dy + (i >> 1) * bh);
        win = wind_create((i < NJOBS) ? KIND : PKIND, dx, dy, dw, dh);
        if (win < 0)
            break;

        if (i < NJOBS)
        {
            struct job *j = &jobs[i];

            j->win = win;
            wind_set_name(win, j->name);
            wind_open(win, ox, oy, bw, bh);
            relayout(j);
        }
        else
        {
            panel = win;
            wind_set_name(win, " shares ");
            wind_open(win, ox, oy, bw, bh);
            wind_get(win, WF_WORKXYWH, &px, &py, &pw, &ph);
        }
    }

    /*
     * Without its other half this program has nothing to show, so it
     * goes rather than sitting there with four empty windows -- and
     * leaving the desktop and its accessories free, which matters when
     * the way to fix it is to upload a new one.
     */
    if (!start_tasks())
    {
        stop_tasks();
        form_alert(1, "[1][The real-time core|refused a task.][ OK ]");
        running = 0;
    }

    while (running)
    {
        short msg[8], mx, my, button, kstate, key, clicks;
        short ev = evnt_multi_button_timer(TICK_MS, &mx, &my, &button,
                                           &kstate, &key, &clicks, msg);

        if (ev & MU_MESAG)
        {
            switch (msg[0])
            {
            case WM_REDRAW:
                for (i = 0; i < NJOBS; i++)
                    if (jobs[i].win == msg[3])
                        redraw_win(jobs[i].win, msg[4], msg[5], msg[6], msg[7],
                                   &jobs[i]);
                if (panel == msg[3])
                    redraw_win(panel, msg[4], msg[5], msg[6], msg[7], 0);
                break;

            case WM_TOPPED:
                wind_set(msg[3], WF_TOP, 0, 0, 0, 0);
                break;

            case WM_MOVED:
            case WM_SIZED:
                wind_set(msg[3], WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
                for (i = 0; i < NJOBS; i++)
                    if (jobs[i].win == msg[3])
                        relayout(&jobs[i]);
                if (panel == msg[3])
                {
                    wind_get(panel, WF_WORKXYWH, &px, &py, &pw, &ph);
                    redraw_win(panel, px, py, pw, ph, 0);
                }
                break;

            case AC_CLOSE:
            case WM_CLOSED:
                running = 0;
                break;

            case IRK_MSG:
                break;              /* nothing reports; the timer looks */
            }
        }

        if ((ev & MU_BUTTON) && panel >= 0
            && mx >= px && mx < px + pw && my >= py && my < py + ph)
            panel_click(mx, my);

        /*
         * Drawn on the clock's word, not the tasks'.  A task finishing a
         * line every few milliseconds could notify() us that often --
         * notifications coalesce, so it would not flood the AES -- but we
         * would still blit a whole window each time.
         *
         * Only what changed, and the whole round inside one update: each
         * wind_update()/graf_mouse() pair is an AES call that may hand
         * the processor to somebody else, so doing them once instead of
         * three times steadies the interval as well as shortening it.
         */
        {
            short any = 0;

            for (i = 0; i < NJOBS; i++)
                if (jobs[i].win >= 0 && jobs[i].frames != shown[i])
                    any = 1;

            if (any)
            {
                wind_update(BEG_UPDATE);
                graf_mouse(M_OFF, 0L);
                for (i = 0; i < NJOBS; i++)
                    if (jobs[i].win >= 0 && jobs[i].frames != shown[i])
                    {
                        shown[i] = jobs[i].frames;
                        clip_and_draw(jobs[i].win, jobs[i].wx, jobs[i].wy,
                                      jobs[i].ww, jobs[i].wh, &jobs[i]);
                    }
                graf_mouse(M_ON, 0L);
                wind_update(END_UPDATE);
            }
        }

        if (++tick >= STAT_TICKS)
        {
            tick = 0;
            statistics();
            if (panel >= 0)
                redraw_win(panel, px, py, pw, ph, 0);
            late_us = 0;            /* worst case since the last look */
        }
    }

    stop_tasks();
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].win >= 0)
        {
            wind_close(jobs[i].win);
            wind_delete(jobs[i].win);
        }
    if (panel >= 0)
    {
        wind_close(panel);
        wind_delete(panel);
    }
    v_clsvwk(vdi);
    appl_exit();
    return 0;
}
