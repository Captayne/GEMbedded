/*
 * gem.c - minimal AES and VDI bindings for pTOS programs (ARM)
 *
 * Copyright (C) 2026 Andreas Keibel
 */

#include "gem.h"

/* ---- AES ---- */

static short control[4];
static short global[15];
static short int_in[16];
static short int_out[7];
static long addr_in[3];
static long addr_out[1];

static void *aespb[6] = { control, global, int_in, int_out, addr_in, addr_out };

short gl_apid;

static short aes(short op, short nintin, short nintout, short naddrin)
{
    register long r0 __asm__("r0") = 200;
    register void *r1 __asm__("r1") = aespb;

    control[0] = op;
    control[1] = nintin;
    control[2] = nintout;
    control[3] = naddrin;
    __asm__ volatile ("svc 2"
                      : "+r"(r0), "+r"(r1)
                      :
                      : "r2", "r3", "r12", "lr", "memory", "cc");
    return int_out[0];
}

short appl_init(void)
{
    gl_apid = aes(10, 0, 1, 0);
    return gl_apid;
}

short appl_exit(void)
{
    return aes(19, 0, 1, 0);
}

short evnt_mesag(short *msg)
{
    addr_in[0] = (long)msg;
    return aes(23, 0, 1, 1);
}

short evnt_timer(unsigned long ms)
{
    int_in[0] = (short)(ms & 0xffff);
    int_in[1] = (short)(ms >> 16);
    return aes(24, 2, 1, 0);
}

/* Give way to whoever is furthest behind, and come back at once. */
short appl_yield(void)
{
    return aes(17, 0, 1, 0);
}

short appl_write(short id, short length, const void *msg)
{
    int_in[0] = id;
    int_in[1] = length;
    addr_in[0] = (long)msg;
    return aes(12, 2, 1, 1);
}

/*
 * Ask the shell to run something.  It takes effect when whatever is
 * running now ends -- an accessory that calls this and wants the desktop
 * to make way sends it a message afterwards, so that it looks and finds
 * out (the desktop of pTOS does; TOS's does not).
 *
 * The command tail is the usual GEM one: its first byte is the length.
 */
short shel_write(short doex, short isgraf, short isover,
                 const char *cmd, const char *tail)
{
    int_in[0] = doex;
    int_in[1] = isgraf;
    int_in[2] = isover;
    addr_in[0] = (long)cmd;
    addr_in[1] = (long)tail;
    return aes(121, 3, 1, 2);
}

short menu_register(short apid, const char *name)
{
    int_in[0] = apid;
    addr_in[0] = (long)name;
    return aes(35, 1, 1, 1);
}

short form_alert(short defbutton, const char *text)
{
    int_in[0] = defbutton;
    addr_in[0] = (long)text;
    return aes(52, 1, 1, 1);
}

short form_dial(short flag, short lx, short ly, short lw, short lh,
                short bx, short by, short bw, short bh)
{
    int_in[0] = flag;
    int_in[1] = lx;
    int_in[2] = ly;
    int_in[3] = lw;
    int_in[4] = lh;
    int_in[5] = bx;
    int_in[6] = by;
    int_in[7] = bw;
    int_in[8] = bh;
    return aes(51, 9, 1, 0);
}

short graf_handle(short *wchar, short *hchar, short *wbox, short *hbox)
{
    short h = aes(77, 0, 5, 0);

    *wchar = int_out[1];
    *hchar = int_out[2];
    *wbox = int_out[3];
    *hbox = int_out[4];
    return h;
}

/*
 * Where the pointer is and what is pressed.  On this machine that is how
 * a program reads the touch screen: a click outside a window belongs to
 * the screen manager, and taking it over with wind_update(BEG_MCTRL)
 * hangs here.  Asking works for anyone.
 */
short graf_mkstate(short *mx, short *my, short *mstate, short *kstate)
{
    short r = aes(79, 0, 5, 0);

    *mx = int_out[1];
    *my = int_out[2];
    *mstate = int_out[3];
    *kstate = int_out[4];
    return r;
}

