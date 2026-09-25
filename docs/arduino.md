# GEM programs from the Arduino IDE

Write a GEM program, press Upload, watch it run on the machine. No
installation anywhere: everything lives inside the GEMbedded folder, and
copying that folder takes the whole development environment with it.

What you get is **not** a simplified Arduino wrapper. A sketch here is an
ordinary GEM program with its own `main()`, `appl_init()` and its own
`evnt_multi()` loop, and every AES and VDI call is available. What the
Arduino IDE adds is the editor, the compiler and the Upload button --
plus, through `irk.h`, the second half of a program: tasks on the
real-time core.

## One-time setup

**1. Fetch the compiler.** It is the one piece too big for the
repository, so it is fetched once into `arduino/toolchain`:

    python tools/get-toolchain.py

About 300 MB to download. Afterwards the folder is self-contained.

**2. Install the Arduino IDE.** Two ways:

- **Fully portable (IDE 1.8.x):** unpack the IDE into `arduino/ide` and
  create an empty folder named `portable` inside it. The IDE then keeps
  its settings, boards and libraries there instead of in your user
  profile ([Arduino: portable
  installation](https://www.arduino.cc/en/Guide/PortableIDE)). Nothing
  outside GEMbedded is touched.
- **IDE 2.x:** portable mode [does not
  exist](https://github.com/arduino/arduino-ide/issues/122) there. Install
  it normally; only its own settings live in your user profile.

**3. Point the IDE at our sketchbook.** File → Preferences → Sketchbook
location:

    <...>\GEMbedded\arduino\sketchbook

That folder holds the board definition (`hardware/gembedded/rp2350`), the
GEM bindings and the example sketches. Restart the IDE.

**4. Let "New Sketch" start from a GEM program.** The IDE makes up the
`setup()`/`loop()` skeleton itself, but since 2.0.1 it will take a file of
yours instead. Press Ctrl+Shift+P, choose *Preferences: Open Settings
(UI)*, search for `arduino.sketch.inoBlueprint` and enter

    <...>\GEMbeddedrduino\sketchbook\default\default.ino

Restart the IDE. File → New Sketch then starts from a whole GEM program:
a window that can be moved, sized and closed, drawn through the rectangle
list. The same thing is under File → Examples → GEM → Window.

**5. Select the board.** Tools → Board → GEMbedded → *GEMbedded (RP2350,
320x240)*, and Tools → Port → the port of the machine (COM20 on this
bench; the uploader finds it by itself if you leave it alone).

## Every day

1. On the machine, start **DEPLOY**. It waits for programs and does not
   end, so you can upload again and again.
2. In the IDE, open a sketch, press **Upload**.
3. The program is written to `F:\` and started.

The uploader is `tools/gemdeploy.py`. It needs nothing but Python and
finds the machine by its USB identification:

    python tools/gemdeploy.py --list
    python tools/gemdeploy.py MYPROG.PRG            # send and run
    python tools/gemdeploy.py MYPROG.RSC MYPROG.PRG # resource first

## What a sketch looks like

GEM programs are written in C, and the Arduino IDE compiles every `.c`
file of a sketch folder as C -- while the `.ino` itself is compiled as
C++. So:

- put the program in a **`.c` file** next to the `.ino`, and
- leave the `.ino` as a note, or as the part you like writing in C++.

The board brings **no** `main()` of its own, so yours is the one that
runs. `arduino/sketchbook/GEMbedClk` shows the whole arrangement: a
window that can be moved, sized and closed, a dial drawn through the
rectangle list, and two cyclic tasks on the real-time core.

```c
#include "gem.h"                /* AES and VDI */
#include "irk.h"                /* the other core */

int main(void)
{
    short msg[8];
    short apid = appl_init();
    short win = wind_create(NAME|CLOSER|MOVER|SIZER, 0, 0, 320, 240);

    wind_open(win, 20, 20, 160, 160);
    for (;;) {
        short ev = evnt_multi_mesag_timer(50, msg);
        ...
    }
    appl_exit();
    return 0;
}
```

## Names, resources and sizes

- **File names on the machine are 8.3.** The uploader shortens a sketch
  name by itself: `GEMbedClk.ino.PRG` arrives as `GEMBEDCL.PRG`.
- **Resource files** (`.RSC`) are not made by the IDE. Draw them with an
  Atari resource editor under Hatari and put the `.RSC` and its header in
  the sketch folder; send the resource before the program, as above. Keep
  dialogs within 40 columns and 30 rows, which is what a 320x240 screen
  holds at 8x8 characters.
- **The display** is 320x240. `wind_get(0, WF_WORKXYWH, ...)` says what
  is left of it for windows.

## What is where

    GEMbedded\
      arduino\
        toolchain\          the ARM compiler (fetched, not in the repo)
        ide\                the Arduino IDE, if you keep it here
        sketchbook\
          GEMbedClk\        an example sketch
          hardware\gembedded\rp2350\
            boards.txt      the board
            platform.txt    the compiler and upload recipes
            cores\gem\      the GEM bindings (gem.c/h, mathglue, accstart)
            lib\            libcmini.a and the TOS start-up object
      tools\
        gemdeploy.py        the uploader
        get-toolchain.py    fetches the compiler
      pTOS\ rtcore\         the operating system and the kernel
      docs\

## If something does not work

- **"no answer -- is DEPLOY running on the machine?"** DEPLOY has to be
  started on the machine before uploading.
- **The wrong port.** `python tools/gemdeploy.py --list` shows which one
  says `GEMbedded`.
- **The program starts but the window is off-screen.** The desktop is
  320x240; ask the AES for the work area rather than assuming a size.
- **`undefined reference to main`.** The sketch has no `main()` -- this
  board brings none of its own on purpose.
