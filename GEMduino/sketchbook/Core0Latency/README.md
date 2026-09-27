# Core0Latency — how long can the system core be away?

pTOS gives the processor away at the end of every AES call, and IRKernel hands
it to whoever is furthest behind. That makes "how long does another task have
to wait" a question with a number, and this measures it instead of arguing
about it.

## Who does the measuring

Not the GEM half. It cannot: it would only see the gaps it happens to look at,
and it is the thing being measured.

So a **cyclic task on the real-time core** — which nothing on this machine can
delay — watches a timestamp that the GEM half refreshes as often as it can, and
keeps the worst gap it ever sees. The watcher runs every millisecond and counts
every gap over a millisecond as well as the longest.

One writer each for the two shared variables, so there is no lock.

## Three rounds, three seconds each

| | what core 0 is doing | what it tells you |
| --- | --- | --- |
| 1 | `evnt_multi()` | the ordinary state of a program: waiting, and yielding constantly |
| 2 | VDI calls in a loop | drawing does not yield, so the gap is one call long |
| 3 | writing to `C:` | a file write, sector by sector over SPI, with the driver polling a busy card |

The answer depends entirely on what core 0 is doing, which is the point. Round
1 is the good case and rounds 2 and 3 are the cost of doing work without
handing back.

## Why the third round changed

It used to write to `F:`, a drive in the QSPI flash, and that was a harsher
measurement: a flash sector erase takes some 45 ms, nothing shortens it, and
while it runs **neither QSPI chip select answers** — so the PSRAM is
unreachable too.

That drive is gone. The flash is now written only by the operating system, for
the settings it keeps, at a moment it chooses. The floor has not changed; it is
simply no longer something an application can walk into by accident, which was
the whole reason for removing the drive.

## What to take from it

This is the argument for the two halves, in numbers. Anything that must be on
time belongs on core 1, where none of these rounds can reach it — and the
stepper demonstrator holding a 1 ms grid to within 8 µs while core 0 reads the
card is the same claim, measured from the other end.

## Running it

DEPLOY has to be listening on the machine (`Desk → Deploy`), then press
**Upload**.

See [../README.md](../README.md) for the sketchbook, and
[docs/gemduino.md](../../../docs/gemduino.md) for the set-up.