short graf_mouse(short form, const void *mform)
{
    int_in[0] = form;
    addr_in[0] = (long)mform;
    return aes(78, 1, 1, 1);
}

short objc_draw(OBJECT *tree, short start, short depth,
                short x, short y, short w, short h)
{
    int_in[0] = start;
    int_in[1] = depth;
    int_in[2] = x;
    int_in[3] = y;
    int_in[4] = w;
    int_in[5] = h;
    addr_in[0] = (long)tree;
    return aes(42, 6, 1, 1);
}

short objc_find(OBJECT *tree, short start, short depth, short mx, short my)
{
    int_in[0] = start;
    int_in[1] = depth;
    int_in[2] = mx;
    int_in[3] = my;
    addr_in[0] = (long)tree;
    return aes(43, 4, 1, 1);
}

short evnt_multi_button_timer(unsigned long ms, short *mx, short *my,
                              short *button, short *kstate, short *key,
                              short *clicks, short *msg)
{
    int i;

    for (i = 0; i < 16; i++)
        int_in[i] = 0;
    /*
     * MU_KEYBD belongs here. This takes a 'key' and a 'kstate' to write
     * into, which are of no use to anybody unless keys are among the
     * events asked for -- and they were not, so a sketch waiting on this
     * never saw one. The signature promised something the mask did not
     * request.
     */
    int_in[0] = MU_KEYBD | MU_BUTTON | MU_TIMER | MU_MESAG;
    int_in[1] = 1;                      /* clicks */
    int_in[2] = 1;                      /* button mask: left */
    int_in[3] = 1;                      /* wanted state: pressed */
    int_in[14] = (short)(ms & 0xffff);
    int_in[15] = (short)(ms >> 16);
    addr_in[0] = (long)msg;
    aes(25, 16, 7, 1);
    *mx = int_out[1];
    *my = int_out[2];
    *button = int_out[3];
    *kstate = int_out[4];
    *key = int_out[5];
    *clicks = int_out[6];
    return int_out[0];
}

short evnt_multi_mesag_timer(unsigned long ms, short *msg)
{
    int i;

    for (i = 0; i < 16; i++)
        int_in[i] = 0;
    int_in[0] = MU_MESAG | MU_TIMER;
    int_in[14] = (short)(ms & 0xffff);
    int_in[15] = (short)(ms >> 16);
    addr_in[0] = (long)msg;
    aes(25, 16, 7, 1);
    return int_out[0];
}

short wind_update(short mode)
{
    int_in[0] = mode;
    return aes(107, 1, 1, 0);
}

/* ---- windows ---- */

static void xywh(short a, short b, short c, short d)
{
    int_in[1] = a;
    int_in[2] = b;
    int_in[3] = c;
    int_in[4] = d;
}

short wind_create(short kind, short x, short y, short w, short h)
{
    int_in[0] = kind;
    xywh(x, y, w, h);
    return aes(100, 5, 1, 0);
}

short wind_open(short handle, short x, short y, short w, short h)
{
    int_in[0] = handle;
    xywh(x, y, w, h);
    return aes(101, 5, 1, 0);
}

short wind_close(short handle)
{
    int_in[0] = handle;
    return aes(102, 1, 1, 0);
}

short wind_delete(short handle)
{
    int_in[0] = handle;
    return aes(103, 1, 1, 0);
}

short wind_get(short handle, short field,
               short *a, short *b, short *c, short *d)
{
    short r;

    int_in[0] = handle;
    int_in[1] = field;
    r = aes(104, 2, 5, 0);
    *a = int_out[1];
    *b = int_out[2];
    *c = int_out[3];
    *d = int_out[4];
    return r;
}

short wind_set(short handle, short field, short a, short b, short c, short d)
{
    int_in[0] = handle;
    int_in[1] = field;
    int_in[2] = a;
    int_in[3] = b;
    int_in[4] = c;
    int_in[5] = d;
    return aes(105, 6, 1, 0);
}

