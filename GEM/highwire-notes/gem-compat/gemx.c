/*
 * gemx.c - the calls gemlib has and the board package does not
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Three kinds of thing live here, and they are worth telling apart:
 *
 *   REAL CALLS.  appl_getinfo, rsrc_obfix, vq_extnd, vsf_udpat,
 *   vsl_udsty, vr_trnfm, v_opnwk -- ordinary AES and VDI entry points
 *   that our bindings never needed and that HighWire does.  These reach
 *   the operating system and do what they say.
 *
 *   SHAPE ADAPTERS.  gemlib and the board package disagree about the
 *   signature of five calls, so gem.h redirects the name here and this
 *   is where the two shapes meet.
 *
 *   STUBS.  The Speedo/GDOS calls.  HighWire reaches them only when
 *   has_fsm_gdos is true, which it decides from the cookie at start-up,
 *   and on this machine there is no font GDOS -- so they are never
 *   called.  They exist because the compiler still has to translate the
 *   branch that names them.  Each answers the way a VDI without a font
 *   GDOS answers, so that a caller which gets here anyway is told "no"
 *   rather than crashing.
 *
 * The parameter blocks are this file's own.  gem.c keeps its arrays
 * static, and copying thirty lines is a smaller price than making the
 * published binding export its internals for our sake.
 */

#include <gem.h>
#include <nkcc.h>
#include <string.h>

/*
 * gem.h redirects five names to the adapters below.  In this file the
 * redirection has to come off again, or the adapter calls itself.
 */
#undef v_opnvwk
#undef vst_font
#undef vst_point
#undef vrt_cpyfm
#undef objc_change

/* ==== the way to the operating system ================================= */

static short  aes_control[5];
static short  aes_global[16];
static short  aes_int_in[16];
static short  aes_int_out[16];
static long   aes_addr_in[4];
static long   aes_addr_out[2];

static void *aespb[6] = { aes_control, aes_global, aes_int_in,
                          aes_int_out, aes_addr_in, aes_addr_out };

static short aes(short op, short nintin, short nintout, short naddrin)
{
    register long r0 __asm__("r0") = 200;
    register void *r1 __asm__("r1") = aespb;

    aes_control[0] = op;
    aes_control[1] = nintin;
    aes_control[2] = nintout;
    aes_control[3] = naddrin;
    __asm__ volatile ("svc 2"
                      : "+r"(r0), "+r"(r1)
                      :
                      : "r2", "r3", "r12", "lr", "memory", "cc");
    return aes_int_out[0];
}

static short  contrl[12] __attribute__((aligned(4)));
static short  intin[128];
static short  ptsin[128];
static short  intout[128];
static short  ptsout[128];

static void *vdipb[5] = { contrl, intin, ptsin, intout, ptsout };

static void vdi(short handle, short op, short nptsin, short nintin)
{
    register long r0 __asm__("r0") = 0x73;
    register void *r1 __asm__("r1") = vdipb;

    contrl[0] = op;
    contrl[1] = nptsin;
    contrl[3] = nintin;
    contrl[5] = 0;
    contrl[6] = handle;
    __asm__ volatile ("svc 2"
                      : "+r"(r0), "+r"(r1)
                      :
                      : "r2", "r3", "r12", "lr", "memory", "cc");
}

/* ==== real calls ====================================================== */

/*
 * What the AES can do.  A program asks before it uses anything that not
 * every AES has; HighWire asks about windows and about the keyboard.
 */
short appl_getinfo(short type, short *out1, short *out2,
                   short *out3, short *out4)
{
    short ret;

    aes_int_in[0] = type;
    ret = aes(130, 1, 5, 0);
    if (out1) *out1 = aes_int_out[1];
    if (out2) *out2 = aes_int_out[2];
    if (out3) *out3 = aes_int_out[3];
    if (out4) *out4 = aes_int_out[4];
    return ret;
}

/*
 * appl_xgetinfo is NOT here: HighWire defines its own in AEI.c, and
 * two definitions do not link.
 */

