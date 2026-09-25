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
    int_in[0] = MU_BUTTON | MU_TIMER | MU_MESAG;
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

/* ---- VDI ---- */

static short contrl[12];
static short intin[128];
static short ptsin[128];
static short intout[128];
static short ptsout[128];

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