/*
 * The name is kept by address, so it has to stay where it is.  The AES
 * reads the address straight out of int_in[2..3] as a pointer, in memory
 * order -- on this little-endian machine the low half comes first, the
 * other way round from the Atari.
 */
short wind_set_name(short handle, const char *name)
{
    unsigned long p = (unsigned long)name;

    return wind_set(handle, WF_NAME, (short)(p & 0xffff), (short)(p >> 16),
                    0, 0);
}

short wind_calc(short type, short kind, short x, short y, short w, short h,
                short *ox, short *oy, short *ow, short *oh)
{
    int_in[0] = type;
    int_in[1] = kind;
    int_in[2] = x;
    int_in[3] = y;
    int_in[4] = w;
    int_in[5] = h;
    aes(108, 6, 5, 0);
    *ox = int_out[1];
    *oy = int_out[2];
    *ow = int_out[3];
    *oh = int_out[4];
    return int_out[0];
}

short wind_find(short x, short y)
{
    int_in[0] = x;
    int_in[1] = y;
    return aes(106, 2, 1, 0);
}

short wind_new(void)
{
    return aes(109, 0, 1, 0);
}

/* ---- events ---- */

/*
 * The general evnt_multi().  Sixteen words go in and seven come back,
 * which is why every other event call is a special case of this one; the
 * two ready-made ones above stay because they are what most programs
 * actually wait for.  Every output is optional -- pass 0 for what you do
 * not want back.
 */
short evnt_multi(short flags, short clicks, short bmask, short bstate,
                 short m1flags, short m1x, short m1y, short m1w, short m1h,
                 short m2flags, short m2x, short m2y, short m2w, short m2h,
                 short *msg, unsigned long ms,
                 short *mx, short *my, short *button, short *kstate,
                 short *key, short *clicks_out)
{
    int_in[0] = flags;
    int_in[1] = clicks;
    int_in[2] = bmask;
    int_in[3] = bstate;
    int_in[4] = m1flags;
    int_in[5] = m1x;
    int_in[6] = m1y;
    int_in[7] = m1w;
    int_in[8] = m1h;
    int_in[9] = m2flags;
    int_in[10] = m2x;
    int_in[11] = m2y;
    int_in[12] = m2w;
    int_in[13] = m2h;
    int_in[14] = (short)(ms & 0xffff);
    int_in[15] = (short)(ms >> 16);
    addr_in[0] = (long)msg;
    aes(25, 16, 7, 1);
    if (mx) *mx = int_out[1];
    if (my) *my = int_out[2];
    if (button) *button = int_out[3];
    if (kstate) *kstate = int_out[4];
    if (key) *key = int_out[5];
    if (clicks_out) *clicks_out = int_out[6];
    return int_out[0];
}

short evnt_keybd(void)
{
    return aes(20, 0, 1, 0);
}

short evnt_button(short clicks, short mask, short state,
                  short *mx, short *my, short *button, short *kstate)
{
    int_in[0] = clicks;
    int_in[1] = mask;
    int_in[2] = state;
    aes(21, 3, 5, 0);
    if (mx) *mx = int_out[1];
    if (my) *my = int_out[2];
    if (button) *button = int_out[3];
    if (kstate) *kstate = int_out[4];
    return int_out[0];
}

short evnt_mouse(short flags, short x, short y, short w, short h,
                 short *mx, short *my, short *button, short *kstate)
{
    int_in[0] = flags;
    int_in[1] = x;
    int_in[2] = y;
    int_in[3] = w;
    int_in[4] = h;
    aes(22, 5, 5, 0);
    if (mx) *mx = int_out[1];
    if (my) *my = int_out[2];
    if (button) *button = int_out[3];
    if (kstate) *kstate = int_out[4];
    return int_out[0];
}

/* ---- menus ---- */

short menu_bar(OBJECT *tree, short show)
{
    int_in[0] = show;
    addr_in[0] = (long)tree;
    return aes(30, 1, 1, 1);
}

