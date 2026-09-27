# GEMtest — native GEM from the Arduino IDE

GEMtest is an interactive test of the AES and VDI bindings on **GEMbedded**, an
Atari TOS successor based on pTOS. The current target is a Waveshare RP2350B
PiZero with a 320×240 colour touch display. Write the application in the
Arduino IDE, press Upload, and a GEM window opens on the running desktop.

**AES** provides windows, events and dialogs; **VDI** provides graphics, text
and raster operations. GEMtest calls these APIs directly. Seven pages combine
numerical checks, drawings and interactive dialogs to exercise the bindings
that pass arguments and results between the application and operating system.

![GEMtest on GEMbedded, showing the graphics primitives](../../../docs/images/gemtest.png)

## From sketch to application

```text
Arduino IDE → ARM compiler + GEM bindings + libcmini → .PRG (ARM ELF)
            → USB upload → DEPLOY on GEMbedded → running GEM application
```

The board package builds native Cortex-M33 code. Its `.PRG` is an ELF
executable loaded by pTOS. Uploading installs and starts the application;
the operating system stays running, without reflashing the board.

The program lives in [gemtest.c](gemtest.c), with its own `main()`.
[GEMtest.ino](GEMtest.ino) is a comment-only file for opening the sketch in the
IDE. The IDE compiles the `.c` file as C. There is no `setup()` or `loop()` in
this example. The board's [GEM bindings](../hardware/gembedded/rp2350/cores/gem/gem.h)
provide calls such as `appl_init()`, `wind_create()` and `v_circle()`.

## Where IRKernel fits

IRKernel schedules the AES processes and the user program on core 0.
GEMtest participates through normal AES event handling: its
`evnt_multi_button_timer(1000, ...)` waits for a button event, a window
message or a one-second timeout. The timer does not advance pages.

