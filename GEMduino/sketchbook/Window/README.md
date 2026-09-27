# Window — hello, GEMbedded

The first one. It opens a window called *Sketch*, writes **Hello from GEM** in
it, and lets you move it, size it and close it. Nothing else — and that is
already a whole GEM program.

Start here, then go on to [`GEMbedClk`](../GEMbedClk/) for the second core.

## Why it is longer than "hello, world"

Because a window is not a console. There is no `print` that puts a line
somewhere and forgets about it: the text belongs to you, and GEM will ask you
to draw it again whenever something uncovers part of your window.

So the program has three parts a console program does not:

```c
appl_init();                                /* announce yourself */
vdi = v_opnvwk(graf_handle(...));           /* open a workstation to draw on */
win = wind_create(NAME|CLOSER|MOVER|SIZER, ...);
```

and then a loop that waits for GEM to say what happened — redraw, moved,
sized, closed — rather than one that does the work and exits.

## The part to copy carefully

```c
wind_get(win, WF_FIRSTXYWH, &x, &y, &w, &h);
while (w && h) {
    /* clip to this piece, and draw */
    wind_get(win, WF_NEXTXYWH, &x, &y, &w, &h);
}
```

**Draw only when GEM says so, and only where your window can be seen.** The
rectangle list gives you the visible pieces, one at a time; another window may
lie across yours and it must stay untouched. There is no compositor here to
sort it out afterwards — if you draw outside your rectangles, you draw on
somebody else's window.

This is the part of GEM with no modern equivalent, and the part every new GEM
program gets wrong once. It is why this example exists at all.

## The same file, twice

[`default/`](../default/) holds the identical program. That copy is the
blueprint the Arduino IDE starts a **File → New Sketch** from, once
`arduino.sketch.inoBlueprint` points at it (see
[docs/gemduino.md](../../../docs/gemduino.md), step 4). This copy is the one
that turns up under **File → Examples → GEM → Window**, where somebody looking
for an example will actually find it.

Two roads to the same first program, on purpose.

## Running it

DEPLOY has to be listening on the machine (`Desk → Deploy`), then press
**Upload**. The program lands on `C:\` and starts.

See [../README.md](../README.md) for the sketchbook.