/* icheck, ienable and tnormal differ only in the opcode */
static short menu_item(short op, OBJECT *tree, short item, short how)
{
    int_in[0] = item;
    int_in[1] = how;
    addr_in[0] = (long)tree;
    return aes(op, 2, 1, 1);
}

short menu_icheck(OBJECT *tree, short item, short check)
{
    return menu_item(31, tree, item, check);
}

short menu_ienable(OBJECT *tree, short item, short enable)
{
    return menu_item(32, tree, item, enable);
}

short menu_tnormal(OBJECT *tree, short title, short normal)
{
    return menu_item(33, tree, title, normal);
}

short menu_text(OBJECT *tree, short item, const char *text)
{
    int_in[0] = item;
    addr_in[0] = (long)tree;
    addr_in[1] = (long)text;
    return aes(34, 1, 1, 2);
}

/* ---- object trees ---- */

short objc_add(OBJECT *tree, short parent, short child)
{
    int_in[0] = parent;
    int_in[1] = child;
    addr_in[0] = (long)tree;
    return aes(40, 2, 1, 1);
}

short objc_delete(OBJECT *tree, short obj)
{
    int_in[0] = obj;
    addr_in[0] = (long)tree;
    return aes(41, 1, 1, 1);
}

short objc_order(OBJECT *tree, short obj, short newpos)
{
    int_in[0] = obj;
    int_in[1] = newpos;
    addr_in[0] = (long)tree;
    return aes(45, 2, 1, 1);
}

/* where an object sits on the screen, its parents' positions included */
short objc_offset(OBJECT *tree, short obj, short *x, short *y)
{
    short r;

    int_in[0] = obj;
    addr_in[0] = (long)tree;
    r = aes(44, 1, 3, 1);
    if (x) *x = int_out[1];
    if (y) *y = int_out[2];
    return r;
}

short objc_change(OBJECT *tree, short obj, short x, short y, short w, short h,
                  short newstate, short redraw)
{
    int_in[0] = obj;
    int_in[1] = 0;                      /* reserved */
    int_in[2] = x;
    int_in[3] = y;
    int_in[4] = w;
    int_in[5] = h;
    int_in[6] = newstate;
    int_in[7] = redraw;
    addr_in[0] = (long)tree;
    return aes(47, 8, 1, 1);
}

short objc_edit(OBJECT *tree, short obj, short ch, short *idx, short kind)
{
    short r;

    int_in[0] = obj;
    int_in[1] = ch;
    int_in[2] = idx ? *idx : 0;
    int_in[3] = kind;
    addr_in[0] = (long)tree;
    r = aes(46, 4, 2, 1);
    if (idx) *idx = int_out[1];
    return r;
}

/* ---- dialogues ---- */

short form_do(OBJECT *tree, short start)
{
    int_in[0] = start;
    addr_in[0] = (long)tree;
    return aes(50, 1, 1, 1);
}

short form_center(OBJECT *tree, short *x, short *y, short *w, short *h)
{
    short r;

    addr_in[0] = (long)tree;
    r = aes(54, 0, 5, 1);
    if (x) *x = int_out[1];
    if (y) *y = int_out[2];
    if (w) *w = int_out[3];
    if (h) *h = int_out[4];
    return r;
}

short form_error(short err)
{
    int_in[0] = err;
    return aes(53, 1, 1, 0);
}

short form_button(OBJECT *tree, short obj, short clicks, short *nxtobj)
{
    short r;

    int_in[0] = obj;
    int_in[1] = clicks;
    addr_in[0] = (long)tree;
    r = aes(56, 2, 2, 1);
    if (nxtobj) *nxtobj = int_out[1];
    return r;
}

short form_keybd(OBJECT *tree, short obj, short nxtobj, short thechar,
                 short *nxtout, short *charout)
{
    short r;

    int_in[0] = obj;
    int_in[1] = thechar;
    int_in[2] = nxtobj;
    addr_in[0] = (long)tree;
    r = aes(55, 3, 3, 1);
    if (nxtout) *nxtout = int_out[1];
    if (charout) *charout = int_out[2];
    return r;
}

