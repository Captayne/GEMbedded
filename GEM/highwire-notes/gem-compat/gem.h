/*
 * gem.h - gemlib's umbrella header, as GEMbedded provides it
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * An Atari GEM program written against gemlib includes one header and
 * expects the whole of the AES and the VDI behind it, plus gemlib's own
 * conveniences: the GRECT-shaped window calls, the short types, a few
 * constants.  Our bindings live in gem.h and cover what our own programs
 * needed, which is less.
 *
 * This is the difference, and nothing else.  It includes gem.h and adds:
 *
 *   - the type names gemlib programs use (WORD, BOOL, MFORM, TEDINFO ...)
 *   - the GRECT forms of wind_get/set/open/create/calc, which are the same
 *     calls with the four shorts gathered into a rectangle
 *   - five AES/VDI calls pTOS does not have at all
 *   - and stubs for the Speedo/GDOS calls, which have to link but are
 *     never reached: HighWire only makes them when has_fsm_gdos is true,
 *     and on this machine it is not (see its romvdi.c).
 *
 * The stubs are honest about being stubs: each returns what a VDI without
 * a font GDOS would return, so that a caller which ignores our advice and
 * calls one anyway gets a defensible answer rather than a crash.
 */

#ifndef GEMBEDDED_GEMLIB_H
#define GEMBEDDED_GEMLIB_H

/* the board package's own bindings, one directory further on */
#include_next <gem.h>


