# WifiTime — the machine asks the internet what time it is

This machine has no clock that survives being switched off. Every start begins
at midnight until somebody says otherwise. A radio module on the second serial
port can ask the internet instead, so that is what this does — and then it sets
the **system** clock, so the desktop shows the right time and files get the
right date, not just this window.

That is the part worth copying: a program that learns something useful should
tell the operating system, not keep it.

## No password in a sketch

The network and its key come from the desktop — **Options → "Wifi
settings…"**, kept with *Save desktop* — and the sketch reads them through the
`_WIF` cookie. The hour offset from UTC is entered in the same place.

So the sketch can be shared, committed and photographed without leaking
anything, and the machine keeps its own credentials across a reflash.

## Wiring an ESP-01

| ESP-01 | RP2350 | |
| --- | --- | --- |
| TXD | GPIO 5 | UART1 in |
| RXD | GPIO 4 | UART1 out |
| CH_PD / EN | 3.3 V | or it stays silent |
| VCC | 3.3 V | **from its own supply** |
| GND | GND | |

GPIO 4 and 5 are the free pair; the display, the touch panel, the card and the
console have all the others (`PINS_IN_USE` in `GEMduino.h`).

**The supply is what usually goes wrong.** The module pulls some 300 mA while
it transmits, which no GPIO pin can give — and a module that browns out behaves
exactly like one that is not there. If nothing answers, suspect the 3.3 V
before you suspect the code.

## What it uses

`#include <GEMduino.h>` brings the Arduino half — `millis()`, `delay()`, pins,
the second serial port — and `EspAt` wraps the module's AT command set:

```cpp
Esp.begin(115200);
Esp.join(ssid, key);
Esp.startTime(utc_offset, "pool.ntp.org");   /* AT+CIPSNTPCFG */
Esp.time(&t);                                /* AT+CIPSNTPTIME? */
```

Then `Tsetdate()` and `Tsettime()`, and the whole machine knows.

Note that **`delay()` here gives the processor away** rather than burning it.
On a machine with a desktop and other programs, a busy wait is rude; the kernel
hands the time to somebody else and comes back. `delayMicroseconds()` is the
busy one, for the short exact waits a bit-banged protocol needs.

The window keeps the last nine lines of the conversation with the module, which
is the whole debugging story when a network will not join.

## This sketch is a single `.ino`

It is compiled as C++, and it still has its own `main()` — no `setup()`, no
`loop()`. That works because the board package links a wrapper that calls it
and runs global constructors first. Older sketches here keep the program in a
`.c` file beside the `.ino`; both are fine, and C++ is the easier of the two if
you want the library classes.

## Running it

Set the network under **Options → Connecty…** on the machine first, then
DEPLOY has to be listening (`Desk → Deploy`) and you press **Upload**.

See [../README.md](../README.md) for the sketchbook, and
[docs/gemduino.md](../../../docs/gemduino.md) for the set-up.
