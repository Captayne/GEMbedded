# GEMduino - GEM programs from the Arduino IDE

Write a GEM program, press Upload, watch it run on the machine. No
installation anywhere: everything lives inside the GEMbedded folder, and
copying that folder takes the whole development environment with it.

**GEMduino** is the name of that arrangement, and of the folder that holds
it: the board package, the compiler, the bindings and the example sketches.
Programs written any other way live in `GEM/` instead -- the two-halves
clock, DEPLOY, and anything built with a plain Makefile. The split is by
how a program is built, not by what it does.

What you get is **not** a simplified Arduino wrapper. A sketch here is an
ordinary GEM program with its own `main()`, `appl_init()` and its own
`evnt_multi()` loop, and every AES and VDI call is available. What the
Arduino IDE adds is the editor, the compiler and the Upload button --
plus, through `irk.h`, the second half of a program: tasks on the
real-time core.

## One-time setup

**1. Fetch the compiler.** It is the one piece too big for the
repository, so it is fetched once into `GEMduino/toolchain`:
    python tools/get-toolchain.py

About 300 MB to download. Afterwards the folder is self-contained.

**2. Install the Arduino IDE.** Two ways:

- **Fully portable (IDE 1.8.x):** unpack the IDE into `GEMduino/ide` and
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
    <...>\GEMbedded\GEMduino\sketchbook

That folder holds the board definition (`hardware/gembedded/rp2350`), the
GEM bindings and the example sketches. Restart the IDE.

**4. Let "New Sketch" start from a GEM program.** The IDE makes up the
`setup()`/`loop()` skeleton itself, but since 2.0.1 it will take a file of
yours instead. Press Ctrl+Shift+P, choose *Preferences: Open Settings
(UI)*, search for `arduino.sketch.inoBlueprint` and enter
    <...>\GEMbedded\GEMduino\sketchbook\default\default.ino

Restart the IDE. File → New Sketch then starts from a whole GEM program:
a window that can be moved, sized and closed, drawn through the rectangle
list. The same thing is under File → Examples → GEM → Window.

**5. Select the board.** Tools → Board → GEMbedded → *GEMbedded (RP2350,
320x240)*, and Tools → Port → the port of the machine (COM20 on this
bench; the uploader finds it by itself if you leave it alone).

## Why not the Boards Manager

The usual way into the Arduino IDE is a URL under *Additional Boards Manager
URLs*, and GEMduino does not have one yet. That is a packaging job, not a
missing feature: a Boards Manager entry is a versioned archive of the platform
plus a `package_gemduino_index.json` with its checksum, hosted somewhere
stable, and it pins a toolchain as a separate dependency. Until there is a
release worth pinning, pointing the IDE at the sketchbook does the same thing
and has one real advantage -- the board package you compile against is the one
in the repository, so a change to the bindings takes effect on the next Upload
instead of on the next release.

Note also that GEMduino is a *board package*, not a library: it brings its own
compiler recipes, its own start-up object and no `main()`. So it would never
belong in the Library Manager, only in the Boards Manager.

## Every day