#ifdef __cplusplus
extern "C" {
#endif

/* ---- types -----------------------------------------------------------
 *
 * WORD, UWORD, LONG, ULONG, CHAR and BOOL are NOT declared here.  A
 * program written against gemlib gets them from gemlib; HighWire, which
 * has to build without it too, declares them itself in hw-types.h and
 * #undefs them first.  Declaring them here as well makes BOOL a short in
 * one header and an enum in the other, and the file that includes both
 * does not compile.
 *
 * GRECT is shared the way hw-types.h expects it to be shared: same struct
 * tag, same guard, so whichever header is read first wins and the other
 * steps over it.
 */

#ifndef __GRECT
typedef struct graphic_rectangle {
    short   g_x, g_y, g_w, g_h;
} GRECT;
# define __GRECT
#endif

#ifndef __PXY
typedef struct point_coord {
    short   p_x, p_y;
} PXY;
# define __PXY
#endif

/* ---- structures the AES hands about --------------------------------- */

/* OBSPEC now comes from the board package itself. */

typedef struct _tedinfo
{
    char   *te_ptext;
    char   *te_ptmplt;
    char   *te_pvalid;
    short   te_font, te_fontid;
    short   te_just, te_color, te_fontsize;
    short   te_thickness, te_txtlen, te_tmplen;
} TEDINFO;

typedef struct
{
    short   mf_xhot, mf_yhot;
    short   mf_nplanes;
    short   mf_fg, mf_bg;
    short   mf_mask[16];
    short   mf_data[16];
} MFORM;

/*
 * What vqt_xfntinfo() fills in.  Only the fields HighWire reads are
 * named; the rest is the space the call would have written into.
 */
typedef struct
{
    long    format;
    short   id;
    short   index;
    char    font_name[50];
    char    family_name[50];
    char    style_name[50];
    char    file_name1[200];
    char    file_name2[200];
    char    file_name3[200];
    short   pt_cnt;
    short   pt_sizes[64];
} XFNT_INFO;

/* ---- constants gemlib defines and we did not ------------------------- */

/*
 * The two parameter blocks of evnt_multi_fast().  These are structures,
 * not numbers -- HighWire declares a static EVMULT_IN and fills it with a
 * braced initialiser (its HighWire.c:61), so both the field names and
 * their order have to be right.  Both are taken from that initialiser and
 * from every emi_/emo_ field the source touches.
 */
typedef struct {
    short   emi_flags;
    short   emi_bclicks;
    short   emi_bmask;
    short   emi_bstate;
    short   emi_m1leave;
    GRECT   emi_m1;
    short   emi_m2leave;
    GRECT   emi_m2;
    short   emi_tlow;
    short   emi_thigh;
} EVMULT_IN;

typedef struct {
    PXY     emo_mouse;
    short   emo_mbutton;
    short   emo_kmeta;
    short   emo_kreturn;
    short   emo_mclicks;
} EVMULT_OUT;

#ifndef TA_CENTER
#define TA_LEFT         0       /* vst_alignment(), horizontal */
#define TA_CENTER       1
#define TA_RIGHT        2
#endif

#ifndef ALL_BLACK
#define ALL_WHITE       0       /* vr_recfl() / vro_cpyfm() write modes */
#define S_AND_D         1
#define S_ONLY          3
#define S_XOR_D         6
#define S_OR_D          7
#define ALL_BLACK       15
#endif

/* S_WRITE comes from libcmini's mint/ostruct.h. */

/* The VDI's sixteen logical pens, by name. */
#ifndef LOG_BLACK
#define LOG_WHITE       0
#define LOG_BLACK       1
#define LOG_RED         2
#define LOG_GREEN       3
#define LOG_BLUE        4
#define LOG_CYAN        5
#define LOG_YELLOW      6
#define LOG_MAGENTA     7
#define LOG_LWHITE      8
#define LOG_LBLACK      9
#define LOG_LRED        10
#define LOG_LGREEN      11
#define LOG_LBLUE       12
#define LOG_LCYAN       13
#define LOG_LYELLOW     14
#define LOG_LMAGENTA    15
#endif

/* ---- the constants gemlib names and the board package did not -------
 *
 * All standard GEM values.  Each is guarded, so where the bindings above
 * already have a name, theirs stands.
 */

/* the sixteen VDI pens, as an object's colour word writes them */
#ifndef G_WHITE
#define G_WHITE         0
#endif
#ifndef G_BLACK
#define G_BLACK         1
#endif
#ifndef G_RED
#define G_RED           2
#endif
#ifndef G_GREEN
#define G_GREEN         3
#endif
#ifndef G_BLUE
#define G_BLUE          4
#endif
#ifndef G_CYAN
#define G_CYAN          5
#endif
#ifndef G_YELLOW
#define G_YELLOW        6
#endif
#ifndef G_MAGENTA
#define G_MAGENTA       7
#endif
#ifndef G_LWHITE
#define G_LWHITE        8
#endif
#ifndef G_LBLACK
#define G_LBLACK        9
#endif
#ifndef G_LRED
#define G_LRED          10
#endif
#ifndef G_LGREEN
#define G_LGREEN        11
#endif
#ifndef G_LBLUE
#define G_LBLUE         12
#endif
#ifndef G_LCYAN
#define G_LCYAN         13
#endif
#ifndef G_LYELLOW
#define G_LYELLOW       14
#endif
#ifndef G_LMAGENTA
#define G_LMAGENTA      15
#endif

/* ob_flags, gemlib's spelling of the flags gem.h calls SELECTABLE etc. */
#ifndef OF_NONE
#define OF_NONE         0x0000
#endif
#ifndef OF_SELECTABLE
#define OF_SELECTABLE   0x0001
#endif
#ifndef OF_DEFAULT
#define OF_DEFAULT      0x0002
#endif
#ifndef OF_EXIT
#define OF_EXIT         0x0004
#endif
#ifndef OF_EDITABLE
#define OF_EDITABLE     0x0008
#endif
#ifndef OF_RBUTTON
#define OF_RBUTTON      0x0010
#endif
#ifndef OF_LASTOB
#define OF_LASTOB       0x0020
#endif
#ifndef OF_TOUCHEXIT
#define OF_TOUCHEXIT    0x0040
#endif
#ifndef OF_HIDETREE
#define OF_HIDETREE     0x0080
#endif
#ifndef OF_INDIRECT
#define OF_INDIRECT     0x0100
#endif
#ifndef OF_FL3DIND
#define OF_FL3DIND      0x0200
#endif
#ifndef OF_FL3DACT
#define OF_FL3DACT      0x0400
#endif
#ifndef OF_FL3DBAK
#define OF_FL3DBAK      0x0600
#endif
#ifndef OF_SUBMENU
#define OF_SUBMENU      0x0800
#endif
#ifndef OF_FLAG15
#define OF_FLAG15       0x8000
#endif

/* ob_state */
#ifndef OS_NORMAL
#define OS_NORMAL       0x0000
#endif
#ifndef OS_SELECTED
#define OS_SELECTED     0x0001
#endif
#ifndef OS_CROSSED
#define OS_CROSSED      0x0002
#endif
#ifndef OS_CHECKED
#define OS_CHECKED      0x0004
#endif
#ifndef OS_DISABLED
#define OS_DISABLED     0x0008
#endif
#ifndef OS_OUTLINED
#define OS_OUTLINED     0x0010
#endif
#ifndef OS_SHADOWED
#define OS_SHADOWED     0x0020
#endif
#ifndef OS_WHITEBAK
#define OS_WHITEBAK     0x0040
#endif
#ifndef OS_DRAW3D
#define OS_DRAW3D       0x0080
#endif

#ifndef ROOT
#define ROOT            0       /* the first object of a tree */
#endif

#ifndef DESKTOP_HANDLE
#define DESKTOP_HANDLE  0       /* wind_get() on the desktop */
#endif

/* the window parts gem.h does not name */
#ifndef FULLER
#define FULLER          0x0004
#endif
#ifndef INFO
#define INFO            0x0010
#endif
#ifndef UPARROW
#define UPARROW         0x0040
#endif
#ifndef DNARROW
#define DNARROW         0x0080
#endif
#ifndef VSLIDE
#define VSLIDE          0x0100
#endif
#ifndef LFARROW
#define LFARROW         0x0200
#endif
#ifndef RTARROW
#define RTARROW         0x0400
#endif
#ifndef HSLIDE
#define HSLIDE          0x0800
#endif
#ifndef SMALLER
#define SMALLER         0x4000
#endif

#ifndef WF_INFO
#define WF_INFO         3       /* wind_set() with the info line */
#endif

/* graf_mouse() forms */
#ifndef ARROW
#define ARROW           0
#endif
#ifndef TEXT_CRSR
#define TEXT_CRSR       1
#endif
#ifndef BUSYBEE
#define BUSYBEE         2
#endif
#ifndef POINT_HAND
#define POINT_HAND      3
#endif
#ifndef FLAT_HAND
#define FLAT_HAND       4
#endif
#ifndef THIN_CROSS
#define THIN_CROSS      5
#endif
#ifndef THICK_CROSS
#define THICK_CROSS     6
#endif
#ifndef OUTLN_CROSS
#define OUTLN_CROSS     7
#endif
#ifndef USER_DEF
#define USER_DEF        255
#endif
#ifndef M_OFF
#define M_OFF           256
#endif
#ifndef M_ON
#define M_ON            257
#endif

/* vswr_mode() */
#ifndef MD_REPLACE
#define MD_REPLACE      1
#endif
#ifndef MD_TRANS
#define MD_TRANS        2
#endif
#ifndef MD_XOR
#define MD_XOR          3
#endif
#ifndef MD_ERASE
#define MD_ERASE        4
#endif

/* vst_effects() */
#ifndef TXT_NORMAL
#define TXT_NORMAL      0x00
#endif
#ifndef TXT_THICKENED
#define TXT_THICKENED   0x01
#endif
#ifndef TXT_LIGHTENED
#define TXT_LIGHTENED   0x02
#endif
#ifndef TXT_SKEWED
#define TXT_SKEWED      0x04
#endif
#ifndef TXT_UNDERLINED
#define TXT_UNDERLINED  0x08
#endif
#ifndef TXT_OUTLINED
#define TXT_OUTLINED    0x10
#endif
#ifndef TXT_SHADOWED
#define TXT_SHADOWED    0x20
#endif

/* vst_alignment(), vertical */
#ifndef TA_BASE
#define TA_BASE         0
#endif
#ifndef TA_HALF
#define TA_HALF         1
#endif
#ifndef TA_ASCENT
#define TA_ASCENT       2
#endif
#ifndef TA_BOTTOM
#define TA_BOTTOM       3
#endif
#ifndef TA_DESCENT
#define TA_DESCENT      4
#endif
#ifndef TA_TOP
#define TA_TOP          5
#endif

/* evnt_multi() mouse rectangles */
#ifndef MO_ENTER
#define MO_ENTER        0
#endif
#ifndef MO_LEAVE
#define MO_LEAVE        1
#endif

/* the drag-and-drop protocol's replies */
#ifndef DD_OK
#define DD_OK           0
#endif
#ifndef DD_NAK
#define DD_NAK          1
#endif
#ifndef DD_EXT
#define DD_EXT          2
#endif
#ifndef DD_LEN
#define DD_LEN          3
#endif

/* ---- AES messages, window fields and the rest ------------------------
 *
 * Standard GEM values.  Guarded one by one, because a block guard skips
 * every name in it the moment the bindings above happen to have one.
 */

/* what arrives in the message buffer of evnt_multi() */
#ifndef MN_SELECTED
#define MN_SELECTED      10
#endif
#ifndef WM_REDRAW
#define WM_REDRAW        20
#endif
#ifndef WM_TOPPED
#define WM_TOPPED        21
#endif
#ifndef WM_CLOSED
#define WM_CLOSED        22
#endif
#ifndef WM_FULLED
#define WM_FULLED        23
#endif
#ifndef WM_ARROWED
#define WM_ARROWED       24
#endif
#ifndef WM_HSLID
#define WM_HSLID         25
#endif
#ifndef WM_VSLID
#define WM_VSLID         26
#endif
#ifndef WM_SIZED
#define WM_SIZED         27
#endif
#ifndef WM_MOVED
#define WM_MOVED         28
#endif
#ifndef WM_NEWTOP
#define WM_NEWTOP        29
#endif
#ifndef WM_UNTOPPED
#define WM_UNTOPPED      30
#endif
#ifndef WM_ONTOP
#define WM_ONTOP         31
#endif
#ifndef WM_OFFTOP
#define WM_OFFTOP        32
#endif
#ifndef WM_BOTTOMED
#define WM_BOTTOMED      33
#endif
#ifndef WM_ICONIFY
#define WM_ICONIFY       34
#endif
#ifndef WM_UNICONIFY
#define WM_UNICONIFY     35
#endif
#ifndef WM_ALLICONIFY
#define WM_ALLICONIFY    36
#endif
#ifndef WM_TOOLBAR
#define WM_TOOLBAR       37
#endif
/* wind_get() and wind_set() fields */
#ifndef WF_KIND
#define WF_KIND          1
#endif
#ifndef WF_HSLIDE
#define WF_HSLIDE        8
#endif
#ifndef WF_VSLIDE
#define WF_VSLIDE        9
#endif
#ifndef WF_RESVD
#define WF_RESVD         13
#endif
#ifndef WF_NEWDESK
#define WF_NEWDESK       14
#endif
#ifndef WF_HSLSIZE
#define WF_HSLSIZE       15
#endif
#ifndef WF_VSLSIZE
#define WF_VSLSIZE       16
#endif
#ifndef WF_SCREEN
#define WF_SCREEN        17
#endif
#ifndef WF_COLOR
#define WF_COLOR         18
#endif
#ifndef WF_DCOLOR
#define WF_DCOLOR        19
#endif
#ifndef WF_OWNER
#define WF_OWNER         20
#endif
#ifndef WF_BEVENT
#define WF_BEVENT        24
#endif
#ifndef WF_BOTTOM
#define WF_BOTTOM        25
#endif
#ifndef WF_ICONIFY
#define WF_ICONIFY       26
#endif
#ifndef WF_UNICONIFY
#define WF_UNICONIFY     27
#endif
#ifndef WF_UNICONIFYXYWH
#define WF_UNICONIFYXYWH 28
#endif
#ifndef WF_TOOLBAR
#define WF_TOOLBAR       30
#endif
#ifndef WF_FTOOLBAR
#define WF_FTOOLBAR      31
#endif
#ifndef WF_NTOOLBAR
#define WF_NTOOLBAR      32
#endif
#ifndef WF_MENU
#define WF_MENU          33
#endif
/* which arrow WM_ARROWED means */
#ifndef WA_UPPAGE
#define WA_UPPAGE        0
#endif
#ifndef WA_DNPAGE
#define WA_DNPAGE        1
#endif
#ifndef WA_UPLINE
#define WA_UPLINE        2
#endif
#ifndef WA_DNLINE
#define WA_DNLINE        3
#endif
#ifndef WA_LFPAGE
#define WA_LFPAGE        4
#endif
#ifndef WA_RTPAGE
#define WA_RTPAGE        5
#endif
#ifndef WA_LFLINE
#define WA_LFLINE        6
#endif
#ifndef WA_RTLINE
#define WA_RTLINE        7
#endif
/* the objects a window's own tree is made of */
#ifndef W_BOX
#define W_BOX            0
#endif
#ifndef W_TITLE
#define W_TITLE          1
#endif
#ifndef W_CLOSER
#define W_CLOSER         2
#endif
#ifndef W_NAME
#define W_NAME           3
#endif
#ifndef W_FULLER
#define W_FULLER         4
#endif
#ifndef W_INFO
#define W_INFO           5
#endif
#ifndef W_DATA
#define W_DATA           6
#endif
#ifndef W_WORK
#define W_WORK           7
#endif
#ifndef W_SIZER
#define W_SIZER          8
#endif
#ifndef W_VBAR
#define W_VBAR           9
#endif
#ifndef W_UPARROW
#define W_UPARROW        10
#endif
#ifndef W_DNARROW
#define W_DNARROW        11
#endif
#ifndef W_VSLIDE
#define W_VSLIDE         12
#endif
#ifndef W_VELEV
#define W_VELEV          13
#endif
#ifndef W_HBAR
#define W_HBAR           14
#endif
#ifndef W_LFARROW
#define W_LFARROW        15
#endif
#ifndef W_RTARROW
#define W_RTARROW        16
#endif
#ifndef W_HSLIDE
#define W_HSLIDE         17
#endif
#ifndef W_HELEV
#define W_HELEV          18
#endif
#ifndef W_SMALLER
#define W_SMALLER        19
#endif
/* menus, buttons, text justification, the file selector */
#ifndef MENU_REMOVE
#define MENU_REMOVE      0
#endif
#ifndef MENU_INSTALL
#define MENU_INSTALL     1
#endif
#ifndef CHECK
#define CHECK            1
#endif
#ifndef UNCHECK
#define UNCHECK          0
#endif
#ifndef HIGHLIGHT
#define HIGHLIGHT        0
#endif
#ifndef UNHIGHLIGHT
#define UNHIGHLIGHT      1
#endif
#ifndef LEFT_BUTTON
#define LEFT_BUTTON      1
#endif
#ifndef RIGHT_BUTTON
#define RIGHT_BUTTON     2
#endif
#ifndef TE_LEFT
#define TE_LEFT          0
#endif
#ifndef TE_RIGHT
#define TE_RIGHT         1
#endif
#ifndef TE_CNTR
#define TE_CNTR          2
#endif
#ifndef FSEL_CANCEL
#define FSEL_CANCEL      0
#endif
#ifndef FSEL_OK
#define FSEL_OK          1
#endif
/* VDI fill interiors and their outline */
#ifndef PERIMETER_OFF
#define PERIMETER_OFF    0
#endif
#ifndef PERIMETER_ON
#define PERIMETER_ON     1
#endif
#ifndef IP_HOLLOW
#define IP_HOLLOW        0
#endif
#ifndef IP_1PATT
#define IP_1PATT         1
#endif
#ifndef IP_2PATT
#define IP_2PATT         2
#endif
#ifndef IP_3PATT
#define IP_3PATT         3
#endif
#ifndef IP_4PATT
#define IP_4PATT         4
#endif
#ifndef IP_5PATT
#define IP_5PATT         5
#endif
#ifndef IP_6PATT
#define IP_6PATT         6
#endif
#ifndef IP_SOLID
#define IP_SOLID         8
#endif
/* ---- values still to be checked against gemlib ------------------------
 *
 * EVERY NUMBER BELOW IS A BEST GUESS
 *
 * These are not in HighWire's own sources and not documented anywhere I
 * could check from here, so the values come from memory of the GEM API
 * rather than from a source.  A wrong constant compiles perfectly and
 * then behaves wrongly at run time, which is worse than not compiling at
 * all -- so they are separated out rather than mixed in above, and each
 * has to be confirmed against a real gemlib header before this is used
 * for anything but counting how far the build gets.
 */

/* vst_kern() and vst_scratch() -- UNVERIFIED */
#ifndef TRACK_NONE
#define TRACK_NONE       0
#endif
#ifndef TRACK_NORMAL
#define TRACK_NORMAL     1
#endif
#ifndef TRACK_TIGHT
#define TRACK_TIGHT      2
#endif
#ifndef TRACK_VERYTIGHT
#define TRACK_VERYTIGHT  3
#endif
#ifndef PAIR_OFF
#define PAIR_OFF         0
#endif
#ifndef PAIR_ON
#define PAIR_ON          1
#endif
#ifndef SCRATCH_BOTH
#define SCRATCH_BOTH     0
#endif
#ifndef SCRATCH_BIG
#define SCRATCH_BIG      1
#endif
#ifndef SCRATCH_NONE
#define SCRATCH_NONE     2
#endif
/* shel_write, appl_getinfo, drag and drop -- UNVERIFIED */
#ifndef SWM_BROADCAST
#define SWM_BROADCAST    7
#endif
#ifndef SWM_NEWMSG
#define SWM_NEWMSG       8
#endif
#ifndef SWM_ENVIRON
#define SWM_ENVIRON      9
#endif
#ifndef AES_WINDOW
#define AES_WINDOW       9
#endif
#ifndef DD_NAMEMAX
#define DD_NAMEMAX       128
#endif
#ifndef DD_EXTSIZE
#define DD_EXTSIZE       32
#endif
#ifndef DD_TIMEOUT
#define DD_TIMEOUT       4000
#endif
#ifndef MAX_LEN
#define MAX_LEN          128
#endif
#ifndef WF_WINX
#define WF_WINX          0x5758
#endif
/*
 * gemlib keeps the AES version appl_init() returned in a global.  Ours is
 * a definition in gemx.c rather than an extern, because nothing sets it
 * yet -- another thing to finish.
 */
extern short gl_ap_version;

/* ---- the GRECT forms ------------------------------------------------
 *
 * The same calls, with the four shorts gathered into a rectangle.  Inline
 * so that nothing is linked for them.
 */

/* wind_get, wind_set, wind_calc, wind_create, wind_open, vs_clip and
 * evnt_multi are already declared by the bindings above.  Declaring
 * them again here is how this header first broke four files that had
 * compiled without it: pTOS takes an unsigned long for evnt_multi's
 * timer where gemlib takes two shorts, and the two prototypes cannot
 * both be right. */

static __inline short wind_get_grect(short handle, short what, GRECT *r)
{
    return wind_get(handle, what, &r->g_x, &r->g_y, &r->g_w, &r->g_h);
}

static __inline short wind_set_grect(short handle, short what, const GRECT *r)
{
    return wind_set(handle, what, r->g_x, r->g_y, r->g_w, r->g_h);
}

static __inline short wind_open_grect(short handle, const GRECT *r)
{
    return wind_open(handle, r->g_x, r->g_y, r->g_w, r->g_h);
}

static __inline short wind_create_grect(short parts, const GRECT *r)
{
    return wind_create(parts, r->g_x, r->g_y, r->g_w, r->g_h);
}

static __inline short wind_calc_grect(short type, short parts,
                                      const GRECT *in, GRECT *out)
{
    return wind_calc(type, parts, in->g_x, in->g_y, in->g_w, in->g_h,
                     &out->g_x, &out->g_y, &out->g_w, &out->g_h);
}

/*
 * wind_set() with a string: the title and the info line are kept by
 * address, so the pointer goes through in two of the int_in words and the
 * AES reads it back as a pointer -- gemwmlib.c's *(char **)pinwds.
 *
 * Which half goes first is therefore not a convention but a question of
 * memory order, and the answer is the other way round from the Atari.
 * On this little-endian machine the low half belongs at the lower word.
 * Putting the high half there, as the Atari documentation describes,
 * hands the AES a pointer with its halves swapped: it then draws the
 * window title from whatever that address points at, which is how
 * HighWire faulted at 0x91f0110a on a string that really lived at
 * 0x110a91f0.
 */
static __inline short wind_set_str(short handle, short what, const char *str)
{
    unsigned long p = (unsigned long)str;
    return wind_set(handle, what, (short)(p & 0xffff), (short)(p >> 16), 0, 0);
}

/* ---- clipping, which gemlib splits in two ---------------------------- */

void vs_clip(short handle, short clip_flag, const short *pxy);

static __inline void vs_clip_off(short handle)
{
    short pxy[4];
    pxy[0] = pxy[1] = pxy[2] = pxy[3] = 0;
    vs_clip(handle, 0, pxy);
}

static __inline void vs_clip_pxy(short handle, const PXY *pxy)
{
    vs_clip(handle, 1, (const short *)pxy);
}

/* ---- evnt_multi() with the parameter blocks gemlib passes ------------ */

short evnt_multi_fast(const EVMULT_IN *in, short *msg, EVMULT_OUT *out);

/* ---- rectangle arithmetic gemlib brings along ------------------------ */

short rc_intersect(const GRECT *src, GRECT *dst);

/* ---- where gemlib and the board package disagree about a shape --------
 *
 * Two calls exist on both sides with different signatures, and a macro is
 * the only way to have both: the declaration from the bindings above is
 * already parsed by the time these are seen, so the name is simply
 * redirected from here on.
 *
 *   v_opnvwk   the bindings take a physical handle and return the new
 *              one; gemlib fills a work_out array and returns nothing.
 *   vst_font   the bindings return nothing; gemlib returns the font that
 *              was actually set, which is not always the one asked for,
 *              and HighWire checks exactly that (its fontbase.c).
 */

void  gemlib_v_opnvwk(short *work_in, short *handle, short *work_out);
short gemlib_vst_font(short handle, short font);

/*
 *   vrt_cpyfm   the bindings take the two colours as two arguments;
 *               gemlib takes the pair as an array, which is what the VDI
 *               itself expects.
 */
void  gemlib_vrt_cpyfm(short handle, short mode, const short *pxy,
                       const MFDB *src, const MFDB *dst, const short *colour);

/*
 *   vst_point   the bindings return nothing; gemlib returns the point
 *               size actually set, and romvdi.c passes that straight on.
 */
short gemlib_vst_point(short handle, short point,
                       short *cw, short *ch, short *bw, short *bh);

/*
 *   objc_change  gemlib has a reserved argument after the object number
 *                that the bindings leave out.
 */
short gemlib_objc_change(OBJECT *tree, short obj, short resvd,
                         short x, short y, short w, short h,
                         short newstate, short redraw);

#define objc_change gemlib_objc_change
#define vst_point   gemlib_vst_point
#define v_opnvwk    gemlib_v_opnvwk
#define vst_font    gemlib_vst_font
#define vrt_cpyfm   gemlib_vrt_cpyfm

/* ---- what pTOS does not have at all ---------------------------------- */

short scrp_read(char *path);
short scrp_write(const char *path);
short appl_getinfo(short type, short *out1, short *out2,
                   short *out3, short *out4);

/*
 * Declared, but not implemented here: HighWire brings its own
 * appl_xgetinfo along in AEI.c, which asks the AES once and remembers the
 * answer.  Two definitions would not link, so gemx.c deliberately leaves
 * it out -- but Window.c and HighWire.c call it without including AEI's
 * header, and on the Atari it is gemlib's gem.h that declares it for
 * them.  Here that is this file's job.
 */
short appl_xgetinfo(short type, short *out1, short *out2,
                    short *out3, short *out4);
short rsrc_obfix(OBJECT *tree, short object);
void  vq_extnd(short handle, short owflag, short *work_out);
void  vsf_udpat(short handle, const short *pfill, short planes);
void  vsl_udsty(short handle, short pattern);
void  vr_trnfm(short handle, MFDB *src, MFDB *dst);
void  v_opnwk(const short *work_in, short *handle, short *work_out);

/*
 * Which GDOS is there.  On this machine: none.  HighWire asks once, at
 * start-up, and takes the ROM VDI path for the rest of its life.
 */
#define GDOS_NONE       (-2L)
#define GDOS_FSM        0x5F46534DL     /* '_FSM' */
#define GDOS_FNT        0x5F464E54L     /* '_FNT' */

static __inline long vq_vgdos(void)
{
    return GDOS_NONE;
}

/* ---- the Speedo/GDOS calls: present so that it links ------------------
 *
 * HighWire reaches these only when has_fsm_gdos is true, which it decides
 * from the cookie at start-up (its romvdi.c).  Without a font GDOS they
 * are never called, but they are named in code the compiler still has to
 * translate.  Each answers the way a VDI without a font GDOS answers.
 */

/*
 * Two of these take coordinates and the third takes a point.  That is
 * not a guess: HighWire's romvdi.c calls v_ftext and v_ftext16 with x
 * and y, and wraps the v_ftext16n call in #ifdef __PUREC__ precisely
 * because gemlib wants a PXY there and Pure C does not.
 */
void  v_ftext(short handle, short x, short y, const char *str);
void  v_ftext16(short handle, short x, short y, const short *str);
void  v_ftext16n(short handle, PXY pos, const short *str, short n);
short vqt_advance(short handle, short ch, short *advx, short *advy,
                  short *xrem, short *yrem);
short vqt_f_extent16n(short handle, const short *str, short n, short *extent);
short vqt_width(short handle, short ch, short *cw, short *ldelta,
                short *rdelta);
short vqt_fontinfo(short handle, short *minade, short *maxade,
                   short *distances, short *maxwidth, short *effects);
short vqt_xfntinfo(short handle, short flags, short id, short index,
                   XFNT_INFO *info);
void  vq_scrninfo(short handle, short *work_out);
short vst_arbpt(short handle, short point, short *wchar, short *hchar,
                short *wcell, short *hcell);
short vst_setsize32(short handle, long point, short *wchar, short *hchar,
                    short *wcell, short *hcell);
short vst_kern(short handle, short tmode, short pmode, short *tracks,
               short *pairs);
short vst_map_mode(short handle, short mode);
void  vst_scratch(short handle, short mode);
short vst_load_fonts(short handle, short select);
void  vst_unload_fonts(short handle, short select);


#ifdef __cplusplus
}
#endif

#endif /* GEMBEDDED_GEMLIB_H */
