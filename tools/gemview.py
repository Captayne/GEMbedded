#!/usr/bin/env python3
"""gemview.py - a keyboard and a camera for a GEMbedded machine

    gemview.py [-p COM23] [-d shots]

A small window on the PC that does two things over the USB console:

  *  What you type in it goes to the machine as keystrokes.  The console
     is the machine's keyboard (bios/ikbd.c's push_ascii_ikbdiorec), so
     this needs nothing on the machine's side that is not already there.

  *  F5, or the button, asks for a screenshot in colour, shows it, and
     writes it into the shots directory.  Ctrl+S saves a copy somewhere
     else.

  *  Ctrl+V, Shift+Insert, or the button types the clipboard in.  An
     address copied from a browser is long, case sensitive and easy to
     get wrong by hand; a mistyped one costs a page load, a timeout and
     a retry before it says so.

It is not a remote desktop and does not try to be: the picture is taken
when you ask for it, not streamed.  That is usually what you want while
developing -- a still you can look at -- and it costs the machine four
tenths of a second rather than its whole attention.

How the screenshot is asked for: setting the port to 1201 baud, which the
machine's console watches for (rp2350_usbcon.c's BAUD_SCREENSHOT), the
same way pico-sdk's 1200 means "reboot into BOOTSEL".  Out of band, so it
cannot be confused with something typed.  The machine answers with

    [fb565 320 240]\r\n

and then 320 * 240 * 2 bytes of RGB565, low byte first.

Standard library only, like the rest of tools/: tkinter for the window,
and the PNG is written here because there is no Pillow.

Run it with a Python that has tkinter.  On this machine that is python3
(the Store build); plain "python" is PythonSCAD's own interpreter, which
does not carry it:

    python3 tools/gemview.py
"""

import argparse
import os
import struct
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gemdeploy                        # noqa: E402  (ports, and Serial)

W, H = 320, 240
HEADER = b"[fb565 320 240]"
BAUD_SCREENSHOT = 1201


# ---- the picture ------------------------------------------------------

def rgb565_to_rgb(data):
    """RGB565, low byte first, to a list of H rows of W (r,g,b)."""
    rows = []
    for y in range(H):
        row = []
        base = y * W * 2
        for x in range(W):
            v = data[base + x * 2] | (data[base + x * 2 + 1] << 8)
            # widen each field by repeating its top bits, so that full
            # scale stays full scale
            r = (v >> 11) & 0x1f
            g = (v >> 5) & 0x3f
            b = v & 0x1f
            row.append(((r << 3) | (r >> 2),
                        (g << 2) | (g >> 4),
                        (b << 3) | (b >> 2)))
        rows.append(row)
    return rows


