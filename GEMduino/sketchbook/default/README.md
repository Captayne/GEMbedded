# default — what a new sketch starts from

One window that can be moved, sized and closed. Nothing else. This is the
blueprint the Arduino IDE copies when you choose **File → New Sketch**, once
`arduino.sketch.inoBlueprint` points at `default.ino` (see
[docs/gemduino.md](../../../docs/gemduino.md), step 4). The same file is under
**File → Examples → GEM → Window**.

It is a whole program, not a fragment. It has its own `main()`, it announces
itself with `appl_init()`, and it waits for events in its own loop. There is no
`setup()` and no `loop()`, because that is not how GEM works — and because
everything the AES and the VDI can do is open to you.

## The one thing to copy carefully

```c
wind_get(win, WF_FIRSTXYWH, &x, &y, &w, &h);
while (w && h) {
    /* draw this piece */
    wind_get(win, WF_NEXTXYWH, &x, &y, &w, &h);
}
```

Draw **only when GEM says so, and only where the window can be seen.** The
rectangle list says which pieces those are. Other windows may lie over yours,
and they must stay untouched — there is no compositor here to sort it out
afterwards.

This is the part of GEM with no modern equivalent, and the part every new GEM
program gets wrong once. The loop above is why the example exists.

## Where to go next

- **The second core:** add `#include "irk.h"` and start cyclic tasks. See
  [`GEMbedClk`](../GEMbedClk/).
- **Drawing a computed picture:** fill an RGB565 canvas and copy it in with one
  `vro_cpyfm()`. See [`Fractals`](../Fractals/).
- **What the bindings do:** [`GEMtest`](../GEMtest/) prints expected against
  actual, for the calls whose arguments are easy to get wrong.

## Running it

DEPLOY has to be listening on the machine (`Desk → Deploy`), then press
**Upload**. The program lands on `C:\` and starts.

See [../README.md](../README.md) for the sketchbook.