/* ---- rubber bands and boxes ---- */

short graf_rubberbox(short x, short y, short minw, short minh,
                     short *endw, short *endh)
{
    short r;

    int_in[0] = x;
    int_in[1] = y;
    int_in[2] = minw;
    int_in[3] = minh;
    r = aes(70, 4, 3, 0);
    if (endw) *endw = int_out[1];
    if (endh) *endh = int_out[2];
    return r;
}

short graf_dragbox(short w, short h, short sx, short sy,
                   short bx, short by, short bw, short bh,
                   short *endx, short *endy)
{
    short r;

    int_in[0] = w;
    int_in[1] = h;
    int_in[2] = sx;
    int_in[3] = sy;
    int_in[4] = bx;
    int_in[5] = by;
    int_in[6] = bw;
    int_in[7] = bh;
    r = aes(71, 8, 3, 0);
    if (endx) *endx = int_out[1];
    if (endy) *endy = int_out[2];
    return r;
}

short graf_movebox(short w, short h, short sx, short sy, short dx, short dy)
{
    int_in[0] = w;
    int_in[1] = h;
    int_in[2] = sx;
    int_in[3] = sy;
    int_in[4] = dx;
    int_in[5] = dy;
    return aes(72, 6, 1, 0);
}

/* growbox and shrinkbox are the same eight words the other way round */
static short graf_box(short op, short ax, short ay, short aw, short ah,
                      short bx, short by, short bw, short bh)
{
    int_in[0] = ax;
    int_in[1] = ay;
    int_in[2] = aw;
    int_in[3] = ah;
    int_in[4] = bx;
    int_in[5] = by;
    int_in[6] = bw;
    int_in[7] = bh;
    return aes(op, 8, 1, 0);
}

short graf_growbox(short sx, short sy, short sw, short sh,
                   short dx, short dy, short dw, short dh)
{
    return graf_box(73, sx, sy, sw, sh, dx, dy, dw, dh);
}

short graf_shrinkbox(short dx, short dy, short dw, short dh,
                     short sx, short sy, short sw, short sh)
{
    return graf_box(74, dx, dy, dw, dh, sx, sy, sw, sh);
}

short graf_watchbox(OBJECT *tree, short obj, short instate, short outstate)
{
    int_in[0] = 0;                      /* reserved */
    int_in[1] = obj;
    int_in[2] = instate;
    int_in[3] = outstate;
    addr_in[0] = (long)tree;
    return aes(75, 4, 1, 1);
}

short graf_slidebox(OBJECT *tree, short parent, short obj, short isvert)
{
    int_in[0] = parent;
    int_in[1] = obj;
    int_in[2] = isvert;
    addr_in[0] = (long)tree;
    return aes(76, 3, 1, 1);
}

/* ---- the file selector, the scrap directory, other applications ---- */

short fsel_input(char *path, char *sel, short *button)
{
    short r;

    addr_in[0] = (long)path;
    addr_in[1] = (long)sel;
    r = aes(90, 0, 2, 2);
    if (button) *button = int_out[1];
    return r;
}

short fsel_exinput(char *path, char *sel, short *button, const char *label)
{
    short r;

    addr_in[0] = (long)path;
    addr_in[1] = (long)sel;
    addr_in[2] = (long)label;
    r = aes(91, 0, 2, 3);
    if (button) *button = int_out[1];
    return r;
}

short scrp_read(char *path)
{
    addr_in[0] = (long)path;
    return aes(80, 0, 1, 1);
}

short scrp_write(const char *path)
{
    addr_in[0] = (long)path;
    return aes(81, 0, 1, 1);
}

short appl_find(const char *name)
{
    addr_in[0] = (long)name;
    return aes(13, 0, 1, 1);
}

