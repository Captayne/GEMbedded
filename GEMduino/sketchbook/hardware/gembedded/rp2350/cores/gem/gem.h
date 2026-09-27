/*
 * gem.h - minimal AES and VDI bindings for pTOS programs (ARM)
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * Just what the GEMbedded tools need; the call interface is the one of
 * Atari GEM (trap #2 = svc 2, r0 = 200 for the AES, 0x73 for the VDI,
 * r1 = parameter block).
 */

#ifndef GEM_H
#define GEM_H

/* A sketch's .ino is compiled as C++, and C++ decorates the names of
   functions: without this it would look for them in vain. */
#ifdef __cplusplus
extern "C" {
#endif

/* ---- objects ---- */

typedef struct
{
    short ob_next, ob_head, ob_tail;
    unsigned short ob_type, ob_flags, ob_state;
    long ob_spec;
    short ob_x, ob_y, ob_width, ob_height;
} OBJECT;

#define G_BOX           20
#define G_TEXT          21
#define G_BOXTEXT       22
#define G_IBOX          25
#define G_BUTTON        26
#define G_STRING        28

#define NONE            0x0000
#define SELECTABLE      0x0001
#define DEFAULT         0x0002
#define EXIT            0x0004
#define EDITABLE        0x0008
#define RBUTTON         0x0010
#define LASTOB          0x0020
#define TOUCHEXIT       0x0040
#define HIDETREE        0x0080

#define NORMAL          0x0000
#define SELECTED        0x0001
#define CROSSED         0x0002
#define CHECKED         0x0004
#define DISABLED        0x0008
#define OUTLINED        0x0010
#define SHADOWED        0x0020

#define MAX_DEPTH       8

/* ---- AES ---- */

#define MU_KEYBD        0x0001
#define MU_BUTTON       0x0002
#define MU_M1           0x0004
#define MU_M2           0x0008
#define MU_MESAG        0x0010
#define MU_TIMER        0x0020

#define AP_TERM         50
#define AC_OPEN         40
#define AC_CLOSE        41

#define BEG_UPDATE      1
#define END_UPDATE      0
#define BEG_MCTRL       3
#define END_MCTRL       2

#define SHW_NOEXEC      0       /* shel_write(): back to the desktop */
#define SHW_EXEC        1       /* ... run this program next */

#define FMD_START       0               /* form_dial() */
#define FMD_GROW        1
#define FMD_SHRINK      2
#define FMD_FINISH      3

#define M_OFF           256
#define M_ON            257

extern short gl_apid;

short appl_init(void);
short appl_exit(void);
short evnt_mesag(short *msg);
short evnt_timer(unsigned long ms);
short appl_yield(void);
short appl_write(short id, short length, const void *msg);
short menu_register(short apid, const char *name);
short shel_write(short doex, short isgraf, short isover,
                 const char *cmd, const char *tail);
short form_alert(short defbutton, const char *text);
short form_dial(short flag, short lx, short ly, short lw, short lh,
                short bx, short by, short bw, short bh);
short graf_handle(short *wchar, short *hchar, short *wbox, short *hbox);
short graf_mouse(short form, const void *mform);
short graf_mkstate(short *mx, short *my, short *mstate, short *kstate);
short wind_update(short mode);
short objc_draw(OBJECT *tree, short start, short depth,
                short x, short y, short w, short h);
short objc_find(OBJECT *tree, short start, short depth, short mx, short my);

/* evnt_multi() for a button press or a timeout: returns the MU_ flags */
short evnt_multi_button_timer(unsigned long ms, short *mx, short *my,
                              short *button, short *kstate, short *key,
                              short *clicks, short *msg);

/* evnt_multi() for a message or a timeout: returns the MU_ flags */
short evnt_multi_mesag_timer(unsigned long ms, short *msg);

/* ---- windows ---- */

#define NAME            0x0001          /* window parts */
#define CLOSER          0x0002
#define FULLER          0x0004
#define MOVER           0x0008
#define SIZER           0x0020

#define WM_REDRAW       20              /* messages */
#define WM_TOPPED       21
#define WM_CLOSED       22
#define WM_FULLED       23
#define WM_SIZED        27
#define WM_MOVED        28

#define WF_NAME         2               /* wind_get() / wind_set() */
#define WF_WORKXYWH     4
#define WF_CURRXYWH     5
#define WF_PREVXYWH     6
#define WF_FULLXYWH     7
#define WF_TOP          10
#define WF_FIRSTXYWH    11
#define WF_NEXTXYWH     12

#define WC_BORDER       0               /* wind_calc() */
#define WC_WORK         1

short wind_create(short kind, short x, short y, short w, short h);
short wind_open(short handle, short x, short y, short w, short h);
short wind_close(short handle);
short wind_delete(short handle);
short wind_get(short handle, short field,
               short *a, short *b, short *c, short *d);
short wind_set(short handle, short field, short a, short b, short c, short d);
short wind_set_name(short handle, const char *name);
short wind_calc(short type, short kind, short x, short y, short w, short h,
                short *ox, short *oy, short *ow, short *oh);
short wind_find(short x, short y);
short wind_new(void);

/* ---- events ---- */

/*
 * The general form.  The eight arrays the AES fills are all optional
 * except msg, which must have room for eight words whenever MU_MESAG is
 * asked for; pass 0 for anything you do not want back.
 */
short evnt_multi(short flags, short clicks, short bmask, short bstate,
                 short m1flags, short m1x, short m1y, short m1w, short m1h,
                 short m2flags, short m2x, short m2y, short m2w, short m2h,
                 short *msg, unsigned long ms,
                 short *mx, short *my, short *button, short *kstate,
                 short *key, short *clicks_out);
short evnt_keybd(void);
short evnt_button(short clicks, short mask, short state,
                  short *mx, short *my, short *button, short *kstate);
short evnt_mouse(short flags, short x, short y, short w, short h,
                 short *mx, short *my, short *button, short *kstate);

/* ---- menus ---- */

short menu_bar(OBJECT *tree, short show);
short menu_icheck(OBJECT *tree, short item, short check);
short menu_ienable(OBJECT *tree, short item, short enable);
short menu_tnormal(OBJECT *tree, short title, short normal);
short menu_text(OBJECT *tree, short item, const char *text);

/* ---- object trees ---- */

short objc_add(OBJECT *tree, short parent, short child);
short objc_delete(OBJECT *tree, short obj);
short objc_order(OBJECT *tree, short obj, short newpos);
short objc_offset(OBJECT *tree, short obj, short *x, short *y);
short objc_change(OBJECT *tree, short obj, short x, short y, short w, short h,
                  short newstate, short redraw);
short objc_edit(OBJECT *tree, short obj, short ch, short *idx, short kind);

/* the kinds of objc_edit() */
#define ED_START        0
#define ED_INIT         1
#define ED_CHAR         2
#define ED_END          3

/* ---- dialogues ---- */

/*
 * A dialogue needs no resource file: an OBJECT tree built in the program
 * serves form_center() and form_do() just as well as one loaded from
 * disk, and on a machine whose programs are compiled next to the system
 * that is often the plainer way round.
 */
short form_do(OBJECT *tree, short start);
short form_center(OBJECT *tree, short *x, short *y, short *w, short *h);
short form_error(short err);
short form_button(OBJECT *tree, short obj, short clicks, short *nxtobj);
short form_keybd(OBJECT *tree, short obj, short nxtobj, short thechar,
                 short *nxtout, short *charout);

/* ---- rubber bands and boxes ---- */

short graf_rubberbox(short x, short y, short minw, short minh,
                     short *endw, short *endh);
short graf_dragbox(short w, short h, short sx, short sy,
                   short bx, short by, short bw, short bh,
                   short *endx, short *endy);
short graf_movebox(short w, short h, short sx, short sy, short dx, short dy);
short graf_growbox(short sx, short sy, short sw, short sh,
                   short dx, short dy, short dw, short dh);
short graf_shrinkbox(short dx, short dy, short dw, short dh,
                     short sx, short sy, short sw, short sh);
short graf_watchbox(OBJECT *tree, short obj, short instate, short outstate);
short graf_slidebox(OBJECT *tree, short parent, short obj, short isvert);

/* ---- the file selector, the scrap directory, other applications ---- */

short fsel_input(char *path, char *sel, short *button);
short fsel_exinput(char *path, char *sel, short *button, const char *label);
short scrp_read(char *path);
short scrp_write(const char *path);
short appl_find(const char *name);
short appl_read(short id, short length, void *buf);
short shel_read(char *cmd, char *tail);

/* ---- resources ---- */

short rsrc_load(const char *name);
short rsrc_free(void);
short rsrc_gaddr(short type, short index, void *addr);

/* the types rsrc_gaddr() knows */
#define R_TREE          0
#define R_OBJECT        1
#define R_TEDINFO       2
#define R_ICONBLK       3
#define R_BITBLK        4
#define R_STRING        5
#define R_IMAGEDATA     6
#define R_OBSPEC        7
#define R_TEPTEXT       8
#define R_TEPTMPLT      9
#define R_TEPVALID      10
#define R_IBPMASK       11
#define R_IBPDATA       12
#define R_IBPTEXT       13
#define R_BIPDATA       14
#define R_FRSTR         15
#define R_FRIMG         16

/* ---- VDI ---- */

#define MD_REPLACE      1
#define MD_TRANS        2
#define FIS_SOLID       1

/*
 * A bitmap, for vro_cpyfm().  fd_addr is the pixels, or 0 for the screen.
 * fd_wdwidth is the width in words, not bytes, and is what a line of the
 * bitmap is long -- so a 16 bits per pixel bitmap has fd_wdwidth == fd_w.
 * fd_stand 0 says the pixels are already in the screen's own format,
 * which is what you want: the standard format exists to carry a bitmap
 * between machines, and converting costs a copy.
 */
typedef struct
{
    void  *fd_addr;
    short  fd_w, fd_h;
    short  fd_wdwidth;
    short  fd_stand;
    short  fd_nplanes;
    short  fd_r1, fd_r2, fd_r3;
} MFDB;

/* the logic operations vro_cpyfm() can apply; S is source, D destination */
#define ALL_WHITE       0
#define S_AND_D         1
#define S_ONLY          3       /* plain copy */
#define NOT_S_AND_D     4
#define S_XOR_D         6
#define S_OR_D          7
#define ALL_BLACK       15

short v_opnvwk(short phys_handle);      /* returns the new handle, 0 on error */
void v_clsvwk(short handle);
void vs_clip(short handle, short on, const short *xyxy);
void vswr_mode(short handle, short mode);
void vsl_color(short handle, short color);
void vsf_color(short handle, short color);
void vsf_interior(short handle, short style);
void vst_color(short handle, short color);
void v_pline(short handle, short count, const short *xy);
void vr_recfl(short handle, const short *xyxy);
void v_gtext(short handle, short x, short y, const char *s);

/*
 * Copy a rectangle from one bitmap to another: eight coordinates, the
 * source rectangle then the destination one, each as two corners.  Either
 * MFDB may be the screen (fd_addr 0), so this is how a picture a program
 * computed itself gets into a window -- and the only way that does not
 * cost a VDI call per pixel.
 */
void vro_cpyfm(short handle, short mode, const short *xyxy8,
               const MFDB *src, const MFDB *dst);

/*
 * The same, but the source is a single-plane mask: where a bit is set the
 * pixel becomes col[1], where it is clear col[0] -- or, in the
 * transparent modes, is left alone.  That is how a shape with a hole in
 * it is drawn, and why this exists beside the opaque copy.
 */
void vrt_cpyfm(short handle, short mode, const short *xyxy8,
               const MFDB *src, const MFDB *dst, short fg, short bg);

/* the writing modes of vrt_cpyfm() */
#define MD_TRANS_FG     1       /* set bits in the foreground colour */
#define MD_TRANS_BG     2       /* clear bits in the background colour */
#define MD_XOR_FM       3
#define MD_ERASE_FM     4

/* ---- colour ---- */

/*
 * A pen's colour, in thousandths of full intensity -- 0 to 1000 for each
 * of red, green and blue, which is the VDI's own scale and not the
 * hardware's.  On a truecolor screen this is how a program gets at more
 * than the sixteen colours it is given: there are 256 pens, and every
 * one of them can be set to anything.
 */
void vs_color(short handle, short index, const short *rgb);
void vq_color(short handle, short index, short flag, short *rgb);

#define VQ_REQUESTED    0       /* what was asked for */
#define VQ_ACTUAL       1       /*  and what the hardware could do */

/* ---- lines, fills, markers ---- */

void vsl_type(short handle, short style);
void vsl_width(short handle, short width);
void vsl_ends(short handle, short beg, short end);
void vsf_style(short handle, short style);
void vsf_perimeter(short handle, short on);
void v_fillarea(short handle, short count, const short *xy);
void v_contourfill(short handle, short x, short y, short index);
void v_clrwk(short handle);

/* line types for vsl_type() */
#define SOLID           1
#define LDASHED         2
#define DOTTED          3
#define DASHDOT         4
#define DASH            5
#define DASHDOTDOT      6
#define USERLINE        7

/* ends for vsl_ends() */
#define SQUARED         0
#define ARROWED         1
#define ROUNDED         2

/* fill interiors for vsf_interior() */
#define FIS_HOLLOW      0
#define FIS_PATTERN     2
#define FIS_HATCH       3
#define FIS_USER        4

/* ---- the graphics primitives (one opcode, a subfunction each) ---- */

void v_bar(short handle, const short *xyxy);
void v_circle(short handle, short x, short y, short radius);
void v_ellipse(short handle, short x, short y, short xradius, short yradius);
void v_arc(short handle, short x, short y, short radius,
           short begang, short endang);
void v_pieslice(short handle, short x, short y, short radius,
                short begang, short endang);
void v_rbox(short handle, const short *xyxy);
void v_rfbox(short handle, const short *xyxy);

/* ---- text ---- */

void vst_height(short handle, short height,
                short *cw, short *ch, short *bw, short *bh);
void vst_point(short handle, short point,
               short *cw, short *ch, short *bw, short *bh);
void vst_font(short handle, short font);
void vst_rotation(short handle, short angle);
void vst_effects(short handle, short effects);
void vst_alignment(short handle, short hin, short vin,
                   short *hout, short *vout);

/*
 * How wide and how tall a string would be, as four corners.  Worth
 * having before laying anything out: a font's characters are not all
 * eight pixels wide, and code that assumes they are lays out correctly
 * only by accident.
 *
 * The corners are the string's own size anchored at (0,0), not an offset
 * from where it would be drawn: (0,0), (w,0), (w,h), (0,h).  So they go
 * beside the *top left* of the text, and v_gtext() puts its y on the
 * baseline unless vst_alignment() has been told otherwise.  Adding the
 * extent to a baseline y drops the box a whole line -- ask for TA_TOP
 * first, or subtract the height.
 */
void vqt_extent(short handle, const char *s, short *extent);

/* text effects, which combine */
#define TF_THICKENED    0x01
#define TF_LIGHTENED    0x02
#define TF_SKEWED       0x04
#define TF_UNDERLINED   0x08
#define TF_OUTLINED     0x10
#define TF_SHADOWED     0x20

/* horizontal alignment for vst_alignment() */
#define TA_LEFT         0
#define TA_CENTER       1
#define TA_RIGHT        2
/* and vertical */
#define TA_BASELINE     0
#define TA_HALF         1
#define TA_ASCENT       2
#define TA_BOTTOM       3
#define TA_DESCENT      4
#define TA_TOP          5

/* ---- asking the screen ---- */

void v_get_pixel(short handle, short x, short y, short *pel, short *index);
void v_show_c(short handle, short reset);
void v_hide_c(short handle);


#ifdef __cplusplus
}
#endif

#endif /* GEM_H */
