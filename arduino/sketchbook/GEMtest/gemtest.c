/*
 * gemtest.c - GEMtest: exercise the AES and VDI bindings and say what
 *             they did
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Every binding has to put its arguments where the operating system looks
 * for them, and a wrong slot is silent: the call returns, something
 * happens, and it is not what was asked for.  This program asks.
 *
 * Page 0 checks what arithmetic can check and prints expected against
 * actual.  The other pages draw things whose shape is the answer -- an
 * arc whose radius came from the wrong slot does not come out the wrong
 * size, it does not come out at all.
 */

#include <mint/osbind.h>
#include "gem.h"
#include <stdio.h>                      /* sprintf */
#include <string.h>

#define WHITE       0
#define BLACK       1
#define RED         2
#define GREEN       3
#define BLUE        4

#define KIND        (NAME | CLOSER | MOVER | SIZER)
#define PAGES       7

/*
 * An object's colour word, as include/obdefs.h lays it out: the interior
 * colour in the lowest four bits, then three bits of fill pattern, one
 * bit of text writing mode, four of text colour, four of border colour,
 * and eight of border thickness above that.
 */
#define OBSPEC(thick, framecol, textcol, mode, pattern, incol) \
    (((long)(thick) << 16) | ((long)(framecol) << 12) \
     | ((long)(textcol) << 8) | ((long)(mode) << 7) \
     | ((long)(pattern) << 4) | (long)(incol))

#define PAT_SOLID   7

static short vdi;                       /* our virtual workstation */
static short win;
static short wx, wy, ww, wh;            /* the work area, screen coordinates */
static short page;
static short wchar, hchar, wbox, hbox;  /* from graf_handle() */

/* ---- writing lines into the window ---------------------------------- */

static short line_y;                    /* where the next line goes */

static void say(const char *s)
{
    vst_color(vdi, BLACK);
    v_gtext(vdi, (short)(wx + 2), line_y, s);
    line_y = (short)(line_y + hchar + 1);
}

static void sayf(const char *fmt, long a, long b)
{
    char buf[80];

    sprintf(buf, fmt, a, b);
    say(buf);
}

/*
 * The results of page 0, measured once and kept.
 *
 * Drawing happens once for every visible piece of the window, so a check
 * that draws its own subject -- v_get_pixel needs a pixel to read --
 * would run several times, clipped differently each time, and fail
 * whenever its probe fell outside the piece being drawn.  Measuring is
 * not drawing; it is done once, on its own, and the page reports it.
 */
#define MAX_LINES   24
static char results[MAX_LINES][48];
static short nresults;

static void result(const char *what, long want, long got, long slack)
{
    long d = (got > want) ? got - want : want - got;

    if (nresults >= MAX_LINES)
        return;
    sprintf(results[nresults++], "%-11s %5ld %5ld %s", what, want, got,
            (d <= slack) ? "ok" : "FAIL");
}

static void clear_page(void)
{
    short r[4];

    r[0] = wx; r[1] = wy;
    r[2] = (short)(wx + ww - 1); r[3] = (short)(wy + wh - 1);
    vsf_color(vdi, WHITE);
    vsf_interior(vdi, FIS_SOLID);
    vr_recfl(vdi, r);
    line_y = (short)(wy + hchar);
}

/* ---- page 0: what can be checked by arithmetic ---------------------- */

/*
 * A dialogue tree, built here rather than loaded from a resource file.
 * form_center() and form_do() do not care where a tree came from, and on
 * a machine whose programs are compiled beside the system there is no
 * reason to keep one in a separate file.  It doubles as the subject of
 * the objc_offset() and form_center() checks, whose right answers follow
 * from the numbers below.
 */
#define DLG_W       200
#define DLG_H       80
#define KID_X       20
#define KID_Y       30

static char dlg_title[] = "A dialogue, built in code";
static char dlg_ok[] = "OK";
static char dlg_cancel[] = "Cancel";

static OBJECT dialogue[] = {
    /* 0: the box itself */
    { -1, 1, 3, G_BOX, NONE, NORMAL,
      OBSPEC(2, BLACK, BLACK, 1, PAT_SOLID, WHITE), 0, 0, DLG_W, DLG_H },
    /* 1: a string, at a known offset -- objc_offset() must agree */
    { 2, -1, -1, G_STRING, NONE, NORMAL,
      (long)dlg_title, KID_X, KID_Y, 24 * 8, 8 },
    /* 2: OK, the default */
    { 3, -1, -1, G_BUTTON, SELECTABLE | DEFAULT | EXIT, NORMAL,
      (long)dlg_ok, 20, 55, 50, 16 },
    /* 3: Cancel, and the last object of the tree */
    { 0, -1, -1, G_BUTTON, SELECTABLE | EXIT | LASTOB, NORMAL,
      (long)dlg_cancel, 120, 55, 50, 16 }
};

