/*
 * touchcal.c - touch screen calibration for pTOS3000
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * A desk accessory (TOUCHCAL.ACC; it also runs as a program):
 *
 *  - at start-up it loads the calibration saved in C:\TOUCHCAL.INF and
 *    hands it to the touch driver -- unless it was just calibrated by hand
 *    at boot (finger on the screen while the machine starts);
 *  - "Touch calibration" in the Desk menu asks for nine crosses to be
 *    touched (least-squares fit), uses the result at once and offers to save it.
 *
 * It talks to the driver through the _TCH cookie (pTOS include/touch.h).
 */

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include "touch.h"
#include "gem.h"

#define CAL_FILE        "C:\\TOUCHCAL.INF"
#define CAL_MAGIC       0x54434831L     /* 'TCH1' */

struct cal_file
{
    long magic;
    struct tch_cal cal;
};

static const char menu_name[] = "  Touch calibration";

static struct tch_api *tch;
static short vh;                        /* VDI workstation */
static short scr_w, scr_h;
#define NPOINTS         9
static short pts[NPOINTS][2];           /* a 3 x 3 grid of crosses */

/* ---- calibration file ---- */

static void load_cal(void)
{
    struct cal_file f;
    long fh = Fopen(CAL_FILE, 0);

    if (fh < 0)
        return;
    if (Fread((short)fh, sizeof(f), &f) == sizeof(f)
        && f.magic == CAL_MAGIC && f.cal.div != 0)
        tch->set_cal(&f.cal);
    Fclose((short)fh);
}

static int save_cal(void)
{
    struct cal_file f;
    long fh, n;

    f.magic = CAL_MAGIC;
    tch->get_cal(&f.cal);
    fh = Fcreate(CAL_FILE, 0);
    if (fh < 0)
        return -1;
    n = Fwrite((short)fh, sizeof(f), &f);
    Fclose((short)fh);
    return n == sizeof(f) ? 0 : -1;
}

/* ---- drawing ---- */

static void clear_screen(void)
{
    short r[4] = { 0, 0, scr_w - 1, scr_h - 1 };

    vsf_interior(vh, FIS_SOLID);
    vsf_color(vh, 0);
    vr_recfl(vh, r);
}

static void cross(const short *p, short color)
{
    short h[4] = { p[0] - 12, p[1], p[0] + 12, p[1] };
    short v[4] = { p[0], p[1] - 12, p[0], p[1] + 12 };
    short b[10] = { p[0] - 3, p[1] - 3, p[0] + 3, p[1] - 3,
                    p[0] + 3, p[1] + 3, p[0] - 3, p[1] + 3,
                    p[0] - 3, p[1] - 3 };

    vsl_color(vh, color);
    v_pline(vh, 2, h);
    v_pline(vh, 2, v);
    v_pline(vh, 5, b);
}

/* ---- touch input (the mouse is off meanwhile) ---- */

static int touched(short *rx, short *ry)
{
    return (int)tch->get_raw(rx, ry);
}

static void wait_lifted(void)
{
    short rx, ry;

    while (touched(&rx, &ry))
        evnt_timer(10);
    evnt_timer(200);
}

/* raw position of the next touch, averaged while it lasts (at most 0.5 s) */
static void read_touch(short *raw)
{
    long sx = 0, sy = 0;
    short rx, ry;
    int n = 0;

    while (!touched(&rx, &ry))
        evnt_timer(10);
    evnt_timer(50);                     /* let it settle */
    while (touched(&rx, &ry) && n < 50)
    {
        sx += rx;
        sy += ry;
        n++;
        evnt_timer(10);
    }
    if (n == 0)
    {
        sx = rx;
        sy = ry;
        n = 1;
    }
    raw[0] = (short)(sx / n);
    raw[1] = (short)(sy / n);
}

/* ---- the calibration dialogue ---- */

