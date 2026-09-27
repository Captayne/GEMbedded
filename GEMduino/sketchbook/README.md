# The sketchbook

This folder is what you point the Arduino IDE at. It holds the board
definition, the GEM bindings, and the example sketches below.

**Set-up is in [docs/gemduino.md](../../docs/gemduino.md)** — the compiler, the
IDE, where to point it, and how Upload reaches the machine. Read that first;
this page is only the tour.

## A sketch here is a GEM program

There is no `setup()` and no `loop()`. A sketch has its own `main()`, calls
`appl_init()`, opens a window and runs its own `evnt_multi()` loop — exactly as
a program would on an Atari, because that is what it is. The board package
brings no `main()` of its own on purpose, so yours is the one that runs.

**C or C++, whichever suits the sketch.** The IDE compiles every `.c` file of a
sketch folder as C and the `.ino` itself as C++, and both routes work here:

- **The whole program in the `.ino`,** compiled as C++. `Window`, `default`,
  `WifiTime`, `Fractals` and `Core0Latency` are written this way. It is the
  easier road, and the one to take for a new sketch: the board package links a
  wrapper that calls your `main()` and runs global constructors before it.
- **The program in a `.c` file** beside the `.ino`, with the `.ino` left as a
  note. `GEMbedClk` and `GEMtest` are still shaped that way, from before the
  C++ road worked, and they build unchanged.

`undefined reference to main` means the sketch has no `main()` at all.

To reach the second core, `#include "irk.h"` and start tasks through the `_IRK`
cookie. `GEMbedClk` is the short way in; `Fractals` is the full one.

## The examples

Start at the top; each one adds one idea to the one above it.

| Sketch | What it is for |
| --- | --- |
| [`Window`](Window/) | **start here.** Hello GEMbedded: open a window, write in it, and draw only where it can be seen. |
| [`default`](default/) | the same program again, as the blueprint **File → New Sketch** starts from. |
| [`GEMbedClk`](GEMbedClk/) | a program with two halves — a clock face drawn by GEM, the seconds and a turning cube computed on the real-time core. |
| [`Fractals`](Fractals/) | four windows, three real-time tasks, and sliders that redistribute the processor between them while you watch. |
| [`WifiTime`](WifiTime/) | a radio module on the second serial port, an SNTP server, and the machine's own clock set from it. |
| [`GEMtest`](GEMtest/) | asks the AES and VDI bindings what they really do, and prints expected against actual. |
| [`Core0Latency`](Core0Latency/) | measures how long the system core can be away, from the core that cannot be delayed. |

## The board package

`hardware/gembedded/rp2350/` is the board itself:

| | |
| --- | --- |
| `boards.txt`, `platform.txt` | the board, and the compiler and upload recipes |
| `cores/gem/` | the AES and VDI bindings (`gem.c`/`gem.h`), `mathglue`, `accstart` |
| `lib/` | `libcmini.a` and the TOS start-up object |
| `libraries/GEM/` | the same bindings, as an Arduino library |
| `libraries/GEMduino/` | the Arduino half: `millis()`, `delay()`, pins, `Uart` for the second serial port, and `EspAt` for an AT radio module |

`GEMduino.h` is worth reading before you use it. Two things work differently
from a bare Arduino, both deliberately: **`delay()` gives the processor away**
rather than burning it — on a machine with a desktop and other programs a busy
wait is rude — and **the pins are not all yours**, since the display, the touch
panel, the card and the console have theirs. GPIO 4 and 5 are the free pair.

`ScreenPhotos/` is photographs of the machine running these.

## Every day

1. On the machine, **DEPLOY** must be listening. It is a desk accessory —
   `Desk → Deploy` in the menu — so it keeps listening while the desktop or
   another program runs, and you can upload as often as you like.
2. Open a sketch, press **Upload**.
3. The program lands on `C:\` and starts.

If the IDE says *"no answer — is DEPLOY running on the machine?"*, it is not.