static void run_checks(void)
{
    short extent[8], rgb[3], want_rgb[3];
    short ox, oy, cx, cy, cw, ch;
    short pel, index;
    short dx, dy, dw, dh;
    short r[4];

    nresults = 0;

    /*
     * vqt_extent: the system font is a fixed width, so the width of a
     * string of n characters is n * wchar and nothing else.  If the four
     * corners come out of the wrong end of ptsout this is nonsense.
     */
    vqt_extent(vdi, "ABCDEFGH", extent);
    result("vqt_extent", 8L * wchar, (long)(extent[2] - extent[0]), 0L);
    result("vqt_height", (long)hchar, (long)(extent[5] - extent[1]), 0L);

    /*
     * vs_color takes thousandths and the hardware rounds to what it has,
     * so the way back is close rather than equal.  What matters is that
     * the three components come back in the right order: a swapped slot
     * shows up as red reading 0 and blue reading 1000.
     */
    want_rgb[0] = 1000; want_rgb[1] = 500; want_rgb[2] = 0;
    vs_color(vdi, 16, want_rgb);
    vq_color(vdi, 16, VQ_REQUESTED, rgb);
    result("vs_color r", 1000L, (long)rgb[0], 40L);
    result("vs_color g", 500L, (long)rgb[1], 40L);
    result("vs_color b", 0L, (long)rgb[2], 40L);

    /*
     * v_get_pixel: fill a square with a known pen and ask what is there.
     * The index is the pen; the pel is what the hardware holds, which on
     * a truecolor screen is not the pen and is not checked.
     */
    r[0] = (short)(wx + ww - 20); r[1] = (short)(wy + 2);
    r[2] = (short)(wx + ww - 5);  r[3] = (short)(wy + 17);
    vsf_color(vdi, RED);
    vsf_interior(vdi, FIS_SOLID);
    vr_recfl(vdi, r);
    v_get_pixel(vdi, (short)(wx + ww - 12), (short)(wy + 9), &pel, &index);
    result("v_get_pixel", (long)RED, (long)index, 0L);

    /*
     * objc_offset: the child sits at KID_X/KID_Y inside a tree whose root
     * is wherever form_center put it, so the difference between the two
     * offsets is the one thing that is known independently of that.
     */
    form_center(dialogue, &cx, &cy, &cw, &ch);
    objc_offset(dialogue, 0, &ox, &oy);
    result("form_ctr w", (long)DLG_W, (long)cw, 0L);
    result("form_ctr h", (long)DLG_H, (long)ch, 0L);
    result("objc_off 0", (long)cx, (long)ox, 0L);
    objc_offset(dialogue, 1, &ox, &oy);
    result("objc_off 1", (long)(cx + KID_X), (long)ox, 0L);

    /*
     * wind_calc, both ways: ask for the work area of a window of a known
     * outside size, then ask for the outside of that work area.  It has
     * to come back where it started.
     */
    wind_calc(WC_WORK, KIND, 100, 100, 160, 120, &dx, &dy, &dw, &dh);
    wind_calc(WC_BORDER, KIND, dx, dy, dw, dh, &dx, &dy, &dw, &dh);
    result("wind_calc x", 100L, (long)dx, 0L);
    result("wind_calc w", 160L, (long)dw, 0L);

}

/* and the page is only the reporting of it */
static void page_checks(void)
{
    short i;

    say("bindings: expected, actual");
    say("");
    for (i = 0; i < nresults; i++)
        say(results[i]);
    say("");
    say("tap to turn the page");
}

/* ---- page 1: the graphics primitives -------------------------------- */

/*
 * Each of these takes its arguments from its own corner of ptsin, so a
 * mistake shows as a shape that is absent rather than wrong.  Drawn
 * against a frame of known size so that "absent" is obvious.
 */