/*
 * Turn an object's character coordinates into pixels.  A resource built
 * for an 8x8 system font has to be fixed up before it is drawn, and this
 * is the call that does it.
 */
short rsrc_obfix(OBJECT *tree, short object)
{
    aes_int_in[0] = object;
    aes_addr_in[0] = (long)tree;
    return aes(114, 1, 1, 1);
}

/* Everything the workstation can tell about itself: 45 words and six
   points.  HighWire asks for the colour depth and the pixel geometry. */
void vq_extnd(short handle, short owflag, short *work_out)
{
    short i;

    intin[0] = owflag;
    vdi(handle, 102, 0, 1);
    for (i = 0; i < 45; i++)
        work_out[i] = intout[i];
    for (i = 0; i < 12; i++)
        work_out[45 + i] = ptsout[i];
}

/* A fill pattern of one's own: 16 words per plane. */
void vsf_udpat(short handle, const short *pfill, short planes)
{
    short i, n = (short)(16 * (planes > 0 ? planes : 1));

    if (n > 128)
        n = 128;
    for (i = 0; i < n; i++)
        intin[i] = pfill[i];
    vdi(handle, 112, 0, n);
}

/* A line pattern of one's own: one word, one bit per pixel. */
void vsl_udsty(short handle, short pattern)
{
    intin[0] = pattern;
    vdi(handle, 113, 0, 1);
}

/*
 * Between the VDI's own bitmap arrangement and the one a program builds.
 * An MFDB with fd_stand set is in the standard form; this is the call
 * that moves it either way.
 */
void vr_trnfm(short handle, MFDB *src, MFDB *dst)
{
    /*
     * The two MFDBs go in whole, not split into halves as on the Atari.
     * contrl[] is pTOS's VDICONTROL (include/vdipb.h): seven words, a pad
     * word that exists only on ARM, and then two native pointers, so they
     * land at words 8 and 10 rather than at 7/8 and 9/10. The pad is what
     * keeps them four-byte aligned, and contrl is declared aligned(4) for
     * the same reason.
     *
     * Writing halves into 7/8 and 9/10 instead put the source pointer one
     * word low with its halves swapped, and the destination over the top
     * of it. That is why HighWire's splash drew as a black square: the
     * transform read its bitmap from an address that was never its own.
     */
    *(MFDB **)&contrl[8] = src;
    *(MFDB **)&contrl[10] = dst;
    vdi(handle, 110, 0, 0);
}

/*
 * Open a physical workstation.  A GEM program normally opens a virtual
 * one on the screen the AES already has (v_opnvwk); this is the other
 * road, and HighWire's Nice_VDI.c offers it.
 */
void v_opnwk(const short *work_in, short *handle, short *work_out)
{
    short i;

    for (i = 0; i < 11; i++)
        intin[i] = work_in[i];
    vdi(0, 1, 0, 11);
    *handle = contrl[6];
    for (i = 0; i < 45; i++)
        work_out[i] = intout[i];
    for (i = 0; i < 12; i++)
        work_out[45 + i] = ptsout[i];
}

/*
 * Rectangle intersection, which gemlib brings along and which touches no
 * operating system at all.  dst becomes the overlap, and the answer says
 * whether there was one.
 */
short rc_intersect(const GRECT *src, GRECT *dst)
{
    short x = (src->g_x > dst->g_x ? src->g_x : dst->g_x);
    short y = (src->g_y > dst->g_y ? src->g_y : dst->g_y);
    short r = (src->g_x + src->g_w < dst->g_x + dst->g_w
               ? src->g_x + src->g_w : dst->g_x + dst->g_w);
    short b = (src->g_y + src->g_h < dst->g_y + dst->g_h
               ? src->g_y + src->g_h : dst->g_y + dst->g_h);

    dst->g_x = x;
    dst->g_y = y;
    dst->g_w = (short)(r - x);
    dst->g_h = (short)(b - y);

    return (short)(dst->g_w > 0 && dst->g_h > 0);
}

