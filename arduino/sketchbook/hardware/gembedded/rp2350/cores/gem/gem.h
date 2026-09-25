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

#define FMD_FINISH      3

#define M_OFF           256
#define M_ON            257

extern short gl_apid;

short appl_init(void);
short appl_exit(void);
short evnt_mesag(short *msg);
short evnt_timer(unsigned long ms);
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

/* ---- VDI ---- */

#define MD_REPLACE      1
#define MD_TRANS        2
#define FIS_SOLID       1

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

#endif /* GEM_H */
