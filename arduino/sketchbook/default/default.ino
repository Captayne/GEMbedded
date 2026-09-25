/*
 * A GEM program: a window that can be moved, sized and closed.
 *
 * This is a whole program, not a fragment: it has its own main(), it
 * announces itself to the AES with appl_init(), and it waits for events
 * in its own loop.  There is no setup() and no loop() here, because that
 * is not how GEM works -- and because everything the AES and the VDI can
 * do is open to you.
 *
 * Draw only when GEM says so, and only where the window can be seen: the
 * rectangle list (WF_FIRSTXYWH, WF_NEXTXYWH) says which pieces those are.
 * Other windows may lie over yours, and they must stay untouched.
 *
 * To reach the other core -- cyclic tasks that keep time while GEM
 * draws -- add #include "irk.h" and look at the GEMbedClk example.
 */

#include "gem.h"

#define KIND        (NAME | CLOSER | MOVER | SIZER)

static short vdi;               /* our virtual workstation */
static short win;               /* our window */
static char  title[] = " Sketch ";

/* what is inside the window, in screen coordinates */
static short wx, wy, ww, wh;

static void layout(void)
{
    wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
}

/* ---- drawing ------------------------------------------------------ */

/*
 * Called once for every visible piece of the window, already clipped.
 * Everything is in screen coordinates, so draw where the work area is,
 * not where you would like it to be.
 */
static void draw(void)
{
    short xy[4];

    xy[0] = wx; xy[1] = wy;
    xy[2] = (short)(wx + ww - 1); xy[3] = (short)(wy + wh - 1);
    vsf_color(vdi, 0);                  /* white */
    vsf_interior(vdi, FIS_SOLID);
    vr_recfl(vdi, xy);

    vst_color(vdi, 1);                  /* black */
    v_gtext(vdi, (short)(wx + 8), (short)(wy + 20), "Hello from GEM");
}

/* draw(), clipped to each visible piece that lies in (x, y, w, h) */
static void redraw(short x, short y, short w, short h)
{
    short rx, ry, rw, rh, clip[4];

    wind_update(BEG_UPDATE);            /* nothing moves meanwhile */
    graf_mouse(M_OFF, 0L);              /* and nothing is drawn over */

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

/* ---- the program -------------------------------------------------- */

int main(void)
{
    short wchar, hchar, wbox, hbox;
    short msg[8], dx, dy, dw, dh, x, y, w, h;
    int   running = 1;

    if (appl_init() < 0)
        return 1;

    vdi = v_opnvwk(graf_handle(&wchar, &hchar, &wbox, &hbox));
    if (!vdi)
    {
        appl_exit();
        return 1;
    }

    /* a window of 160x120, in the middle of whatever the desktop leaves */
    wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
    wind_calc(WC_BORDER, KIND, 0, 0, 160, 120, &x, &y, &w, &h);
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

    while (running)
    {
        /* Waiting costs nothing: the kernel gives the processor to
           somebody else until something happens here. */
        short ev = evnt_multi_mesag_timer(100, msg);

        if (!(ev & MU_MESAG) || msg[3] != win)
            continue;

        switch (msg[0])
        {
        case WM_REDRAW:                 /* msg[4..7]: what to redraw */
            redraw(msg[4], msg[5], msg[6], msg[7]);
            break;
        case WM_TOPPED:                 /* clicked: bring it to the front */
            wind_set(win, WF_TOP, 0, 0, 0, 0);
            break;
        case WM_MOVED:
        case WM_SIZED:
            wind_set(win, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
            layout();
            redraw(wx, wy, ww, wh);
            break;
        case WM_CLOSED:                 /* the close box */
            running = 0;
            break;
        }
    }

    wind_close(win);
    wind_delete(win);
    v_clsvwk(vdi);
    appl_exit();
    return 0;
}
