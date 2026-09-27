# GEMbedClk — a program with two halves

A clock face in a GEM window. The window moves, sizes, fills the screen, goes
behind other windows and closes, like any other. The dial takes the largest
circle that fits, and a wireframe cube turns in the middle of it.

What makes it worth reading is that the drawing and the timekeeping are on
**different processors**, and they talk in two different ways — each chosen for
the rate it runs at.

| | where | period | what it does |
| --- | --- | --- | --- |
| seconds task | real-time core | 1 s | counts, and sends an AES message |
| cube task | real-time core | 50 ms | turns eight corners with a rotation matrix, into shared memory |
| GEM half | system core | 50 ms | draws whatever is newest |

**The second hand arrives as an ordinary AES message.** So the GEM half waits
in `evnt_multi()` for it exactly as it would for a keystroke, and the AES
coalesces notifications — a task that reports often cannot flood it.

**The cube says nothing.** It writes its corners into double-buffered memory
both halves can see. At twenty frames a second, a message per frame would be
the wrong mechanism.

That contrast is the lesson: pick the channel by the rate, not by habit.

## Drawing where the window can be seen

The GEM half draws only through the rectangle list (`WF_FIRSTXYWH`,
`WF_NEXTXYWH`). Other windows may lie over yours and they must stay untouched.
This is the part of GEM that has no modern equivalent and that every new GEM
program gets wrong once.

## With NCLOCKS=2 it is DUALCLK.PRG

Two windows, two clocks, **four** tasks on the real-time core — the second cube
turning the other way, the second clock ticking half a second after the first.
The same source; only the constant changes.

## Without a kernel it still runs

If there is no `_IRK` cookie — no real-time kernel underneath — the clock falls
back to the AES timer and says so under the dial. That is the comparison, and
it is why the fallback exists.

## The arithmetic

Single precision, on the floating point unit both Cortex-M33 cores have. No
lookup tables: `sinf` and `cosf` come from the toolchain's libm, the same one
Arduino uses on ARM. `double` would be software emulation and is not worth it
here.

## Running it

DEPLOY has to be listening on the machine (`Desk → Deploy`), then press
**Upload**. The uploader shortens the name by itself: `GEMbedClk.ino.PRG`
arrives as `GEMBEDCL.PRG`, because file names on the machine are 8.3.

There is a second, plainer version of the same idea in
[`GEM/examples/clock`](../../../GEM/examples/clock/) — same two halves, but full screen
instead of in a window, which makes the halves easier to see.

See [../README.md](../README.md) for the sketchbook, and
[docs/gemduino.md](../../../docs/gemduino.md) for the set-up.
