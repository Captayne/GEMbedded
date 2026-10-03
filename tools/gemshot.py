#!/usr/bin/env python3
"""gemshot.py - one colour screenshot, without a window

    gemshot.py [-p COM23] [-o shot.png]

gemview.py does this too, but only from its own window, and the window
holds the port -- which is exactly what you do not want while something
else is watching the console.  This is the same screenshot with nothing
around it: ask, read, write the PNG, let go of the port.

Everything it needs is already in gemview.py; this is only a front door.
"""

import argparse
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gemview


def main():
    ap = argparse.ArgumentParser(description=__doc__,
            formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial port (found by itself)")
    ap.add_argument("-o", "--out", default="gemshot.png")
    ap.add_argument("-t", "--timeout", type=float, default=8.0)
    ap.add_argument("-c", "--crop", help="x,y,w,h of the part to keep")
    ap.add_argument("-z", "--zoom", type=int, default=1,
                    help="repeat each pixel N times, to read small text")
    a = ap.parse_args()

    m = gemview.Machine(a.port)
    try:
        rows = m.shot(a.timeout)
    finally:
        m.close()

    # rows are lists of (r,g,b) tuples -- see gemview.rgb565_to_rgb()
    if a.crop:
        x, y, w, h = (int(v) for v in a.crop.split(","))
        rows = [r[x : x+w] for r in rows[y : y+h]]
    if a.zoom > 1:
        z = a.zoom
        wide = []
        for r in rows:
            line = [px for px in r for _ in range(z)]
            wide.extend([line] * z)
        rows = wide

    gemview.W, gemview.H = len(rows[0]), len(rows)
    gemview.write_png(a.out, rows)
    print("%s  %d bytes" % (a.out, os.path.getsize(a.out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