/* ==== shape adapters ================================================== */

/*
 * gemlib fills a work_out array and returns nothing; the board package
 * takes the physical handle and returns the new one.  The caller's
 * handle goes in and comes back out, which is what HighWire's Nice_VDI.c
 * expects (it passes &i and then reads i).
 */
void gemlib_v_opnvwk(short *work_in, short *handle, short *work_out)
{
    short i;

    for (i = 0; i < 11; i++)
        intin[i] = work_in[i];
    contrl[6] = *handle;
    vdi(*handle, 100, 0, 11);
    *handle = contrl[6];
    if (work_out) {
        for (i = 0; i < 45; i++)
            work_out[i] = intout[i];
        for (i = 0; i < 12; i++)
            work_out[45 + i] = ptsout[i];
    }
}

/* gemlib returns the font that was actually set, which is not always the
   one asked for.  HighWire's fontbase.c checks exactly that. */
short gemlib_vst_font(short handle, short font)
{
    intin[0] = font;
    vdi(handle, 21, 0, 1);
    return intout[0];
}

/* gemlib returns the point size actually set, for the same reason. */
short gemlib_vst_point(short handle, short point,
                       short *cw, short *ch, short *bw, short *bh)
{
    intin[0] = point;
    vdi(handle, 107, 0, 1);
    if (cw) *cw = ptsout[0];
    if (ch) *ch = ptsout[1];
    if (bw) *bw = ptsout[2];
    if (bh) *bh = ptsout[3];
    return intout[0];
}

/* gemlib passes the two colours as an array, which is the VDI's own
   arrangement; the board package takes them apart. */
void gemlib_vrt_cpyfm(short handle, short mode, const short *pxy,
                      const MFDB *src, const MFDB *dst, const short *colour)
{
    vrt_cpyfm(handle, mode, pxy, src, dst, colour[0], colour[1]);
}

/* gemlib has a reserved argument after the object number. */
short gemlib_objc_change(OBJECT *tree, short obj, short resvd,
                         short x, short y, short w, short h,
                         short newstate, short redraw)
{
    (void)resvd;
    return objc_change(tree, obj, x, y, w, h, newstate, redraw);
}

/*
 * evnt_multi() with the arguments gathered into two blocks, which is how
 * gemlib passes them and how HighWire's event loop is written.  Twenty-two
 * arguments are a lot to spell out at every call site.
 */
short evnt_multi_fast(const EVMULT_IN *in, short *msg, EVMULT_OUT *out)
{
    short mx = 0, my = 0, button = 0, kstate = 0, kreturn = 0, breturn = 0;
    unsigned long ms = ((unsigned long)(unsigned short)in->emi_thigh << 16)
                     | (unsigned short)in->emi_tlow;
    short ev;

    ev = evnt_multi(in->emi_flags, in->emi_bclicks, in->emi_bmask,
                    in->emi_bstate,
                    in->emi_m1leave, in->emi_m1.g_x, in->emi_m1.g_y,
                    in->emi_m1.g_w, in->emi_m1.g_h,
                    in->emi_m2leave, in->emi_m2.g_x, in->emi_m2.g_y,
                    in->emi_m2.g_w, in->emi_m2.g_h,
                    msg, ms,
                    &mx, &my, &button, &kstate, &kreturn, &breturn);

    if (out) {
        out->emo_mouse.p_x = mx;
        out->emo_mouse.p_y = my;
        out->emo_mbutton   = button;
        out->emo_kmeta     = kstate;
        out->emo_kreturn   = kreturn;
        out->emo_mclicks   = breturn;
    }
    return ev;
}

/* ==== the keyboard ==================================================== */

/*
 * There is no keyboard table to inspect here and no AES version to work
 * around, so there is nothing to set up.
 */
short nkc_init(void)
{
    return 0;
}

