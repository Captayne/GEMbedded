/*
 * cflib.h - what HighWire actually takes from the C Form Library
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * cflib is a large convenience library for GEM dialogs.  HighWire
 * includes its header widely but uses almost nothing from it: what looks
 * like cflib at first reading -- form_buttn, form_radio, form_check,
 * form_text, file_selector -- turns out to be HighWire's own, declared in
 * its Form.h and defined in Form.c and AEI.c.  Those are HTML forms, not
 * GEM ones, and the names only collide.
 *
 * What is genuinely needed through this header is the normalised key
 * codes, which on the Atari arrive with cflib.
 *
 * So this is not a port of cflib.  It is the part of its surface that
 * HighWire touches, and it will grow only when the compiler says it must.
 */

#ifndef GEMBEDDED_CFLIB_H
#define GEMBEDDED_CFLIB_H

/* the original pulls GEM in with it, and sources that include only
   this one rely on that (HighWire's clipbrd.c, for instance) */
#include <gem.h>
#include <nkcc.h>

#endif /* GEMBEDDED_CFLIB_H */