def write_png(path, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)                   # filter type 0
        for r, g, b in row:
            raw.extend((r, g, b))

    def chunk(tag, data):
        c = tag + data
        return (struct.pack(">I", len(data)) + c
                + struct.pack(">I", zlib.crc32(c) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# ---- the machine ------------------------------------------------------

class Machine:
    """The serial port, and the two things we do with it."""

    def __init__(self, port):
        self.name = port or gemdeploy.pick_port()
        self.port = gemdeploy.open_port(self.name)
        self.lock = threading.Lock()

    def close(self):
        try:
            self.port.close()
        except Exception:
            pass

    def type(self, text):
        with self.lock:
            self.port.write(text.encode("latin-1", "replace"))

    def shot(self, timeout=6.0):
        """Ask for a screenshot and read it. Returns rows, or raises."""
        with self.lock:
            # Drain first, and keep draining until the line is quiet.
            #
            # Flushing once is not enough: a dump already on its way, or
            # the tail of an earlier one, arrives after the flush and
            # before the header we are about to ask for. The picture then
            # comes out perfectly plausible and perfectly stale -- which
            # cost an hour of believing a screen that was no longer there.
            self.port.flush_input()
            while self.port.read(65536):
                pass

            self.port.set_baud(BAUD_SCREENSHOT)
            self.port.set_baud(gemdeploy.BAUD)  # the request is the change

            buf = b""
            deadline = time.time() + timeout
            while HEADER not in buf and time.time() < deadline:
                buf += self.port.read(4096)
            if HEADER not in buf:
                raise IOError("the machine did not answer")

            # everything after the header line is the picture
            buf = buf.split(HEADER, 1)[1].lstrip(b"\r\n")
            want = W * H * 2
            while len(buf) < want and time.time() < deadline:
                buf += self.port.read(want - len(buf))
            if len(buf) < want:
                raise IOError("%d of %d bytes" % (len(buf), want))

            return rgb565_to_rgb(buf[:want])


# ---- the window -------------------------------------------------------

# What a key in the window sends.  Only what the machine's keyboard table
# can turn back into a scancode, which today is ASCII -- the cursor keys
# and the function keys have no entry there yet, so they are left out
# rather than sent as something that would arrive as a wrong letter.
SPECIAL = {
    "Return": "\r",         # scancode 0x1c in the tables
    "KP_Enter": "\r",       # and 0x72, which maps to the same character
    "BackSpace": "\x08",    # 0x0e
    "Tab": "\t",            # 0x0f
    "space": " ",
}
# Delete and Escape are not here: sequence() below handles both, and one
# route per key is easier to reason about than two that agree.


# The keys that have no character of their own go over as the escape
# sequences a terminal would send; bios/ikbd.c's push_ascii_ikbdiorec()
# takes them apart again and pushes the Atari scancode.
#
# There is no Escape key here: a bare ESC and the start of a sequence are
# the same byte, and telling them apart needs a timeout on the machine.
# ESC ESC is a real Escape instead, which needs no clock.
CSI = {
    "Up": "A", "Down": "B", "Right": "C", "Left": "D",
    "Home": "H", "End": "F",
}
TILDE = {
    "Insert": 2, "Delete": 3, "Prior": 5, "Next": 6,   # Prior/Next = page up/down
    "F5": 15, "F6": 17, "F7": 18, "F8": 19, "F9": 20, "F10": 21,
}
SS3 = {"F1": "P", "F2": "Q", "F3": "R", "F4": "S"}


def sequence(keysym, state):
    """The bytes for a key that is not a character, or None."""
    # Tk's state bits: 1 shift, 4 control, and 8 or 0x20000 for alt
    # depending on the platform.  The machine wants them one more than a
    # bit field -- 2 shift, 3 alt, 5 control -- which is the xterm rule.
    mod = 1
    if state & 1:
        mod += 1
    if state & (8 | 0x20000):
        mod += 2
    if state & 4:
        mod += 4
    m = "" if mod == 1 else "1;%d" % mod

    if keysym in CSI:
        return "\x1b[" + m + CSI[keysym]
    if keysym in TILDE:
        n = TILDE[keysym]
        return "\x1b[%d%s~" % (n, (";%d" % mod) if mod > 1 else "")
    if keysym in SS3 and mod == 1:
        return "\x1bO" + SS3[keysym]
    if keysym in SS3:
        # F1..F4 with a modifier are sent the long way round
        return "\x1b[1;%d%s" % (mod, SS3[keysym])
    if keysym == "Escape":
        return "\x1b\x1b"
    return None


class App:
    # One character at a time, with a pause between them.
    #
    # The machine turns each byte into a scancode and pushes it into the
    # IKBD queue, which something upstream has to drain -- and whatever
    # drains it is an AES program's event loop, not an interrupt.  Eight
    # characters in a burst with 20 ms between bursts was tried first and
    # lost roughly every other character, silently: an address came out as
    # "hts/d.iie.r/iiAi".
    #
    # 20 characters a second is about as fast as anybody types, which is
    # the rate the machine is built for.  An address takes two seconds and
    # arrives whole, which beats a fifth of a second and a retype.
    PASTE_CHUNK = 1
    PASTE_PAUSE = 0.05

    def __init__(self, root, machine, shotdir):
        self.root = root
        self.machine = machine
        self.shotdir = shotdir
        self.rows = None
        self.photo = None

        root.title("GEMbedded - %s" % machine.name)
        root.configure(bg="#202020")

        bar = tk.Frame(root, bg="#202020")
        bar.pack(fill="x", padx=8, pady=(8, 4))
        tk.Button(bar, text="Screenshot (F5)", command=self.shot).pack(side="left")
        tk.Button(bar, text="Save as... (Ctrl+S)", command=self.save_as).pack(side="left", padx=6)
        tk.Button(bar, text="Paste (Ctrl+V)", command=self.paste).pack(side="left")
        self.status = tk.Label(bar, text="typing goes to the machine",
                               bg="#202020", fg="#a0a0a0", anchor="w")
        self.status.pack(side="left", fill="x", expand=True, padx=8)

        self.canvas = tk.Canvas(root, width=W * 2, height=H * 2,
                                bg="#101010", highlightthickness=0)
        self.canvas.pack(padx=8, pady=(0, 8))

        root.bind("<Key>", self.on_key)
        root.bind("<F5>", lambda e: (self.shot(), "break")[1])
        root.bind("<Control-s>", lambda e: (self.save_as(), "break")[1])
        root.bind("<Control-v>", lambda e: (self.paste(), "break")[1])
        root.bind("<Shift-Insert>", lambda e: (self.paste(), "break")[1])
        root.focus_set()

    def say(self, text):
        self.status.config(text=text)

    def on_key(self, ev):
        if (ev.keysym in ("F5",)
                or (ev.state & 4 and ev.keysym in ("s", "S", "v", "V"))
                or (ev.state & 1 and ev.keysym == "Insert")):
            return   # handled by its own binding
        ch = sequence(ev.keysym, ev.state)
        if ch is None:
            ch = SPECIAL.get(ev.keysym)
        if ch is None and ev.char and " " <= ev.char <= "~":
            ch = ev.char
        if ch is None:
            self.say("%s: the machine has no scancode for that yet" % ev.keysym)
            return "break"
        try:
            self.machine.type(ch)
            # the byte, in hex: what the machine turns into a scancode is
            # this and nothing else, so a report of "the wrong thing
            # happened" can be traced from here without guessing
            self.say("%s -> %s" % (ev.keysym,
                                   " ".join("%02x" % b for b in ch.encode("latin-1"))))
        except Exception as e:
            self.say("could not send: %s" % e)
        return "break"

    def paste(self):
        """Type the clipboard at the machine, as if it had been typed."""
        try:
            text = self.root.clipboard_get()
        except Exception:
            self.say("the clipboard is empty, or holds something that is not text")
            return
        # A copied address usually brings a newline with it, and whatever
        # follows that is a second line nobody meant to type.  Take the
        # first line with something on it, and leave Enter to the reader:
        # pasting an address and sending it are two decisions.
        line = ""
        for candidate in text.splitlines():
            if candidate.strip():
                line = candidate.strip()
                break
        if not line:
            self.say("nothing in the clipboard to type")
            return
        # The same range on_key() accepts.  Refusing is better than sending
        # something the machine draws as a different character.
        bad = sorted(set(c for c in line if not (" " <= c <= "~")))
        if bad:
            self.say("cannot type: %s"
                     % " ".join("U+%04X" % ord(c) for c in bad))
            return
        try:
            for i in range(0, len(line), self.PASTE_CHUNK):
                self.machine.type(line[i:i + self.PASTE_CHUNK])
                self.root.update()
                time.sleep(self.PASTE_PAUSE)
            self.say("typed %d characters -- Enter sends it" % len(line))
        except Exception as e:
            self.say("could not send: %s" % e)

    def shot(self):
        self.say("asking...")
        self.status.update_idletasks()
        try:
            self.rows = self.machine.shot()
        except Exception as e:
            self.say("no screenshot: %s" % e)
            return

        name = time.strftime("gem-%Y%m%d-%H%M%S.png")
        path = os.path.join(self.shotdir, name)
        if not os.path.isdir(self.shotdir):
            os.makedirs(self.shotdir)
        write_png(path, self.rows)
        self.show(self.rows)
        self.say("saved %s" % path)

    def show(self, rows):
        # tkinter's own PhotoImage, fed row by row: no Pillow needed
        img = tk.PhotoImage(width=W, height=H)
        data = " ".join(
            "{" + " ".join("#%02x%02x%02x" % px for px in row) + "}"
            for row in rows)
        img.put(data)
        self.photo = img.zoom(2, 2)     # keep a reference or it vanishes
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=self.photo)

    def save_as(self):
        if not self.rows:
            self.say("take a screenshot first")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".png", filetypes=[("PNG", "*.png")],
            initialfile=time.strftime("gem-%Y%m%d-%H%M%S.png"))
        if path:
            write_png(path, self.rows)
            self.say("saved %s" % path)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
            formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial port (found by itself)")
    ap.add_argument("--paste-delay", type=float, default=None,
                    help="seconds between pasted characters (default %.2f)"
                         % App.PASTE_PAUSE)
    ap.add_argument("-d", "--dir", default="shots",
                    help="where screenshots land (default: shots/)")
    a = ap.parse_args()

    if a.paste_delay is not None:
        App.PASTE_PAUSE = a.paste_delay

    try:
        machine = Machine(a.port)
    except Exception as e:
        print("no machine: %s" % e)
        return 1

    root = tk.Tk()
    App(root, machine, a.dir)
    try:
        root.mainloop()
    finally:
        machine.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