static void page_primitives(void)
{
    short r[4];
    short cell_w = (short)(ww / 4);
    short cell_h = (short)((wh - hchar - 4) / 2);
    short i;

    say("v_bar v_circle v_ellipse v_arc");

    for (i = 0; i < 8; i++)
    {
        short cx = (short)(wx + (i % 4) * cell_w + cell_w / 2);
        short cy = (short)(wy + hchar + 4 + (i / 4) * cell_h + cell_h / 2);
        short rad = (short)((cell_w < cell_h ? cell_w : cell_h) / 2 - 3);

        r[0] = (short)(cx - rad); r[1] = (short)(cy - rad);
        r[2] = (short)(cx + rad); r[3] = (short)(cy + rad);

        vsf_interior(vdi, FIS_SOLID);
        vsf_color(vdi, (short)(i + 1));
        vsl_color(vdi, BLACK);

        switch (i)
        {
        case 0: v_bar(vdi, r); break;
        case 1: v_circle(vdi, cx, cy, rad); break;
        case 2: v_ellipse(vdi, cx, cy, rad, (short)(rad / 2)); break;
        case 3: v_arc(vdi, cx, cy, rad, 0, 1800); break;
        case 4: v_pieslice(vdi, cx, cy, rad, 300, 2700); break;
        case 5: v_rbox(vdi, r); break;
        case 6: v_rfbox(vdi, r); break;
        case 7:
            {
                /* a filled triangle, to try v_fillarea */
                short xy[6];

                xy[0] = cx;                 xy[1] = (short)(cy - rad);
                xy[2] = (short)(cx + rad);  xy[3] = (short)(cy + rad);
                xy[4] = (short)(cx - rad);  xy[5] = (short)(cy + rad);
                v_fillarea(vdi, 3, xy);
            }
            break;
        }
    }
}

/* ---- page 2: colour ------------------------------------------------- */

/*
 * Sixteen pens set to a ramp by vs_color and then drawn.  On a truecolor
 * screen this is the only way a program gets at more than the colours it
 * was given, so a smooth ramp here is the whole point; a banded or
 * repeating one means the pen index went somewhere unexpected.
 */
static void page_colour(void)
{
    short r[4];
    short i;
    short band = (short)(ww / 16);

    say("vs_color: 16 pens, a ramp");

    for (i = 0; i < 16; i++)
    {
        short rgb[3];

        rgb[0] = (short)(i * 1000 / 15);
        rgb[1] = (short)(300 + i * 400 / 15);
        rgb[2] = (short)(1000 - i * 1000 / 15);
        vs_color(vdi, (short)(16 + i), rgb);

        r[0] = (short)(wx + i * band);
        r[1] = (short)(wy + hchar + 6);
        r[2] = (short)(wx + (i + 1) * band - 1);
        r[3] = (short)(wy + wh - 4);
        vsf_color(vdi, (short)(16 + i));
        vsf_interior(vdi, FIS_SOLID);
        vr_recfl(vdi, r);
    }
}

/* ---- page 3: text --------------------------------------------------- */

static void page_text(void)
{
    static const struct { short effect; const char *name; } fx[] = {
        { 0,              "plain" },
        { TF_THICKENED,   "thickened" },
        { TF_LIGHTENED,   "lightened" },
        { TF_SKEWED,      "skewed" },
        { TF_UNDERLINED,  "underlined" },
        { TF_OUTLINED,    "outlined" },
        { TF_SHADOWED,    "shadowed" }
    };
    short i;

    say("vst_effects, and vqt_extent as a box");

    for (i = 0; i < (short)(sizeof(fx) / sizeof(fx[0])); i++)
    {
        short extent[8], box[10];
        short y = (short)(wy + hchar * (i + 3));

        vst_effects(vdi, fx[i].effect);
        vst_color(vdi, BLACK);
        v_gtext(vdi, (short)(wx + 20), y, fx[i].name);

        /*
         * The box vqt_extent says the string occupies.  It is measured
         * with the effect in force, so a thickened string should come out
         * wider than a plain one -- and the frame should sit on the text,
         * not beside it.
         */
        vqt_extent(vdi, fx[i].name, extent);
        box[0] = (short)(wx + 20 + extent[0]);
        box[1] = (short)(y + extent[1]);
        box[2] = (short)(wx + 20 + extent[2]);
        box[3] = (short)(y + extent[3]);
        box[4] = (short)(wx + 20 + extent[4]);
        box[5] = (short)(y + extent[5]);
        box[6] = (short)(wx + 20 + extent[6]);
        box[7] = (short)(y + extent[7]);
        box[8] = box[0];
        box[9] = box[1];
        vsl_color(vdi, RED);
        v_pline(vdi, 5, box);
    }
    vst_effects(vdi, 0);
}

