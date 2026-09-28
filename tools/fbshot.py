#!/usr/bin/env python3
"""fbshot.py - screenshot of the pTOS framebuffer over the USB console

Sends a break to the console; pTOS' bring-up monitor answers with the
interrupted pc and, with the SPI display configured, a picture of the
screen as "[fb]" lines.

    fbshot.py [-p COM23] [-o shot.png] [-s 4]
    fbshot.py -i dump.txt -o shot.png     (render a capture already taken)

The format is what bios/machine/rp2350/rp2350_monitor.c's mon_dump_fb()
prints: 120 lines of 160 characters, each character one 2x2 block of
pixels, its brightness taken from the green channel and mapped onto the
ramp " .:-=+*#%@" -- space darkest, '@' brightest.

So the picture is grey, not colour, and a quarter of the resolution.
What it is for is seeing the layout: where things are, and whether they
overlap.

Standard library only, like the rest of tools/: no Pillow, the PNG is
written here.  The PowerShell version this replaces kept tripping over
its own comparisons, and rendering in Python is both shorter and the
same everywhere.
"""

import argparse
import os
import re
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

RAMP = " .:-=+*#%@"


def write_png(path, pixels, w, h, scale):
    """pixels: a list of h rows, each a list of w grey values 0..255."""
    raw = bytearray()
    for row in pixels:
        for _ in range(scale):
            raw.append(0)               # filter type 0 for this scan line
            for v in row:
                raw.extend(bytes([v]) * scale)

    def chunk(tag, data):
        c = tag + data
        return (struct.pack(">I", len(data)) + c
                + struct.pack(">I", zlib.crc32(c) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale,
                                      8, 0, 0, 0, 0))   # 8-bit greyscale
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    with open(path, "wb") as f:
        f.write(png)


def render(text, out, scale):
    # The dump says how wide it is. The monitor has printed it at more
    # than one resolution over time, and a picture that silently comes
    # out at a quarter size is worse than one that does not come out.
    rows = [r for r in re.findall(r"\[fb\]([^\r\n]+)", text) if len(r) >= 80]
    if not rows:
        print("no [fb] lines in the capture")
        return 1

    # The most common width, not the largest: when a line loses its
    # newline it runs into the next one and would set the maximum on its
    # own, which then throws out every line that is actually right.
    widths = {}
    for r in rows:
        widths[len(r)] = widths.get(len(r), 0) + 1
    cols = max(widths, key=lambda w: widths[w])
    rows = [r for r in rows if len(r) == cols]
    expect = cols * 3 // 4                  # the screen is 4:3

    if len(rows) < expect:
        # the last line often misses its newline because the dump ends
        # there, so one short is normal
        print("%d of %d lines" % (len(rows), expect))

    pixels = []
    for y in range(expect):
        if y < len(rows):
            pixels.append([255 * max(RAMP.find(c), 0) // (len(RAMP) - 1)
                           for c in rows[y]])
        else:
            pixels.append([128] * cols)     # not received

    write_png(out, pixels, cols, expect, scale)
    print("saved %s (%dx%d)" % (out, cols * scale, expect * scale))
    return 0


def capture(port, seconds):
    """Break, then read until the whole dump is in or the time is up."""
    import gemdeploy                    # its serial port, on every OS

    if not port:
        port = gemdeploy.find_port()

    sp = gemdeploy.Serial(port)
    try:
        sp.send_break(0.1)
        text = ""
        deadline = time.time() + seconds
        while time.time() < deadline:
            text += sp.read(4096).decode("latin-1")
            if len(re.findall(r"\[fb\]", text)) >= 240:
                break
        return text
    finally:
        sp.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
            formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial port (found by itself)")
    ap.add_argument("-o", "--out", default="fbshot.png")
    ap.add_argument("-s", "--scale", type=int, default=4)
    ap.add_argument("-i", "--input", help="render a capture instead")
    ap.add_argument("-t", "--timeout", type=float, default=20.0)
    a = ap.parse_args()

    if a.input:
        with open(a.input, encoding="latin-1") as f:
            text = f.read()
    else:
        text = capture(a.port, a.timeout)

    mon = re.search(r"\[mon\][^\r\n]*", text)
    if mon:
        print(mon.group(0))

    return render(text, a.out, a.scale)


if __name__ == "__main__":
    sys.exit(main())