static void calibrate(void)
{
    struct tch_cal old, cal;
    short raw[NPOINTS][2];
    short clip[4] = { 0, 0, scr_w - 1, scr_h - 1 };
    int i, choice;

    tch->get_cal(&old);

    for (;;)
    {
        /* the whole screen is ours until the crosses are done */
        wind_update(BEG_UPDATE);
        wind_update(BEG_MCTRL);
        graf_mouse(M_OFF, 0);
        tch->set_mouse(0);

        vs_clip(vh, 1, clip);
        vswr_mode(vh, MD_REPLACE);
        clear_screen();
        vst_color(vh, 1);
        v_gtext(vh, 8, scr_h / 2 - 24, "Touch calibration:");
        v_gtext(vh, 8, scr_h / 2 - 8, "touch the centre of each cross.");

        wait_lifted();                  /* the tap that opened us */
        for (i = 0; i < NPOINTS; i++)
        {
            cross(pts[i], 1);
            read_touch(raw[i]);
            cross(pts[i], 0);
            wait_lifted();
        }

        if (tch_fit(&cal, (const short (*)[2])pts,
                    (const short (*)[2])raw, NPOINTS) == 0)
            tch->set_cal(&cal);

        tch->set_mouse(1);
        graf_mouse(M_ON, 0);
        wind_update(END_MCTRL);
        wind_update(END_UPDATE);
        form_dial(FMD_FINISH, 0, 0, 0, 0, 0, 0, scr_w, scr_h);  /* redraw all */

        choice = form_alert(1, "[2][Touch calibrated.|Save it for the|next start?]"
                               "[Save|Again|Cancel]");
        if (choice == 2)
            continue;
        if (choice == 3)
            tch->set_cal(&old);
        else if (save_cal() < 0)
            form_alert(1, "[3][Could not write|" CAL_FILE "][ OK ]");
        break;
    }
}

/* ---- start ---- */

static int init(void)
{
    short wchar, hchar, wbox, hbox, phys;

    if (Ssystem(S_GETCOOKIE, TCH_COOKIE, (long)&tch) != 0 || !tch
        || tch->version < TCH_VERSION)
        return -1;

    scr_w = tch->width;
    scr_h = tch->height;
    {
        int i;

        for (i = 0; i < NPOINTS; i++)
        {
            pts[i][0] = scr_w / 10 + (i % 3) * (scr_w * 4 / 10);
            pts[i][1] = scr_h / 10 + (i / 3) * (scr_h * 4 / 10);
        }
    }

    phys = graf_handle(&wchar, &hchar, &wbox, &hbox);
    vh = v_opnvwk(phys);
    return vh > 0 ? 0 : -1;
}

void start_main(BASEPAGE *bp);

void start_main(BASEPAGE *bp)
{
    short msg[8];
    short menu_id;
    int acc = bp->p_parent == 0;

    if (!acc)                           /* a program: give back the rest */
        Mshrink(bp, sizeof(BASEPAGE) + bp->p_tlen + bp->p_dlen + bp->p_blen);

    appl_init();
    if (init() < 0)
    {
        if (!acc)
        {
            form_alert(1, "[3][No touch screen|(_TCH cookie missing).][ OK ]");
            appl_exit();
            Pterm0();
        }
        for (;;)
            evnt_mesag(msg);            /* an accessory must never end */
    }

    if (!acc)
    {
        calibrate();
        v_clsvwk(vh);
        appl_exit();
        Pterm0();
    }

    /* accessory: bring the saved calibration in, unless the user has just
     * calibrated by hand at boot */
    {
        struct tch_cal c;

        if (!(tch->get_cal(&c) & TCH_BOOTCAL))
            load_cal();
    }

    menu_id = menu_register(gl_apid, menu_name);
    for (;;)
    {
        evnt_mesag(msg);
        if (msg[0] == AC_OPEN && msg[4] == menu_id)
            calibrate();
    }
}