/* ---- page 4: the raster copies -------------------------------------- */

/*
 * A shape and its mask, an arrow, one bit per pixel.  vrt_cpyfm takes a
 * single-plane form like this and paints the set bits in one colour and
 * the clear ones in another -- or leaves them alone, which is what makes
 * a shape with a hole in it possible.  vro_cpyfm, beside it, copies whole
 * pixels and needs a form in the screen's own format.
 */
#define ART_W       16
#define ART_H       16

static unsigned short arrow[ART_H] = {
    0x0180, 0x03c0, 0x07e0, 0x0ff0,
    0x1ff8, 0x3ffc, 0x7ffe, 0xffff,
    0x0180, 0x0180, 0x0180, 0x0180,
    0x0180, 0x0180, 0x0180, 0x0180
};

static unsigned short colours[ART_W * ART_H];

static void page_raster(void)
{
    MFDB mask, form, screen;
    short xy[8];
    short i, j;

    say("vrt_cpyfm (mask) and vro_cpyfm");

    memset(&screen, 0, sizeof(screen));     /* fd_addr 0: the screen */

    mask.fd_addr = arrow;
    mask.fd_w = ART_W;
    mask.fd_h = ART_H;
    mask.fd_wdwidth = 1;                    /* one plane: a word a row */
    mask.fd_stand = 0;
    mask.fd_nplanes = 1;
    mask.fd_r1 = mask.fd_r2 = mask.fd_r3 = 0;

    /* the same shape in colour, for the opaque copy */
    for (j = 0; j < ART_H; j++)
        for (i = 0; i < ART_W; i++)
            colours[j * ART_W + i] = (unsigned short)
                ((arrow[j] & (0x8000 >> i)) ? 0xf800 : 0x07e0);
    form.fd_addr = colours;
    form.fd_w = ART_W;
    form.fd_h = ART_H;
    form.fd_wdwidth = ART_W;                /* 16 bpp: a word is a pixel */
    form.fd_stand = 0;
    form.fd_nplanes = 16;
    form.fd_r1 = form.fd_r2 = form.fd_r3 = 0;

    /* four copies of the mask, at 3x scale, in different colour pairs */
    for (i = 0; i < 4; i++)
    {
        xy[0] = 0; xy[1] = 0;
        xy[2] = ART_W - 1; xy[3] = ART_H - 1;
        xy[4] = (short)(wx + 10 + i * 48);
        xy[5] = (short)(wy + hchar + 10);
        xy[6] = (short)(xy[4] + ART_W * 2 - 1);
        xy[7] = (short)(xy[5] + ART_H * 2 - 1);
        vrt_cpyfm(vdi, MD_TRANS_FG, xy, &mask, &screen,
                  (short)(i + 1), WHITE);
    }

    /* and the colour one, opaque */
    xy[0] = 0; xy[1] = 0;
    xy[2] = ART_W - 1; xy[3] = ART_H - 1;
    xy[4] = (short)(wx + 10);
    xy[5] = (short)(wy + hchar + 10 + ART_H * 2 + 8);
    xy[6] = (short)(xy[4] + ART_W * 2 - 1);
    xy[7] = (short)(xy[5] + ART_H * 2 - 1);
    vro_cpyfm(vdi, S_ONLY, xy, &form, &screen);
}

/* ---- page 5: the dialogue ------------------------------------------- */

static short dlg_exit = -1;

static void page_dialogue(void)
{
    say("form_center, form_dial, objc_draw, form_do");
    say("");
    if (dlg_exit < 0)
        say("the dialogue did not open");
    else
        sayf("form_do returned %ld  (2 = OK, 3 = Cancel)",
             (long)dlg_exit, 0L);
}

static void run_dialogue(void)
{
    short cx, cy, cw, ch;

    form_center(dialogue, &cx, &cy, &cw, &ch);
    dialogue[0].ob_x = cx;
    dialogue[0].ob_y = cy;

    form_dial(FMD_START, 0, 0, 0, 0, cx, cy, cw, ch);
    objc_draw(dialogue, 0, MAX_DEPTH, cx, cy, cw, ch);
    dlg_exit = form_do(dialogue, 0);
    form_dial(FMD_FINISH, 0, 0, 0, 0, cx, cy, cw, ch);

    /* a button stays selected after form_do; let it go again */
    dialogue[2].ob_state = NORMAL;
    dialogue[3].ob_state = NORMAL;
}