/*
 * Keys that carry a character and are still keys.
 *
 * Backspace, Return, Tab, Escape and Delete all produce one: 0x08, 0x0d,
 * 0x09, 0x1b, 0x7f.  Deciding by "did it produce a character" therefore
 * got all five wrong, and handed HighWire the bare control character
 * where it was comparing against NK_BS, NK_RET and NK_DEL.  Nothing
 * matched, so they fell through to the branch that inserts a character --
 * and the system font draws 0x08 as a tick, which is what backspacing
 * into a text field produced.
 *
 * Deciding by scancode instead is unambiguous: these six are keys, and
 * every other key that carries a character is a character.
 */
static int is_editing_key(short scan)
{
    switch (scan)
    {
    case 0x01:      /* Esc */
    case 0x0e:      /* Backspace */
    case 0x0f:      /* Tab */
    case 0x1c:      /* Return */
    case 0x53:      /* Delete */
    case 0x72:      /* Enter, on the keypad */
        return 1;
    }
    return 0;
}

/*
 * An AES key code and its shift state in, one normalised code out.
 *
 * evnt_multi() hands back the scancode in the high byte and the ASCII
 * character in the low one.  A key that produced a character is that
 * character, unless it is one of the six above; a key that produced none
 * -- the arrows, the function keys -- is its scancode with NKF_FUNC set,
 * which is what keeps NK_RIGHT apart from 'M' in the same switch (see
 * nkcc.h).
 */
short gem_to_norm(short kstate, short key)
{
    short scan  = (short)((key >> 8) & 0xff);
    short ascii = (short)(key & 0xff);
    short out   = (short)((ascii && !is_editing_key(scan))
                          ? ascii : (NKF_FUNC | NKF_KEY | scan));

    if (kstate & 0x0003) out |= NKF_SHIFT;   /* either shift key */
    if (kstate & 0x0004) out |= NKF_CTRL;
    if (kstate & 0x0008) out |= NKF_ALT;
    if (kstate & 0x0010) out |= NKF_CAPS;

    return out;
}

/*
 * The AES version, which gemlib keeps in a global of this name.
 *
 * DELIBERATELY LOW
 *
 * A program reads this to decide whether it may use something newer than
 * the AES of 1985.  Reporting a low version makes HighWire take the
 * conservative road everywhere, which is the right answer while the
 * board package is still filling in.  When appl_init() starts handing
 * back the AES global block, this should come from there instead.
 */
short gl_ap_version = 0x0140;

/* ==== stubs: the font GDOS that is not there ========================== */

/*
 * Each of these is reached only behind `if (has_fsm_gdos)`, and on this
 * machine that is false.  They answer the way a VDI without a font GDOS
 * answers -- nothing done, nothing available -- so that a caller who
 * gets here despite everything is refused rather than dropped.
 */

void v_ftext(short handle, short x, short y, const char *str)
{
    (void)handle; (void)x; (void)y; (void)str;
}

void v_ftext16(short handle, short x, short y, const short *str)
{
    (void)handle; (void)x; (void)y; (void)str;
}

void v_ftext16n(short handle, PXY pos, const short *str, short n)
{
    (void)handle; (void)pos; (void)str; (void)n;
}

short vqt_advance(short handle, short ch, short *advx, short *advy,
                  short *xrem, short *yrem)
{
    (void)handle; (void)ch;
    if (advx) *advx = 0;
    if (advy) *advy = 0;
    if (xrem) *xrem = 0;
    if (yrem) *yrem = 0;
    return 0;
}

short vqt_f_extent16n(short handle, const short *str, short n, short *extent)
{
    short i;

    (void)handle; (void)str; (void)n;
    if (extent)
        for (i = 0; i < 8; i++)
            extent[i] = 0;
    return 0;
}

short vqt_width(short handle, short ch, short *cw, short *ldelta,
                short *rdelta)
{
    (void)handle;
    if (cw) *cw = 0;
    if (ldelta) *ldelta = 0;
    if (rdelta) *rdelta = 0;
    return (short)-1;           /* the character is not in the font */
    (void)ch;
}