In the [AES dispatcher](https://github.com/Captayne/pTOS/blob/gembedded/aes/gemdisp.c), waiting processes block
through `k_block()`, runnable processes yield through `k_yield()`, and events
make processes runnable through `k_wake()`. IRKernel decides what runs next.
Scheduling is cooperative: a long computation without yielding or blocking
can delay other work on that core.

GEMtest creates no additional IRKernel tasks and does not use core 1. It
shows a GEM application participating in the scheduled system. For examples
that also create tasks on the second core, see [GEMbedClk](../GEMbedClk/)
and [Fractals](../Fractals/).

## Build, upload and use

Follow the [Arduino setup guide](../../../docs/gemduino.md) once to configure
the toolchain, sketchbook and board package. Then:

1. Open `GEMtest.ino`, keeping `gemtest.c` in the same folder.
2. Select **GEMbedded (RP2350, 320x240)** and the machine's USB serial port.
3. Start **DEPLOY** on GEMbedded (**Desk → Deploy**).
4. Press **Upload**. The program is transferred to `C:\` and started.
5. Read page 1, then tap or click inside the window to advance. After page 7,
   the next tap returns to page 1.
6. Close the window to exit. It can also be moved and resized.

Entering page 6 opens its dialog immediately; entering page 7 opens the file
selector. Dismiss either one to see its result, then tap the work area again
to continue. No external `.RSC` resource file is required.

## The seven pages

Screen pages are numbered 1–7; the source variable `page` runs from 0–6.

| Page | Calls exercised | What to look for |
| --- | --- | --- |
| 1 · Checks | `vqt_extent`, `vs_color`, `vq_color`, `v_get_pixel`, `form_center`, `objc_offset`, `wind_calc` | Twelve rows of expected and actual values, each marked `ok` or `FAIL`. |
| 2 · Primitives | `v_bar`, `v_circle`, `v_ellipse`, `v_arc`, `v_pieslice`, `v_rbox`, `v_rfbox`, `v_fillarea` | Two rows: bar, circle, ellipse, half-circle arc; pie slice, rounded outline, filled rounded box, filled triangle. |
| 3 · Colour | `vs_color`, `vsf_color`, `vr_recfl` | Sixteen adjacent bands using pens 16–31, progressing from blue towards red while green increases. |
| 4 · Text | `vst_effects`, `vst_alignment`, `vqt_extent`, `v_gtext`, `v_pline` | Plain, thickened, lightened, skewed, underlined, outlined and shadowed text with red extent boxes. |
| 5 · Raster | `vrt_cpyfm`, `vro_cpyfm` | Four coloured arrow masks above an opaque red arrow on green. Source images are 16×16 pixels. |
| 6 · Dialog | `form_center`, `form_dial`, `objc_draw`, `form_do` | A dialog built from a C `OBJECT` array. After dismissal, 2 means OK and 3 means Cancel. |
| 7 · File selector | `fsel_input` | Initially uses `C:\*.*`. Displays the button result (1 = OK, 0 = Cancel), path and filename. It does not open the selected file. |

### Reading the numerical checks

| Displayed check | Expected result |
| --- | --- |
| `vqt_extent` | Width of `ABCDEFGH` equals eight system-font character widths. |
| `vqt_height` | Extent height equals the system-font character height. This is a label for a `vqt_extent()` result, not a separate API call. |
| `vs_color r/g/b` | Pen 16 reads back as 1000, 500, 0 using `vq_color(..., VQ_REQUESTED, ...)`, with tolerance ±40 per component. |
| `v_get_pixel` | A probe in a freshly drawn red square returns pen index 2. The raw pixel value is not checked. |
| `form_ctr w/h` | Dialog dimensions are 200×80. |
| `objc_off 0/1` | Root x matches the centered dialog x; child x is 20 pixels further right. |
| `wind_calc x/w` | Converting a border rectangle to a work rectangle and back preserves x = 100 and width = 160. |

`run_checks()` stores results at startup and repeats them when the window is
resized. Returning to page 1 displays the stored results. The checks are kept
out of redraw: the pixel probe draws its own square, so repeating it for each
visible rectangle could clip away the pixel being tested.

The other pages require visual inspection or interaction and do not produce
automated pass/fail results. The raster page uses a one-plane mask and a
16-bit RGB565 device-format bitmap; its format assumptions belong to the
current display target.

## Reading the implementation

`main()` registers with AES using `appl_init()`, opens a virtual VDI workstation
and creates the window. It runs the checks, then enters the event loop.
Window messages request redraws, moving, resizing, bringing the window to the
top or closing it. On exit, the program closes and deletes its window, closes
the VDI workstation and calls `appl_exit()`.

`redraw()` walks the visible rectangle list with `WF_FIRSTXYWH` and
`WF_NEXTXYWH`, intersects each rectangle with the requested redraw area and
sets VDI clipping before calling `draw_page()`. This allows exposed portions
to be repainted when windows overlap. Drawing is bracketed by `wind_update()`
and temporarily hides the mouse cursor.

On the text page, `vqt_extent()` supplies a box anchored at the origin.
`page_text()` selects `TA_LEFT, TA_TOP` so adding the drawing position places
the extent box over the text. Afterwards it restores baseline alignment and
clears the text effects.

## Building without the IDE

The optional [Makefile](Makefile) uses `arm-none-eabi-gcc` from `PATH` and
expects libcmini build products in `pTOS/obj/libcmini-build`. The pTOS
`make test-hd` target produces them. With those prerequisites available,
run from this directory:

```sh
make
python ../../../tools/gemdeploy.py GEMTEST.PRG
```

DEPLOY must be listening for the upload. The Arduino build instead uses the
libraries shipped in the board package; it does not invoke this Makefile.

## If something looks wrong

- **Upload reports no answer:** check DEPLOY and the selected serial port.
  The setup guide describes port discovery.
- **A numerical row reports `FAIL`:** record expected and actual values,
  display mode and whether the window was resized. The result identifies an
  observation to investigate, not necessarily a faulty binding.
- **Shapes or text are cut off:** restore a sufficiently large window before
  judging the result. The example has no scrolling; fixed text rows may not
  fit a small work area.
- **Numbers pass but a drawing is wrong:** inspect the visual pages as well.
  These selected checks are not exhaustive AES/VDI or scheduler validation.

See the [sketchbook overview](../README.md) for the other examples and the
[GEMbedded overview](../../../README.md) for the complete system.