short appl_read(short id, short length, void *buf)
{
    int_in[0] = id;
    int_in[1] = length;
    addr_in[0] = (long)buf;
    return aes(11, 2, 1, 1);
}

short shel_read(char *cmd, char *tail)
{
    addr_in[0] = (long)cmd;
    addr_in[1] = (long)tail;
    return aes(120, 0, 1, 2);
}

/* ---- resources ---- */

short rsrc_load(const char *name)
{
    addr_in[0] = (long)name;
    return aes(110, 0, 1, 1);
}

short rsrc_free(void)
{
    return aes(111, 0, 1, 0);
}

/*
 * The address comes back in addr_out[0], so what the caller hands over is
 * where to put it: a pointer to their own pointer.
 */
short rsrc_gaddr(short type, short index, void *addr)
{
    short r;

    int_in[0] = type;
    int_in[1] = index;
    r = aes(112, 2, 1, 0);
    if (addr)
        *(long *)addr = addr_out[0];
    return r;
}

/* ---- VDI ---- */

/*
 * Four-byte aligned because two of its words are not words: on this port
 * contrl[] is the VDICONTROL structure of pTOS's include/vdipb.h -- seven
 * words, a pad, and then two native pointers, at words 8 and 10.  That is
 * where the MFDBs of a raster copy go, whole, not split into halves as on
 * the Atari.
 */
static short contrl[12] __attribute__((aligned(4)));
static short intin[128];
static short ptsin[128];
static short intout[128];
static short ptsout[128];

static void *vdipb[5] = { contrl, intin, ptsin, intout, ptsout };

static void vdi_sub(short handle, short op, short sub,
                    short nptsin, short nintin)
{
    register long r0 __asm__("r0") = 0x73;
    register void *r1 __asm__("r1") = vdipb;

    contrl[0] = op;
    contrl[1] = nptsin;
    contrl[3] = nintin;
    contrl[5] = sub;
    contrl[6] = handle;
    __asm__ volatile ("svc 2"
                      : "+r"(r0), "+r"(r1)
                      :
                      : "r2", "r3", "r12", "lr", "memory", "cc");
}

static void vdi(short handle, short op, short nptsin, short nintin)
{
    vdi_sub(handle, op, 0, nptsin, nintin);
}

/*
 * The graphics primitives -- bars, circles, arcs, rounded boxes -- are
 * all one opcode with a subfunction in contrl[5], which is the only
 * place that field is used.
 */
static void vdi_gdp(short handle, short sub, short nptsin, short nintin)
{
    vdi_sub(handle, 11, sub, nptsin, nintin);
}

short v_opnvwk(short phys_handle)
{
    int i;

    for (i = 0; i < 10; i++)
        intin[i] = 1;
    intin[10] = 2;                      /* raster coordinates */
    vdi(phys_handle, 100, 0, 11);
    return contrl[6];
}

void v_clsvwk(short handle)
{
    vdi(handle, 101, 0, 0);
}

void vs_clip(short handle, short on, const short *xyxy)
{
    int i;

    intin[0] = on;
    for (i = 0; i < 4; i++)
        ptsin[i] = xyxy[i];
    vdi(handle, 129, 2, 1);
}

static void vdi1(short handle, short op, short value)
{
    intin[0] = value;
    vdi(handle, op, 0, 1);
}

void vswr_mode(short handle, short mode)    { vdi1(handle, 32, mode); }
void vsl_color(short handle, short color)   { vdi1(handle, 17, color); }
void vsf_color(short handle, short color)   { vdi1(handle, 25, color); }
void vsf_interior(short handle, short style){ vdi1(handle, 23, style); }
void vst_color(short handle, short color)   { vdi1(handle, 22, color); }

void v_pline(short handle, short count, const short *xy)
{
    int i;

    for (i = 0; i < 2 * count; i++)
        ptsin[i] = xy[i];
    vdi(handle, 6, count, 0);
}

