/*
 * nkcc.h - normalised key codes, as GEMbedded provides them
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * NKCC (Harald Siegmund's Normalized Key Code Conversion) exists because
 * the same key reaches a program differently depending on the keyboard
 * table, the AES version and whether a key was pressed on the main block
 * or the keypad.  It normalises all of that to one number per key.
 *
 * HighWire uses very little of it: it calls nkc_init() once and then
 * compares against the NK_ constants in switch statements, masking the
 * modifier bits off first (its keyinput.c:87).
 *
 * WE DEFINE BOTH SIDES, SO THE NUMBERING IS OURS
 *
 * A normaliser is only required to be self-consistent: what matters is
 * that the codes a program compares against are the codes the conversion
 * produces.  Since the conversion here will be ours -- pTOS's IKBD
 * scancodes on one side, these constants on the other -- the values do
 * not have to match Harald Siegmund's.  They do anyway where HighWire's
 * own comments record them, because those turned out to be the plain
 * Atari scancodes and there is no reason to renumber.
 *
 * The scancodes below are the ones HighWire's source names in comments
 * (NK_TAB 0x0F, NK_F1 0x3B, NK_UP 0x48 and so on).  The ones it does not
 * name are the standard IKBD scancodes for those keys.  The modifier
 * bits are ours: any distinct bits above the low byte will do, because
 * the only thing done with them is masking them off.
 */

#ifndef GEMBEDDED_NKCC_H
#define GEMBEDDED_NKCC_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the key itself --------------------------------------------------
 *
 * NOT a bare scancode, and NOT the code the conversion produces either.
 * Two separate requirements pin this down, and HighWire imposes both in
 * the same few lines (hwWind.c:2619-2645, keyinput.c:87-94, Form.c:1752):
 *
 *   1. The code from the conversion must say whether it is a character,
 *      because that is how the branch is chosen:
 *
 *          if (!(nkey & NKF_FUNC))   insert nkey as a character
 *          else                      it is a key, switch on it
 *
 *   2. Inside that else, NKF_FUNC is taken off again before the switch,
 *      and the switch holds `case NK_RIGHT` next to `case 'M'`. 'M' is
 *      0x4D, which is also the right arrow's scancode, so a bare
 *      scancode would collide -- the two cases would be the same value
 *      and it would not compile.
 *
 * So the NK_ constants carry a marker that survives, and the conversion
 * adds NKF_FUNC on top of it:
 *
 *      NK_RIGHT              = NKF_KEY | 0x4d
 *      gem_to_norm() returns   NKF_FUNC | NKF_KEY | 0x4d
 *
 * NKF_KEY has to be a bit HighWire never masks off. It strips RESVD,
 * SHIFT, CTRL, CAPS -- and NUM in keyinput.c -- then FUNC. 0x4000 is
 * left alone by all of them.
 *
 * This file had NKF_FUNC in the constants themselves, which satisfied
 * requirement 2 and broke requirement 1 at the very next line: after
 * `nkey &= ~NKF_FUNC` the switch compared a stripped code against an
 * unstripped constant and never matched. Backspace and Delete did
 * nothing in a text field, and the arrows did nothing at all.
 *
 * Atari IKBD scancodes in the low byte.  Those marked (hw) are the values
 * HighWire's own source records in its comments.
 */

#define NKF_FUNC        0x0800  /* not a character: a key */
#define NKF_KEY         0x4000  /* part of every NK_ below; never stripped */

#define NK_ESC          (NKF_KEY  | 0x01)
#define NK_BS           (NKF_KEY  | 0x0E)
#define NK_TAB          (NKF_KEY  | 0x0F)    /* (hw) */
#define NK_RET          (NKF_KEY  | 0x1C)
#define NK_F1           (NKF_KEY  | 0x3B)    /* (hw) */
#define NK_F2           (NKF_KEY  | 0x3C)
#define NK_F3           (NKF_KEY  | 0x3D)
#define NK_F4           (NKF_KEY  | 0x3E)
#define NK_F5           (NKF_KEY  | 0x3F)    /* (hw) */
#define NK_F6           (NKF_KEY  | 0x40)
#define NK_F7           (NKF_KEY  | 0x41)    /* (hw) */
#define NK_F8           (NKF_KEY  | 0x42)    /* (hw) */
#define NK_F9           (NKF_KEY  | 0x43)
#define NK_F10          (NKF_KEY  | 0x44)
#define NK_CLRHOME      (NKF_KEY  | 0x47)    /* (hw) */
#define NK_UP           (NKF_KEY  | 0x48)    /* (hw) */
#define NK_M_PGUP       (NKF_KEY  | 0x49)    /* (hw) keypad 9 */
#define NK_LEFT         (NKF_KEY  | 0x4B)    /* (hw) */
#define NK_RIGHT        (NKF_KEY  | 0x4D)    /* (hw) */
#define NK_M_END        (NKF_KEY  | 0x4F)    /* (hw) keypad 1 */
#define NK_DOWN         (NKF_KEY  | 0x50)    /* (hw) */
#define NK_M_PGDOWN     (NKF_KEY  | 0x51)    /* (hw) keypad 3 */
#define NK_INS          (NKF_KEY  | 0x52)    /* (hw) */
#define NK_DEL          (NKF_KEY  | 0x53)
#define NK_UNDO         (NKF_KEY  | 0x61)    /* (hw) */
#define NK_HELP         (NKF_KEY  | 0x62)    /* (hw) */
#define NK_ENTER        (NKF_KEY  | 0x72)    /* keypad Return */

/* ---- the modifiers, above the low byte ------------------------------
 *
 * Ours.  HighWire only ever masks these off; nothing outside this file
 * and the conversion that produces them depends on the values.
 */

#define NKF_SHIFT       0x0100
#define NKF_CTRL        0x0200
#define NKF_ALT         0x0400
#define NKF_CAPS        0x1000
#define NKF_NUM         0x2000  /* pressed on the keypad */
#define NKF_RESVD       0x8000

#define NKF_MODIFIERS   (NKF_SHIFT | NKF_CTRL | NKF_ALT | NKF_FUNC \
                         | NKF_CAPS | NKF_NUM | NKF_RESVD)

/* The key with every modifier taken off. */
#define NK_KEY(nkey)    ((nkey) & 0x00ff)

/* ---- what HighWire calls --------------------------------------------
 *
 * One function, once, at start-up.  Returning 0 for "nothing to set up"
 * is honest here: there is no keyboard table to inspect on this machine
 * and no AES version to work around.  The conversion itself belongs with
 * the event loop and is not written yet.
 */
short nkc_init(void);

/*
 * The conversion itself: an AES key code and its shift state in, one
 * normalised code out.  HighWire calls this for every keypress (its
 * Form.c), so it is the one piece of nkcc that has to be written rather
 * than stubbed.
 */
short gem_to_norm(short kstate, short key);

#ifdef __cplusplus
}
#endif

#endif /* GEMBEDDED_NKCC_H */
