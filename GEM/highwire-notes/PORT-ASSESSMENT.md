# HighWire on GEMbedded — what the source actually says

Assessed 2026-09-27 against
[`neilrackett/atarist-highwire`](https://github.com/neilrackett/atarist-highwire)
(C, last pushed 2026-09-14), cloned to `GEM/highwire/`.

**Superseded in part — read [README.md](README.md) first.** This file is
the assessment as it stood on 27 September 2026, when the question was
still whether HighWire could be ported at all. It is kept because the
reasoning and the wrong turns are worth having, not because every
sentence is still true. What has happened since:

* **It runs, and it browses.** Verified on 10 October 2026: starts, renders
  local pages, and fetches `http://example.com` over the Wi-Fi module
  through pTOS' `STiK` cookie.
* **It is public.** This directory and `GEM/highwire/` are in git; the
  line that used to stand here said they were not.
* **Upstream moved.** HighWire 0.4.6 to 0.4.8 were merged on 10 October
  2026: GPL v3, a reorganised tree (`docs/`, `rsc/`, `build/`, vendored
  image libraries in `lib/`), transparent PNGs, faster image decoding,
  and STinG and STiK2 built from source rather than shipped as
  prebuilt overlays.
* **The network no longer lives in the browser.** `net/espnet.c` is not
  built any more; the AT transport grew up and moved into the operating
  system as `rp2350_stik.c`, and HighWire reaches it through its own
  unmodified `src/inet.c`.

## The three things that looked fatal, and were not

**The source is public.** The landing page at `highwire-browser.github.io`
says *"The browser source is not yet public"* — that refers to the 0.5.0
engine. The project moved to `neilrackett/atarist-highwire`, which has
`src/`, `modules/`, a Makefile and 299 files.

**Vector fonts are no longer required.** The 0.5.0 README says HighWire
*"will refuse to start"* without SpeedoGDOS, NVDI 3 or fVDI. The current
source does not. `src/HighWire.c:160`:

> HighWire prefers a Speedo fonts GDOS ... Where there is not — a plain ST
> or Mega ST(E) running only the ROM VDI — we fall back to the system font
> instead of refusing to start. See romvdi.c.

`src/romvdi.c` is that fallback: it remembers `vst_map_mode()` itself,
narrows 16-bit text to 8-bit for the ROM VDI, and decides from the cookie
whether a real GDOS is there. Somebody has already removed the obstacle we
were most afraid of.

**m68k is shallow.** Three assembly files in the whole tree, and two of
them belong to network modules we will not use:

| | |
| --- | --- |
| `src/rastr_pc.s` | Pure C compiler path, not ours |
| `modules/network.src/socklib/crt0.s` | not ours |
| `modules/network.src/iconnect/set_flag.s` | not ours |

Inline assembly appears in exactly one file, `src/raster.c`: five blocks
guarded by `#if defined(__GNUC__) && !defined(__mcoldfire__)`. Three have a
portable `#else`; two do not. On ARM the guard is true, so it takes the
m68k branch and fails — the guard has to become architecture-specific and
two C routines have to be written. Image scaling and dithering; a day.

Endianness is *easier* for us than for m68k: the three mbedtls big-endian
bugs their fork patches do not apply to ARM.

## The GEM gap, counted

106 real GEM calls in the source, of which pTOS has 68. The 38 that are
missing are not 38 pieces of work:

**13 are gemlib conveniences.** `wind_get_grect`, `wind_open_grect`,
`wind_set_grect`, `wind_calc_grect`, `wind_create_grect`, `wind_set_str`,
`vs_clip_off`, `vs_clip_pxy`, `evnt_multi_fast` and friends are a `GRECT`
where we take four `short`s. A header of inline wrappers.

**15 are GDOS calls that live only in `romvdi.c`** — `v_ftext`,
`v_ftext16`, `v_ftext16n`, `vqt_advance`, `vqt_f_extent16n`, `vqt_width`,
`vqt_xfntinfo`, `vst_arbpt`, `vst_kern`, `vst_load_fonts`,
`vst_map_mode`, `vst_scratch`, `vst_setsize32`, `vst_unload_fonts`,
`vq_scrninfo`. Runtime never reaches them, because `has_fsm_gdos` stays
false. They have to **link**, not work. Stubs.

**5 are real and absent from pTOS entirely:**

    appl_getinfo   rsrc_obfix   vq_extnd   vsf_udpat   vq_vgdos

`vq_vgdos` is a one-liner ("no GDOS"). `appl_getinfo` answers capability
questions, `rsrc_obfix` converts character to pixel coordinates, the other
two are small VDI additions.

**2 exist in pTOS but not in the GEMduino bindings** — `vr_trnfm` and
`vsl_udsty`. Passing through, not writing.

## The network layer is the easy part

HighWire loads `network.ovl` at run time. `modules/` holds `stik2`,
`sting`, `mintnet`, `magicnet`, `iconnect` — five stacks behind one
interface — and `modules/example.src/sample.c` is a working template. A
module exports four functions:

```c
long  __CDECL ovl_init(void);
struct ovl_info_t * __CDECL ovl_version(void);
void *__CDECL ovl_getftab(void);     /* the function table */
long  __CDECL ovl_free(void);
```

So there is no need to impersonate STiK at the cookie. We write a sixth
module and map it onto `AT+CIPSTART` / `AT+CIPSEND`, exactly the shape of
`_IRK`, `_RTX`, `_UDR` and `_WIF` — a function table behind one entry
point.

TLS is the open question. The browser does it itself with Mbed TLS and a
`cacert.pem`; the alternative is to let the radio module do it
(`AT+CIPSSLCSTART`) and hand the browser a socket that is already
encrypted. The ESP-01 here runs NonOS AT 1.7.5, whose SSL buffer is tight
(`AT+CIPSSLSIZE`) — whether a current certificate chain fits has to be
measured, not assumed.

## What is still unknown

Not GEM any more. The two real unknowns are **how much MiNT library**
HighWire expects — `gemlib`, `cflib`, and what `libcmini` lacks against
`mintlib` — and whether **`highwire.rsc`** agrees with our resource
loader.

A compile attempt answers both faster than any further counting.

---

# The compile attempt, 2026-09-27

Every `src/*.c` handed to our own ARM toolchain
(`GEMduino/toolchain/bin/arm-none-eabi-gcc`), `-fsyntax-only`, with the
Makefile's own `-DUSE_OVL -DUSE_INET -DLIBPNG -DLIBGIF -DLIBJPG`, against
libcmini and the GEMduino bindings. No edits to HighWire.

**20 of 57 files compile unchanged.**

Adding `-I pTOS/lib/libcmini/include/mint` is what took it from 18 to 20:
HighWire includes `<osbind.h>` and `<mintbind.h>`, and libcmini has both,
one directory deeper.

What the other 37 fail on:

| | files | |
| --- | --- | --- |
| `gemx.h` missing | 11 | gemlib's extended header |
| `cflib.h` missing | 6 | Atari C Form Library |
| `gif_lib.h`, `png.h`, `jpeglib.h` missing | 4 | ordinary portable libraries |
| names undeclared | 16 | almost all of them from `gemx.h` |

The undeclared names, counted:

    LOG_LWHITE  LOG_BLACK  LOG_LBLACK  ALL_BLACK     colour constants
    BOOL  TA_CENTER  EVMULT_IN  S_WRITE              types and constants
    wind_get_grect  wind_calc_grect                  the GRECT wrappers
    IMGINFO  location_wrIdx  __free                  knock-on, project's own

**Not one failure is about the machine being ARM.** No endianness, no
m68k, no compiler disagreement. That was the question the attempt was
meant to answer.

## Next

Write a `gemx.h` for GEMbedded: types, colour constants, the thirteen
`_grect` wrappers over bindings we already have, the five real gaps
(`appl_getinfo`, `rsrc_obfix`, `vq_extnd`, `vsf_udpat`, `vq_vgdos`), and
link-only stubs for the fifteen GDOS calls that `romvdi.c` never reaches.
One header and a small `.c`.

After that: `cflib`, and building zlib, libpng, libjpeg and libgif for
ARM — which is compiling portable C, not writing it.

---

# A gemx/gem compat header, first pass

`gem-compat/gem.h` — 430 lines. **26 of 56 files compile**, from 20.

(`src/img_ico.c` is excluded from the count: it has no includes at all and
is meant to be pulled into another file, not compiled on its own.)

## What the header has to be

gemlib's umbrella header is **`<gem.h>`**, not `<gemx.h>` — HighWire
includes the former. So the compat directory answers `<gem.h>` first,
hands the job back to the board package's own bindings with
`#include_next`, and adds the difference. `gemx.h` is then one line.

Three mistakes on the way, each of which made things worse before better:

**Re-declaring what the bindings already declare.** `wind_get`, `vs_clip`,
`evnt_multi` and friends came in through `#include_next`; declaring them
again broke four files that had compiled *without* the header. pTOS takes
an `unsigned long` for `evnt_multi`'s timer where gemlib takes two
shorts, and both prototypes cannot be right. 20 -> 16.

**Declaring the base types.** `WORD`, `BOOL`, `CHAR` and the rest must
*not* be here: HighWire's `src/hw-types.h` `#undef`s and declares them
itself, `BOOL` as an enum where we had a short. `GRECT` is shared instead
through the guard hw-types.h already uses — same struct tag, same
`__GRECT` — so whichever header is read first wins.

**Guarding constants by the block.** `#ifndef FULLER` around a block that
also held `LFARROW`, `HSLIDE` and `INFO` meant the board package's own
`FULLER` skipped all of them. Every constant now carries its own guard.

## Where it stands

| | |
| --- | --- |
| compile | 26 of 56 |
| still failing | 25 files, 87 distinct names |
| missing headers | `png.h`, `jpeglib.h`, `gif_lib.h` (4 files) |

The remaining names are the same kind of thing: constants and the smaller
AES calls. It is a long tail, not a wall.

## A third external library

`nkcc` — Normalized Key Codes — is **not in the tree** and not in the
board package either. `NK_TAB`, `NK_LEFT`, `NKF_SHIFT`, `nkc_init()` come
from it. So the outside dependencies are:

    gemlib    replaced by gem-compat/, in progress
    cflib     stubbed, not written
    nkcc      not started
    zlib libpng libjpeg libgif    portable C, to be built for ARM
    mbedtls   or hand TLS to the radio module instead

---

# Overnight, 2026-09-27 into the 28th

**42 of the 43 files the Makefile builds now compile for ARM.**

The one that does not is `img-dcdr.c`, and it fails on `gif_lib.h`,
`png.h` and `jpeglib.h` -- three ordinary portable libraries that have to
be built, not written. Every other obstacle is gone.

Counting note: `src/` holds 57 `.c` files but the Makefile builds 43.
`img_gif.c`, `img_jpg.c`, `img_png.c`, `img_ico.c` and `img_xmp.c` are
fragments `#include`d by `img-dcdr.c`, and `mem-diag.c` is commented out
of the build. Measuring all 57 separately, as the earlier runs did, was
counting files nobody compiles.

## What it took, in order

| | compiles |
| --- | --- |
| untouched | 20 |
| the compat `gem.h` (types, GRECT, wrappers) | 21 |
| GEM constants, each individually guarded | 26 |
| `-fno-short-enums` | 29 |
| EVMULT structures, `v_ftext` shapes, `vst_point` | 34 |
| `cflib.h` pulling in GEM, `vrt_cpyfm`'s colour pair | 36 |
| NK codes carrying NKF_FUNC | 39 |
| **`ob_spec` as a union** | 40 |
| `BFOBSPEC`, `objc_change`'s reserved argument | 44 |
| forwarding headers instead of the whole `mint/` directory | 44 |
| undoing libcmini's `st_atime` macro | **50 of 55 / 42 of 43** |

## Three findings worth keeping

**`-fno-short-enums`.** The ARM ABI packs an enum into the smallest type
that fits. `parser.c` declares `HTMLKEY Key :16`, a bitfield as wide as
the enum is on m68k, and on ARM the enum had become one byte. The flag
has to be on for the whole build or the ABI disagrees with itself.

**The NK codes are not scancodes.** `keyinput.c` has `case NK_RIGHT` next
to `case 'M'`, and 'M' is 0x4D, which is also the right arrow's scancode.
A normalised code carries NKF_FUNC as well, and keyinput.c:87 masks off
every modifier bit except that one. Defining NK_RIGHT as 0x4D produced
five duplicate-case errors and no clue why.

**libcmini contradicts itself over `st_atime`.** `mint/mintbind.h` makes
it a macro for a nanosecond timestamp; `sys/stat.h` and `ext.h` declare it
as an ordinary member. Whichever is read second is rewritten by the macro,
and the compiler reports a syntax error *inside the #define*, which points
nowhere near the cause. HighWire includes both through `file_sys.h`. Three
`#undef`s in our forwarding header fixed six files at once.

## A change to the board package, for approval

`GEMduino/.../cores/gem/gem.h` had `long ob_spec`. **pTOS itself declares
`OBSPEC ob_spec`** (`include/obdefs.h:280`) -- so the binding and the
operating system described the same four bytes differently, and any
program that builds a dialogue had to cast around it. It is now the union,
with `BFOBSPEC` for the bitfield form.

That alone took HighWire from 39 to 44 files: `dl_mngr.c` fell from 49
errors to 5, `Widget.c` from 19 to 4, `fntsetup.c` from 16 to 1.

The fallout in our own code is **one line**, `GEM/examples/deploy/deploy.c`,
now `o->ob_spec.index = spec`. DEPLOY, CLOCK, TOUCHCAL and GEMtest all
rebuild. It is uncommitted: it changes a published header, and that is
the user's call.

## Also noticed

`GEMduino/sketchbook/Fractals/Makefile` still names `fractals.c`, which
is gone since the sketch became a single `.ino`. The IDE builds it; the
Makefile does not. Not caused by any of this.

## Next

1. Build zlib, libpng, libjpeg and libgif for ARM against libcmini. That
   is the last thing between here and a whole object set.
2. Then link, which will produce a different and longer list: every stub
   in `gem-compat` has to become a function.
3. `gemlib_v_opnvwk`, `gemlib_vst_font`, `gemlib_vrt_cpyfm`,
   `gemlib_vst_point`, `gemlib_objc_change`, `evnt_multi_fast`,
   `rc_intersect`, `appl_getinfo`, `rsrc_obfix`, `vq_extnd`, `vsf_udpat`,
   `vsl_udsty`, `vr_trnfm`, `scrp_read`, `nkc_init`, `gem_to_norm` --
   sixteen functions, none of them large.
4. The values marked UNVERIFIED in `gem-compat/gem.h` have to be checked
   against a real gemlib header before any of this runs.

---

# 2026-09-28: it links

    HIGHWIRE.PRG  --  729,780 bytes
       text     data      bss
     493,792    5,476   14,696

**All 51 source files compile and the whole browser links for ARM, with
no undefined symbols.** Engine, parser, renderer, scheduler, layout, and
PNG, JPEG and GIF decoding.

It does not run yet -- there is no network module, no TLS, and the
Speedo stubs are stubs. But the question this exercise asked was whether
the port is feasible, and the answer is on disk.

## The libraries

Built from the released sources, no configure run, compiled directly with
HighWire's own flags. `GEM/highwire-libs/build.sh` does it in one go.

| | | |
| --- | --- | --- |
| zlib 1.3.1 | 70 KB | 11 objects, `-DZ_SOLO` |
| libpng 1.6.43 | 272 KB | 15 objects |
| libjpeg 9f | 248 KB | 46 objects |
| giflib 5.2.2 | 36 KB | 7 objects |

Three headers had to be written for them, all for the same reason: a
library that includes a header libcmini does not have gets newlib's, and
newlib's type headers disagree with libcmini's about what `__int32_t` is
(`int` here, `long int` there -- both 32 bits and still different types).
The compilation then dies inside the C library, far from anything the
library being built ever wrote.

    include/math.h     declarations only; the functions come from libm
    include/stdint.h   the fixed-width names, following libcmini
    include/fcntl.h    the open() flags, TOS values

Two build-flag findings:

**`-DZ_SOLO` excludes zlib's `gz*` family**, which is the part that needs
stdio -- so those files must not be in the file list at all. libpng uses
the raw deflate interface and never asks for a `gzFile`.

**libjpeg needs `-DHAVE_BOOLEAN -Dboolean=int -DFALSE=0 -DTRUE=1`**, in
the library and in the application both. jmorecfg.h avoids its
`typedef enum { FALSE, TRUE } boolean` only when FALSE or TRUE is already
a macro -- and by the time img-dcdr.c reaches jpeglib.h, its own
img_gif.c has just `#undef`'d both, so the enum collides with the FALSE
and TRUE that hw-types.h declares.

## The compat layer, finished

`gem-compat/gemx.c`, 430 lines, holds three kinds of thing and says which
is which:

- **real calls** -- `appl_getinfo`, `rsrc_obfix`, `vq_extnd`,
  `vsf_udpat`, `vsl_udsty`, `vr_trnfm`, `v_opnwk`, `rc_intersect`,
  `evnt_multi_fast`, `gem_to_norm`
- **shape adapters** -- `v_opnvwk`, `vst_font`, `vst_point`, `vrt_cpyfm`,
  `objc_change`, where gemlib and the board package disagree about a
  signature
- **stubs** -- the fifteen Speedo/GDOS calls, reached only behind
  `if (has_fsm_gdos)`, which is false here

`raster.c` needed one change in HighWire itself: seven `#if
defined(__GNUC__) && !defined(__mcoldfire__)` guards around m68k inline
assembly, which on ARM took the assembly branch. Now they also ask for
`__mc68000__`, and the portable C that was already there is used instead.
That is a patch worth sending upstream: it is what "builds on non-m68k
GCC" costs.

## What is left before it runs

1. A network module: four functions (`ovl_init`, `ovl_version`,
   `ovl_getftab`, `ovl_free`) mapping onto `AT+CIPSTART` / `AT+CIPSEND`.
2. TLS: mbed TLS on the RP2350, or `AT+CIPSSLCSTART` on the module.
3. The UNVERIFIED constants in `gem-compat/gem.h` checked against a real
   gemlib header.
4. `gl_ap_version` from the AES global block rather than the conservative
   constant it is now.
5. `highwire.rsc` through our resource loader.

---

# 2026-09-28: it runs

HighWire starts, draws its menu bar, opens a window and stays up. The
crash that stopped it was two mistakes in the compat layer, both the same
mistake: a 32-bit pointer passed to the AES and the VDI the way the Atari
documentation describes it, on a machine whose bytes go the other way.

## How it was found

Not by reading the code. The first fault report said this:

    pc=1004f03e lr=1005e371 fsr=00008200 far=91f0110a

and every address in it was wrong. `pc` pointed at a `nop`, which cannot
take a bus fault; `lr` pointed into the middle of a four-byte
instruction. Three stack words looked like `gr_crack`, `gr_gicon` and
`just_draw`, and an hour went into that -- the resource was decoded
object by object, the AES stack was measured, the OBJECT layout was
compared field by field against `obdefs.h`. All of it clean, and none of
it the point.

The report was wrong for two reasons, both in the reporter:

1. **`vectorsasm.S` never cleared CFSR or HFSR.** They are
   write-1-to-clear and nothing else touched them, so the bits and BFAR
   accumulated: the second report carried the first fault's cause and the
   first fault's address. Two reports agreeing to the bit while their
   `pc` disagreed was the tell.

2. **There was no way to tell a return address from a coordinate.**
   pTOS is built without frame pointers, so a report showed one frame and
   a raw stack window, and reading symbols out of that window is
   guesswork. `1005e42d` resolved to `gr_gicon+0x8d` and was not a return
   address at all -- the halfword before it is `movs r2, #7`.

`earlyfault.c` now scans the stack for words that are odd, land in the
TEXT segment, and have the tail of a `BL` or `BLX` immediately before
them. Checking for the call is what makes it worth printing: without it
every stale pointer in range is offered as a caller. The same heuristic,
applied by hand to the three addresses above, rejects all three.

The next report gave the chain in one shot:

    trapaes -> super -> wm_open -> draw_change -> w_update -> do_walk
      -> ob_draw -> everyobj -> just_draw -> gr_gtext -> gr_just
      -> expand_string          <- fault, r1 = 91f0110a

`expand_string`'s second argument is the string. So a text object in the
window tree had a `te_ptext` of `0x91f0110a`, and `0x91f0110a` with its
halves swapped is `0x110a91f0` -- a perfectly ordinary address.

## The two bugs

**`wind_set_str` (gem-compat/gem.h).** The window title and info line go
through the AES by address, in two of the `int_in` words, and
`gemwmlib.c` reads them back with `*(char **)pinwds`. Which half goes
first is therefore not a convention but a question of memory order, and
on a little-endian machine the low half belongs at the lower word. The
compat layer put the high half first, as the Atari documentation
describes, and its comment even said "high first".

**`vr_trnfm` (gem-compat/gemx.c).** The two MFDBs go into pTOS's
`VDICONTROL` (include/vdipb.h), which on ARM has a pad word and then two
*native* pointers, at words 8 and 10 -- not halves at 7/8 and 9/10. The
compat layer split them into halves and wrote them one word low. That is
why the splash drew as a black square: the transform read its bitmap
from an address that was never its own.

The board package had both right, and says so in a comment next to
`wind_set_name`. The compat layer was written against the Atari
documentation instead of against the port sitting next to it.

## The build

`build.sh` now does it, where before it was a shell command that was not
written down. 51 HighWire sources, plus `gem-compat/gemx.c`,
`net/dummynet.c` in place of `src/ovl_sys.c`, the board package's `gem.c`
and `mathglue.c`, and `hwstart.c`.

Two things the reconstruction turned up:

- **`appl_xgetinfo` was never declared.** HighWire defines its own in
  `AEI.c` and calls it from two other files without including a header
  for it; on the Atari, gemlib's `gem.h` declares it. GCC 14 treats an
  implicit declaration as an error, so this only surfaced now. The
  declaration belongs in the compat header, and is there.

- **The stack was 4 KB.** Linking with the board package's `accstart.S`
  gave HighWire the 4 KB BSS stack that startup keeps for a desk
  accessory. A browser recurses once per nested element. It now links
  with libcmini's `crt0.o` and `hwstart.c` sets `_stksize` to the 128 KB
  HighWire's own Makefile asks the Atari linker for.

    HIGHWIRE.PRG  --  632,844 bytes
       text     data      bss
     417,228    5,572   14,404

## What is left

The network module is still `dummynet.c`, so every address answers the
same canned page. TLS is untouched. Items 3 and 4 of the previous list --
the UNVERIFIED constants and `gl_ap_version` -- still stand, and after
this they are worth more than they looked: the compat layer's mistakes
are all of one kind, and those are the two places left where it guesses.

---

# 2026-09-28: it renders

A local page, `C:\html\index.htm`, laid out correctly: proportional text
with real line spacing, heading sizes, a bordered table with aligned
columns, `<pre>` holding its fixed width, working scroll bars.

## The one bug

`vqt_fontinfo` was a stub in `gem-compat/gemx.c` that returned zeros.

It should never have been one. The stubs in that file are the Speedo and
GDOS calls, reached only behind `if (has_fsm_gdos)`, which is false here.
`vqt_fontinfo` is base VDI, opcode 131, and pTOS implements it
(`vdi/vdi_text.c`'s `vdi_vqt_fontinfo`). It got swept in with the others.

`fontbase.c` takes the ascent from `distances[4]` and the descent from
`distances[0]`. With both zero its automatic line spacing came out as
`(0 + 0 + 4) / 8 + 1`, so every line advanced the next by one pixel and
the whole document piled into a single black band.

What made it findable was that **the table still looked right**: its
cells are placed by the table code, not by font metrics. Everything that
asked the VDI how tall the font was collapsed; everything that did not,
did not. That narrowed it to one call.

The VDI returns the five distances in the y halves of `ptsout` and the
width and three style effects in the x halves, which is what the real
implementation now reads.

## Two tools that were lying

Both cost more time than the bug did, and for the same reason: they
reported something plausible instead of nothing.

**`mon_dump_fb()`** sampled 2x2 pixel blocks, so a 240-line screen came
out as 120 lines. The system font is eight pixels tall, which became
four, and every line of text looked as though it overlapped the next. A
screenshot of a correct render and a screenshot of a broken one were hard
to tell apart, and one reading off it -- "bold switches to a huge font"
-- was simply wrong: it was an `<h2>`. It now prints one character per
pixel. That is 77 KB and about seven seconds on the wire.

**`fbshot.ps1`** was still looking for the 1 bpp hex format the monitor
stopped printing when the display went to RGB565, and said "only 0 of 240
lines received" for every screen it was pointed at. It is now
`tools/fbshot.py`: it reads the width out of the dump rather than
assuming one, takes the most common line length rather than the longest
(a line that loses its newline runs into the next and would otherwise set
it), and writes the PNG itself, since there is no Pillow here.

## Also fixed on the way

- **`AES_VERSION` was 0x0140**, which compiles `WF_OWNER` out of
  `wind_get` entirely (`gemwmlib.c`, `#if AES_VERSION >= 0x330`). It
  returned FALSE and HighWire's `dragdrop.c` reported "Owner not found"
  for every click on the URL field. Now 0x0340. In the whole tree that
  changes exactly two things: `WF_OWNER` exists, and `NUM_WIN` goes from
  8 to 16.

- **The deploy accessory held the USB console for good.** It took raw
  mode at start-up and never gave it back, so on a machine whose only
  keyboard is that port there was no way to type from the moment it
  loaded. The console now watches for the five bytes every transfer opens
  with (`rp2350_usbcon.c`, `rx_drain()`) and switches itself; the bytes
  go into the ring afterwards, so `poll_console()` still finds its
  header. `deploy.c` gives the port back after each transfer.

## What is left

Still `dummynet.c` for the network, so `http://` goes nowhere -- a local
file is the whole of what works. Still no TLS. And the machine wants a
keyboard: the serial line is one (`push_ascii_ikbdiorec()`), but only for
ASCII, so there are no cursor keys. Mapping ANSI escape sequences onto
Atari scancodes in `ikbdiorec_from_ascii()` would make any terminal a
usable keyboard until real hardware arrives.