void vr_recfl(short handle, const short *xyxy)
{
    int i;

    for (i = 0; i < 4; i++)
        ptsin[i] = xyxy[i];
    vdi(handle, 114, 2, 0);
}

void v_gtext(short handle, short x, short y, const char *s)
{
    int n;

    for (n = 0; s[n] && n < 128; n++)
        intin[n] = (unsigned char)s[n];
    ptsin[0] = x;
    ptsin[1] = y;
    vdi(handle, 8, 1, n);
}

void vro_cpyfm(short handle, short mode, const short *xyxy8,
               const MFDB *src, const MFDB *dst)
{
    int i;

    for (i = 0; i < 8; i++)
        ptsin[i] = xyxy8[i];
    intin[0] = mode;
    /* copied in rather than assigned through a cast: a pointer written
       through a short * is exactly what strict aliasing forbids, and the
       builtin becomes the single store it looks like */
    __builtin_memcpy(&contrl[8], &src, sizeof src);
    __builtin_memcpy(&contrl[10], &dst, sizeof dst);
    vdi(handle, 109, 4, 1);
}

void vrt_cpyfm(short handle, short mode, const short *xyxy8,
               const MFDB *src, const MFDB *dst, short fg, short bg)
{
    int i;

    for (i = 0; i < 8; i++)
        ptsin[i] = xyxy8[i];
    intin[0] = mode;
    intin[1] = fg;
    intin[2] = bg;
    __builtin_memcpy(&contrl[8], &src, sizeof src);
    __builtin_memcpy(&contrl[10], &dst, sizeof dst);
    vdi(handle, 121, 4, 3);
}

/* ---- colour ---- */

void vs_color(short handle, short index, const short *rgb)
{
    intin[0] = index;
    intin[1] = rgb[0];
    intin[2] = rgb[1];
    intin[3] = rgb[2];
    vdi(handle, 14, 0, 4);
}

void vq_color(short handle, short index, short flag, short *rgb)
{
    intin[0] = index;
    intin[1] = flag;
    vdi(handle, 26, 0, 2);
    rgb[0] = intout[1];
    rgb[1] = intout[2];
    rgb[2] = intout[3];
}

/* ---- lines, fills, the whole surface ---- */

void vsl_type(short handle, short style)     { vdi1(handle, 15, style); }
void vsf_style(short handle, short style)    { vdi1(handle, 24, style); }
void vsf_perimeter(short handle, short on)   { vdi1(handle, 104, on); }

void vsl_width(short handle, short width)
{
    ptsin[0] = width;
    ptsin[1] = 0;
    vdi(handle, 16, 1, 0);
}

void vsl_ends(short handle, short beg, short end)
{
    intin[0] = beg;
    intin[1] = end;
    vdi(handle, 108, 0, 2);
}

void v_fillarea(short handle, short count, const short *xy)
{
    int i;

    for (i = 0; i < 2 * count; i++)
        ptsin[i] = xy[i];
    vdi(handle, 9, count, 0);
}

void v_contourfill(short handle, short x, short y, short index)
{
    ptsin[0] = x;
    ptsin[1] = y;
    intin[0] = index;
    vdi(handle, 103, 1, 1);
}

void v_clrwk(short handle)
{
    vdi(handle, 3, 0, 0);
}

/* ---- the graphics primitives ---- */

/*
 * All of these are opcode 11 with a subfunction, and each reads its
 * arguments from its own corner of ptsin -- the radius of a circle at
 * ptsin[4], of an arc at ptsin[6], the two radii of an ellipse at
 * ptsin[2..3].  That is not a pattern, it is history, so each one is
 * written out rather than folded together.
 */

void v_bar(short handle, const short *xyxy)
{
    int i;

    for (i = 0; i < 4; i++)
        ptsin[i] = xyxy[i];
    vdi_gdp(handle, 1, 2, 0);
}

void v_circle(short handle, short x, short y, short radius)
{
    ptsin[0] = x;
    ptsin[1] = y;
    ptsin[2] = ptsin[3] = 0;
    ptsin[4] = radius;
    ptsin[5] = 0;
    vdi_gdp(handle, 4, 3, 0);
}

