#!/usr/bin/env python3
"""ptosdeploy.py - send a program to a pTOS3000 machine over the USB console

DEPLOY.PRG must be running on the machine; nothing is accepted otherwise.
It writes the file to F:\\ and, when it is set to, runs it.

    ptosdeploy.py [-p COM20] [-n NAME.PRG] [--no-run] FILE

The wire format is described in apps/deploy/deploy.c.
"""

import argparse
import os
import struct
import sys
import time
import zlib

try:
    import serial                       # pyserial
except ImportError:
    sys.exit("ptosdeploy: pyserial is missing (pip install pyserial)")

MAGIC = b"PTUP1"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("file")
    ap.add_argument("-p", "--port", default="COM20", help="serial port")
    ap.add_argument("-n", "--name", help="name on the machine (8.3)")
    ap.add_argument("--no-run", action="store_true",
                    help="only store it, do not ask for it to be run")
    ap.add_argument("-t", "--timeout", type=float, default=10.0,
                    help="seconds to wait for the answer")
    args = ap.parse_args()

    data = open(args.file, "rb").read()
    name = args.name or os.path.basename(args.file)
    name = name.upper().encode("ascii")
    if len(name) > 12 or b"\\" in name or b"/" in name:
        sys.exit("ptosdeploy: the name must be a plain 8.3 file name")

    header = MAGIC + struct.pack("<HH", 0 if args.no_run else 1, len(name))
    header += name + struct.pack("<II", len(data), zlib.crc32(data) & 0xffffffff)

    with serial.Serial(args.port, 115200, timeout=0.2) as port:
        port.reset_input_buffer()
        port.write(header)
        port.flush()
        sent = 0
        while sent < len(data):
            n = port.write(data[sent:sent + 4096])
            sent += n if n else 0
            print("\r%6d / %d bytes" % (sent, len(data)), end="", file=sys.stderr)
        port.flush()
        print("", file=sys.stderr)

        deadline = time.time() + args.timeout
        line = b""
        while time.time() < deadline:
            ch = port.read(1)
            if not ch:
                continue
            if ch in b"\r\n":
                if line:
                    break
                continue
            line += ch
        text = line.decode("latin-1", "replace")

    if not text:
        sys.exit("ptosdeploy: no answer -- is DEPLOY.PRG running?")
    print(text)
    sys.exit(0 if text.startswith("+") else 1)


if __name__ == "__main__":
    main()