/*
 * How tall the current font is.  A real call, not a stub: this is base
 * VDI, opcode 131, and pTOS has it (vdi/vdi_text.c's vdi_vqt_fontinfo).
 *
 * It was a stub returning zeros, and that one mistake was enough to make
 * the browser unreadable.  fontbase.c takes the ascent and descent from
 * distances[4] and distances[0], and with both zero its line spacing
 * came out as (0 + 0 + 4) / 8 + 1 -- one pixel.  Every line of every
 * page was then drawn one pixel below the last, so the whole document
 * piled up into a single black band.  Tables still looked right, because
 * their cells are placed by the table code and not by font metrics,
 * which is what made it obvious where to look.
 *
 * The VDI hands the five distances back in the y halves of ptsout and
 * the width and the three style effects in the x halves:
 *
 *      ptsout[0] max cell width      ptsout[1] bottom line
 *      ptsout[2] thickening          ptsout[3] descent
 *      ptsout[4] left offset         ptsout[5] half line
 *      ptsout[6] right offset        ptsout[7] ascent
 *      ptsout[8] (unused)            ptsout[9] top line
 */
short vqt_fontinfo(short handle, short *minade, short *maxade,
                   short *distances, short *maxwidth, short *effects)
{
    vdi(handle, 131, 0, 0);

    if (minade) *minade = intout[0];
    if (maxade) *maxade = intout[1];
    if (maxwidth) *maxwidth = ptsout[0];

    if (distances)
    {
        distances[0] = ptsout[1];       /* bottom  */
        distances[1] = ptsout[3];       /* descent */
        distances[2] = ptsout[5];       /* half    */
        distances[3] = ptsout[7];       /* ascent  */
        distances[4] = ptsout[9];       /* top     */
    }

    if (effects)
    {
        effects[0] = ptsout[2];         /* thickening */
        effects[1] = ptsout[4];         /* left  skew */
        effects[2] = ptsout[6];         /* right skew */
    }

    return 0;
}

short vqt_xfntinfo(short handle, short flags, short id, short index,
                   XFNT_INFO *info)
{
    (void)handle; (void)flags; (void)id; (void)index;
    if (info)
        memset(info, 0, sizeof(*info));
    return 0;
}

void vq_scrninfo(short handle, short *work_out)
{
    short i;

    (void)handle;
    if (work_out)
        for (i = 0; i < 16; i++)
            work_out[i] = 0;
}

short vst_arbpt(short handle, short point, short *wchar, short *hchar,
                short *wcell, short *hcell)
{
    (void)handle; (void)point;
    if (wchar) *wchar = 0;
    if (hchar) *hchar = 0;
    if (wcell) *wcell = 0;
    if (hcell) *hcell = 0;
    return 0;
}

short vst_setsize32(short handle, long point, short *wchar, short *hchar,
                    short *wcell, short *hcell)
{
    (void)point;
    return vst_arbpt(handle, 0, wchar, hchar, wcell, hcell);
}

short vst_kern(short handle, short tmode, short pmode, short *tracks,
               short *pairs)
{
    (void)handle; (void)tmode; (void)pmode;
    if (tracks) *tracks = 0;
    if (pairs) *pairs = 0;
    return 0;                   /* no kerning of either kind */
}

short vst_map_mode(short handle, short mode)
{
    (void)handle; (void)mode;
    return 0;
}

void vst_scratch(short handle, short mode)
{
    (void)handle; (void)mode;
}

short vst_load_fonts(short handle, short select)
{
    (void)handle; (void)select;
    return 0;                   /* no extra fonts were loaded */
}

void vst_unload_fonts(short handle, short select)
{
    (void)handle; (void)select;
}

/* ==== one thing the C library is missing ============================== */

/*
 * libcmini has no wide-character functions.  HighWire uses this one to
 * find word breaks while laying text out, and for that the ASCII answer
 * is the right answer: a code point above 0xff is never a space in any
 * text this renders.
 */
int iswspace(unsigned int wc)
{
    return (wc == ' ' || wc == '\t' || wc == '\n' || wc == '\v'
            || wc == '\f' || wc == '\r');
}