void v_ellipse(short handle, short x, short y, short xradius, short yradius)
{
    ptsin[0] = x;
    ptsin[1] = y;
    ptsin[2] = xradius;
    ptsin[3] = yradius;
    vdi_gdp(handle, 5, 2, 0);
}

static void v_curve(short handle, short sub, short x, short y, short radius,
                    short begang, short endang)
{
    ptsin[0] = x;
    ptsin[1] = y;
    ptsin[2] = ptsin[3] = ptsin[4] = ptsin[5] = 0;
    ptsin[6] = radius;
    ptsin[7] = 0;
    intin[0] = begang;
    intin[1] = endang;
    vdi_gdp(handle, sub, 4, 2);
}

void v_arc(short handle, short x, short y, short radius,
           short begang, short endang)
{
    v_curve(handle, 2, x, y, radius, begang, endang);
}

void v_pieslice(short handle, short x, short y, short radius,
                short begang, short endang)
{
    v_curve(handle, 3, x, y, radius, begang, endang);
}

void v_rbox(short handle, const short *xyxy)
{
    int i;

    for (i = 0; i < 4; i++)
        ptsin[i] = xyxy[i];
    vdi_gdp(handle, 8, 2, 0);
}

void v_rfbox(short handle, const short *xyxy)
{
    int i;

    for (i = 0; i < 4; i++)
        ptsin[i] = xyxy[i];
    vdi_gdp(handle, 9, 2, 0);
}

/* ---- text ---- */

/* vst_height() and vst_point() answer with the same four numbers */
static void text_sizes(short *cw, short *ch, short *bw, short *bh)
{
    if (cw) *cw = ptsout[0];
    if (ch) *ch = ptsout[1];
    if (bw) *bw = ptsout[2];
    if (bh) *bh = ptsout[3];
}

void vst_height(short handle, short height,
                short *cw, short *ch, short *bw, short *bh)
{
    ptsin[0] = 0;
    ptsin[1] = height;
    vdi(handle, 12, 1, 0);
    text_sizes(cw, ch, bw, bh);
}

void vst_point(short handle, short point,
               short *cw, short *ch, short *bw, short *bh)
{
    intin[0] = point;
    vdi(handle, 107, 0, 1);
    text_sizes(cw, ch, bw, bh);
}

void vst_font(short handle, short font)       { vdi1(handle, 21, font); }
void vst_rotation(short handle, short angle)  { vdi1(handle, 13, angle); }
void vst_effects(short handle, short effects) { vdi1(handle, 106, effects); }

void vst_alignment(short handle, short hin, short vin,
                   short *hout, short *vout)
{
    intin[0] = hin;
    intin[1] = vin;
    vdi(handle, 39, 0, 2);
    if (hout) *hout = intout[0];
    if (vout) *vout = intout[1];
}

/*
 * The four corners of the rectangle the string would occupy, anti-
 * clockwise from the bottom left.  Ask before laying anything out: the
 * characters of a font are not all the same width, and code that assumes
 * they are is right only by accident.
 */
void vqt_extent(short handle, const char *s, short *extent)
{
    int n, i;

    for (n = 0; s[n] && n < 128; n++)
        intin[n] = (unsigned char)s[n];
    vdi(handle, 116, 0, n);
    for (i = 0; i < 8; i++)
        extent[i] = ptsout[i];
}

/* ---- asking the screen ---- */

void v_get_pixel(short handle, short x, short y, short *pel, short *index)
{
    ptsin[0] = x;
    ptsin[1] = y;
    vdi(handle, 105, 1, 0);
    if (pel) *pel = intout[0];
    if (index) *index = intout[1];
}

void v_show_c(short handle, short reset)
{
    intin[0] = reset;
    vdi(handle, 122, 0, 1);
}

void v_hide_c(short handle)
{
    vdi(handle, 123, 0, 0);
}