/* ---- page 6: the file selector -------------------------------------- */

static char fsel_path[128] = "C:\\*.*";
static char fsel_name[64] = "";
static short fsel_button = -1;
static short fsel_done;

static void page_selector(void)
{
    say("fsel_input");
    say("");
    if (!fsel_done)
        say("the selector did not open");
    else
    {
        sayf("button %ld  (1 = OK, 0 = Cancel)", (long)fsel_button, 0L);
        say(fsel_path);
        say(fsel_name[0] ? fsel_name : "(nothing chosen)");
    }
}

static void run_selector(void)
{
    fsel_input(fsel_path, fsel_name, &fsel_button);
    fsel_done = 1;
}

/* ---- drawing, clipped to the window -------------------------------- */

static void draw_page(void)
{
    char where[40];

    clear_page();
    sprintf(where, "page %d of %d -- tap to turn over", page + 1, PAGES);
    vst_color(vdi, BLACK);
    v_gtext(vdi, (short)(wx + 2), (short)(wy + wh - 2), where);

    switch (page)
    {
    case 0: page_checks(); break;
    case 1: page_primitives(); break;
    case 2: page_colour(); break;
    case 3: page_text(); break;
    case 4: page_raster(); break;
    case 5: page_dialogue(); break;
    case 6: page_selector(); break;
    }
}

static void redraw(short x, short y, short w, short h)
{
    short rx, ry, rw, rh, clip[4];

    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, 0L);

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
            draw_page();
        }
        wind_get(win, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
    }

    graf_mouse(M_ON, 0L);
    wind_update(END_UPDATE);
}

static void layout(void)
{
    wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
}

/* ---- the program ---------------------------------------------------- */

int main(void)
{
    short apid, dx, dy, dw, dh, x, y, w, h;
    int running = 1;

    apid = appl_init();
    if (apid < 0)
        return 1;

    vdi = v_opnvwk(graf_handle(&wchar, &hchar, &wbox, &hbox));
    if (!vdi)
    {
        appl_exit();
        return 1;
    }

    wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
    wind_calc(WC_BORDER, KIND, 0, 0, dw, (short)(dh - 20), &x, &y, &w, &h);
    win = wind_create(KIND, dx, dy, dw, dh);
    if (win < 0)
    {
        v_clsvwk(vdi);
        appl_exit();
        return 1;
    }
    wind_set_name(win, " GEMtest ");
    wind_open(win, dx, dy, (short)w, (short)h);
    layout();

    /*
     * Measured now, with the window open and nothing clipped, and not
     * again unless the window changes size -- v_get_pixel draws its own
     * subject, which has no business happening inside a redraw.
     */
    run_checks();

    while (running)
    {
        short msg[8], mx, my, button, kstate, key, clicks;
        short ev = evnt_multi_button_timer(1000, &mx, &my, &button,
                                          &kstate, &key, &clicks, msg);

        if (ev & MU_MESAG)
        {
            switch (msg[0])
            {
            case WM_REDRAW:
                if (msg[3] == win)
                    redraw(msg[4], msg[5], msg[6], msg[7]);
                break;
            case WM_TOPPED:
                wind_set(win, WF_TOP, 0, 0, 0, 0);
                break;
            case WM_MOVED:
            case WM_SIZED:
                wind_set(win, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
                layout();
                if (msg[0] == WM_SIZED)
                    run_checks();
                redraw(wx, wy, ww, wh);
                break;
            case AC_CLOSE:
            case WM_CLOSED:
                running = 0;
                break;
            }
        }

        if ((ev & MU_BUTTON)
            && mx >= wx && mx < wx + ww && my >= wy && my < wy + wh)
        {
            /*
             * One rule: a tap turns the page.  The two pages that open
             * something of their own do it on arrival, not on the tap --
             * otherwise the tap means two different things depending on
             * where you are, and there is no way off those pages at all.
             * A dialogue and a selector draw over the window, so the page
             * is drawn again once they are gone.
             */
            page = (short)((page + 1) % PAGES);
            if (page == 5)
                run_dialogue();
            else if (page == 6)
                run_selector();
            redraw(wx, wy, ww, wh);
        }
    }

    wind_close(win);
    wind_delete(win);
    v_clsvwk(vdi);
    appl_exit();
    return 0;
}