1. On the machine, start **DEPLOY** from the DESK Menu. It waits for programs and does not end, so you can upload again and again. A Checkmark indicates the status. While the status is ticked, SD-Card sharing per USB-Port is not possible and vice versa (See Entry under Options).
2. In the IDE, open a sketch, press **Upload**.
3. The program is written to `C:\` -- the boot drive -- and started.

The uploader is `tools/gemdeploy.py`. It needs nothing but Python and
finds the machine by its USB identification:
    python tools/gemdeploy.py --list
    python tools/gemdeploy.py MYPROG.PRG            # send and run
    python tools/gemdeploy.py MYPROG.RSC MYPROG.PRG # resource first

## What a sketch looks like

The Arduino IDE compiles every `.c` file of a sketch folder as C, and the
`.ino` itself as C++. Both roads work here, and which you take is a
matter of taste rather than of rules:

- **The whole program in the `.ino`,** as C++. `Window`, `WifiTime`,
  `Fractals` and `Core0Latency` are written this way, and it is the road
  to take for a new sketch -- one file, and the library classes (`Esp`,
  `Uart`) are within reach. The board package links a wrapper that calls
  your `main()` and runs global constructors before it.
- **The program in a `.c` file** beside the `.ino`, with the `.ino` left
  as a note. `GEMbedClk` and `GEMtest` are still shaped that way, from
  before the C++ road worked, and they build unchanged.

The board brings **no** `main()` of its own, so yours is the one that
runs. `GEMduino/sketchbook/Window` is the shortest whole program;
`GEMduino/sketchbook/GEMbedClk` shows the whole arrangement: a window
that can be moved, sized and closed, a dial drawn through the rectangle
list, and two cyclic tasks on the real-time core.

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

## The libraries

**There is nothing to install.** Both libraries ship inside the board
package, under `hardware/gembedded/rp2350/libraries/`, so the IDE offers
them to any sketch built for this board. Nothing is fetched, nothing
goes in your user profile, and the Library Manager is not involved --
which also means they cannot go stale against the board you compile for.

They turn up in the usual two places: **Sketch -> Include Library**, and
**File -> Examples**.

|              |                         |                                                                                                                                                    |
| ------------ | ----------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| **GEM**      | `#include <GEM.h>`      | the AES and the VDI -- windows, menus, dialogues, drawing -- and `irk.h` for cyclic tasks on the real-time core                                    |
| **GEMduino** | `#include <GEMduino.h>` | the Arduino half: `millis()`, `micros()`, `delay()`, pins, `Uart` for the second serial port, and `Esp` for a radio module that speaks AT commands |

The older sketches include `"gem.h"` and `"irk.h"` directly instead of
`<GEM.h>`; both reach the same code, and the library form is simply the
one the IDE knows how to offer you.

### Two things GEMduino does differently, on purpose

**`delay()` gives the processor away.** On a machine with a desktop and
other programs running, burning a millisecond is rude: the kernel hands
the time to whoever is furthest behind and comes back when the delay is
up. If you need a wait that really does nothing else -- the short, exact
kind that a bit-banged protocol needs -- that is `delayMicroseconds()`.

**The pins are not all yours.** The display, the touch panel, the SD card
and the USB console have theirs; `PINS_IN_USE` in `GEMduino.h` says
which. **GPIO 4 and 5** are the free pair, and they carry the second
serial port -- which is where the radio module goes, and why
`GEMduino/sketchbook/WifiTime` can fetch the time.

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
      GEMduino\           everything built with the Arduino IDE
        toolchain\          the ARM compiler (fetched, not in the repo)
        ide\                the Arduino IDE, if you keep it here
        sketchbook\
          Window\           hello, GEMbedded -- the one to read first
          GEMbedClk\ Fractals\ WifiTime\ GEMtest\ Core0Latency\
          default\          what File -> New Sketch copies
          hardware\gembedded\rp2350\
            boards.txt      the board
            platform.txt    the compiler and upload recipes
            cores\gem\      the GEM bindings (gem.c/h, mathglue, accstart)
            lib\            libcmini.a and the TOS start-up object
            libraries\GEM\      the bindings again, as a library
            libraries\GEMduino\ millis, delay, pins, Uart, EspAt
      GEM\                everything built any other way
        examples\clock\   the two-halved clock, full screen
        examples\deploy\  DEPLOY.PRG and DEPLOY.ACC
      tools\
        gemdeploy.py        the uploader
        get-toolchain.py    fetches the compiler
      pTOS\ rtcore\         the operating system and the kernel
      docs\

## If something does not work

- **"no answer -- is DEPLOY running on the machine?"** DEPLOY has to be
  activated on the machine before uploading, and the menu entry needs to show the checkmark.
- **The wrong port.** `python tools/gemdeploy.py --list` shows which one
  says `GEMbedded`.
- **The program starts but the window is off-screen.** The desktop is currently 
  320x240; ask the AES for the work area rather than assuming a size.
- **`undefined reference to main`.** The sketch has no `main()` -- this
  board brings none of its own on purpose.


