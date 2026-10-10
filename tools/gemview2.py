#!/usr/bin/env python3
"""gemview2.py - the console, the keyboard and the screen in one window

    python3 tools/gemview2.py [-p COM23] [-z 2] [--tick 3]
    python3 tools/gemview2.py --demo        no board: just look at it

gemview.py takes a still when you ask for it, and that is the right tool
once a display works.  While one is being brought up the question is a
different one, and it has two answers that look exactly alike at the
glass:

    a wrong picture     the system is fine, the scanout is not
    no picture at all   the system is dead

This window tells them apart in a second.  The framebuffer is on top, the
machine's console runs underneath it, and what you type goes to the
machine.  If the desktop is in the framebuffer and the console answers,
then the fault is behind the framebuffer -- in the panel, its timing, its
wiring -- and not in GEM.

That works even on a machine that has crashed.  The screenshot is
answered from a timer interrupt (rp2350_monitor.c's mon_dump_fb_raw, out
of the TIMER0 alarm), so the picture comes back from a hard faulted
system too, and shows what it last drew.  A console that has gone quiet
next to a framebuffer that is still being filled is a different fault
from both lines stopping at once.

How it differs from gemview.py, and why:

  *  One thread owns the port and never stops reading.  gemview asks for
     a picture and reads the answer on the spot, which cannot also be a
     console: whatever the machine says between two screenshots is
     dropped by the next flush, and those are exactly the lines that
     explain the picture.  Here the reader splits the stream itself, so
     nothing has to be drained before a request -- nothing was ever left
     standing.

  *  The geometry comes out of the header, not out of a constant.  A
     480x854 panel photographs itself without a change here.  (The
     firmware still says 320x240 in mon_dump_fb_raw; that constant is the
     one to fix when the panel arrives.)

  *  The picture can refresh itself.  Not once a second: a frame costs
     the machine four tenths of a second at 320x240 and over two seconds
     at 480x854, and a tool that spends half the machine's time shipping
     frames distorts the very timing being debugged.  So: 0.2 s after the
     last thing you typed -- which is when something changed -- and
     otherwise an idle tick of three seconds, with F9 to hold it.

Standard library only, like the rest of tools/.  Run it with a Python
that has tkinter; on this machine that is python3 (the Store build),
while plain "python" is PythonSCAD's interpreter and carries none.
"""

import argparse
import os
import queue
import re
import sys
import threading
import time

try:
    import tkinter as tk
    from tkinter import filedialog
except ImportError:
    tk = None

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gemdeploy                    # noqa: E402  the ports, and the port
import gemview                      # noqa: E402  the keyboard, and the PNG


# ---- pixels -----------------------------------------------------------

_TBL = None


def rgb_table():
    """65536 three-byte strings: RGB565 to RGB888, built once.

    gemview widens each field with a little arithmetic per pixel, which
    is fine for a still somebody asked for.  Here a frame arrives every
    three seconds and may be 410 000 pixels, so the arithmetic happens
    65536 times in total instead of once per pixel.  Measured on this
    machine: 13 ms to build the table, then 5 ms a frame at 320x240 and
    30 ms at 480x854, against 0.4 s and 2.2 s to fetch them.  The window
    is never the slow part.
    """
    global _TBL
    if _TBL is None:
        t = []
        for v in range(65536):
            # repeat the top bits downwards, so that full scale in five
            # bits stays full scale in eight
            r = (v >> 11) & 0x1f
            g = (v >> 5) & 0x3f
            b = v & 0x1f
            t.append(bytes(((r << 3) | (r >> 2),
                            (g << 2) | (g >> 4),
                            (b << 3) | (b >> 2))))
        _TBL = t
    return _TBL


def to_ppm(w, h, raw):
    """A P6 PPM of a raw RGB565 frame, low byte first.

    PPM and not PhotoImage.put(): put() wants a string of hex triplets in
    braces, which for a 480x854 frame is three and a half megabytes of
    text to build and then parse again.  Tk reads a PPM out of the -data
    option as it is -- as bytes, not base64: its GIF and PNG readers
    accept base64, its PPM reader wants the file.
    """
    tbl = rgb_table()
    # cast("H") reads the byte pairs in this machine's own order, which on
    # a PC is low byte first -- the order the wire uses.
    px = memoryview(raw).cast("H")
    return (b"P6\n%d %d\n255\n" % (w, h)
            + b"".join(map(tbl.__getitem__, px)))


# ---- the link ---------------------------------------------------------

class Link:
    """The port: read by one thread, written by whoever asks.

    The reader never stops, and it does the framing: a frame header
    switches it into counting bytes, everything else is console text.
    Both come out of one queue, in the order they arrived, which is also
    the order that matters -- the line before a frame is usually the
    reason to look at the frame.
    """

    HEAD = re.compile(rb"\[fb565 (\d{1,4}) (\d{1,4})\]\r?\n")
    HEAD_MAX = 24                   # the longest a header can possibly be
    MAX_PIXELS = 1 << 21            # a header asking for more is noise

    def __init__(self, port=None):
        self.name = port or gemdeploy.pick_port()
        self.port = gemdeploy.open_port(self.name)
        self.events = queue.Queue()
        self.wlock = threading.Lock()
        self.halt = threading.Event()
        self.reader = threading.Thread(target=self._loop, daemon=True)
        self.reader.start()

    # -- the one reader

    def _loop(self):
        buf = b""                   # text, or the start of a header
        pix = b""                   # a frame being collected
        want = 0                    # bytes of it still to come
        w = h = 0

        while not self.halt.is_set():
            try:
                chunk = self.port.read(4096)
            except Exception as e:
                self.events.put(("gone", str(e)))
                return

            if not chunk:
                # A read returns after 200 ms of silence (gemdeploy sets
                # ReadTotalTimeoutConstant).  Silence means the machine
                # has finished its sentence, so flush the tail that was
                # being held back in case it was the start of a header.
                # A 200 ms gap inside a 19-byte header would have to come
                # from a host that stopped draining, and this one never
                # does.
                if want == 0 and buf:
                    self.events.put(("text", buf))
                    buf = b""
                continue

            buf += chunk
            while True:
                if want:
                    take = min(want, len(buf))
                    pix += buf[:take]
                    buf = buf[take:]
                    want -= take
                    if want:
                        self.events.put(("progress", (len(pix), len(pix) + want)))
                        break
                    self.events.put(("frame", (w, h, pix)))
                    pix = b""

                m = self.HEAD.search(buf)
                if m:
                    if m.start():
                        self.events.put(("text", buf[:m.start()]))
                    gw, gh = int(m.group(1)), int(m.group(2))
                    buf = buf[m.end():]
                    if gw and gh and gw * gh <= self.MAX_PIXELS:
                        w, h, want = gw, gh, gw * gh * 2
                        continue
                    # A plausible-looking header with impossible numbers:
                    # say so and carry on reading text, rather than
                    # swallowing the console for the next megabyte.
                    self.events.put(("text",
                                     b"[gemview2: ignoring a header for %dx%d]\n"
                                     % (gw, gh)))
                    continue

                # No header in sight.  Hold back the last few bytes, which
                # may be the beginning of one.
                keep = min(len(buf), self.HEAD_MAX)
                if len(buf) > keep:
                    self.events.put(("text", buf[:len(buf) - keep]))
                    buf = buf[len(buf) - keep:]
                break

    # -- what anybody else does with it

    def ask_shot(self):
        """Ask for a picture.  The answer arrives through the queue.

        Setting the port to 1201 baud is what the machine watches for
        (rp2350_usbcon.c's BAUD_SCREENSHOT), the same way pico-sdk's 1200
        means BOOTSEL.  Out of band, so it cannot be confused with
        something typed.  Two ioctls and no waiting, so this is safe to
        call from the window's own thread.
        """
        with self.wlock:
            self.port.set_baud(gemview.BAUD_SCREENSHOT)
            self.port.set_baud(gemdeploy.BAUD)      # the change is the request

    def type(self, text):
        with self.wlock:
            self.port.write(text.encode("latin-1", "replace"))

    def close(self):
        self.halt.set()
        self.reader.join(1.0)
        try:
            self.port.close()
        except Exception:
            pass


class FakeLink:
    """Enough of a Link to look at the window without a board.

    The two stages this window is built in need no hardware to write, and
    it would be a poor claim if they needed hardware to see.  This talks
    to itself: a test pattern for a frame, a line of console now and
    then, and what is typed comes back as an echo.
    """

    name = "demo"

    def __init__(self):
        self.events = queue.Queue()
        self.events.put(("text", b"[demo] no board: this is gemview2 "
                                 b"talking to itself.\r\n"))
        self.n = 0

    def ask_shot(self):
        w, h = 320, 240
        raw = bytearray(w * h * 2)
        self.n += 1
        for y in range(h):
            for x in range(w):
                # a moving diagonal over a colour field, so that a stale
                # frame is obvious at a glance
                r = (x * 31 // w)
                g = (y * 63 // h)
                b = 31 if (x + y + self.n * 8) % 64 < 8 else 0
                v = (r << 11) | (g << 5) | b
                raw[(y * w + x) * 2] = v & 0xff
                raw[(y * w + x) * 2 + 1] = v >> 8
        self.events.put(("frame", (w, h, bytes(raw))))
        self.events.put(("text", b"[demo] frame %d\r\n" % self.n))

    def type(self, text):
        self.events.put(("text", text.encode("latin-1", "replace")))

    def close(self):
        pass


# ---- the window -------------------------------------------------------

class App:
    BEAT = 60                       # ms between looks at the queue
    AFTER_INPUT = 0.2               # s after the last keystroke
    TICK = 3.0                      # s of quiet before a frame by itself
    TIMEOUT = 10.0                  # s before an unanswered request is given up
    LOG_LINES = 400                 # kept in the console pane
    CHUNK_MAX = 2048                # a longer run of text is summarised

    def __init__(self, root, link, shotdir, zoom=0, tick=None, log_rows=12):
        self.root = root
        self.link = link
        self.shotdir = shotdir
        self.zoom = zoom            # 0 = decide from the frame
        if tick:
            self.TICK = tick

        self.frame = None           # (w, h, raw565) as it came in
        self.photo = None           # keep a reference or tkinter drops it
        self.auto = True
        self.due = 0.0              # when an input asked for a frame
        self.sent = 0.0             # when a request went out, 0 = none
        self.last = 0.0             # when a frame last came back
        self.save_next = False

        root.title("GEMbedded 2 - %s" % link.name)
        root.configure(bg="#202020")

        bar = tk.Frame(root, bg="#202020")
        bar.pack(fill="x", padx=8, pady=(8, 4))

        def button(text, cmd):
            # takefocus=0 throughout: the keyboard belongs to the machine,
            # and a focused button would eat the space bar.
            b = tk.Button(bar, text=text, command=cmd, takefocus=0)
            b.pack(side="left", padx=(0, 6))
            return b

        button("Shot (F5)", self.shot_now)
        self.autobtn = button("", self.toggle_auto)
        button("Save as... (Ctrl+S)", self.save_as)
        button("Paste (Ctrl+V)", self.paste)
        button("Clear log", self.clear_log)
        self.status = tk.Label(bar, text="", bg="#202020", fg="#a0a0a0",
                               anchor="w")
        self.status.pack(side="left", fill="x", expand=True)

        self.canvas = tk.Canvas(root, width=640, height=480, bg="#101010",
                                highlightthickness=0, takefocus=0)
        self.canvas.pack(padx=8)

        # The console underneath, and underneath is the point: the screen
        # is what the machine meant to say, this is what it had to say.
        pane = tk.Frame(root, bg="#202020")
        pane.pack(fill="both", expand=True, padx=8, pady=8)
        bars = tk.Scrollbar(pane)
        bars.pack(side="right", fill="y")
        self.log = tk.Text(pane, height=log_rows, bg="#101010", fg="#c8c8c8",
                           insertbackground="#c8c8c8", font=("Consolas", 9),
                           wrap="char", takefocus=0, state="disabled",
                           yscrollcommand=bars.set, highlightthickness=0,
                           borderwidth=0)
        self.log.pack(side="left", fill="both", expand=True)
        bars.config(command=self.log.yview)

        # Typing reaches the machine wherever the focus happens to be.
        # The pane's own binding runs before its class bindings and breaks
        # them, so a keystroke is never both sent and inserted.
        for w in (root, self.log):
            w.bind("<Key>", self.on_key)
        root.bind("<F5>", self.key_shot)
        root.bind("<F9>", self.key_auto)
        root.bind("<Control-s>", self.key_save)
        root.bind("<Control-v>", self.key_paste)
        root.bind("<Shift-Insert>", self.key_paste)
        root.bind("<Control-c>", self.key_copy)
        self.log.bind("<Control-c>", self.key_copy)
        self.canvas.bind("<Button-1>", self.on_click)
        root.focus_set()

        self.show_auto()
        self.say("typing goes to the machine")
        self.root.after(self.BEAT, self.beat)

    # -- saying things

    def say(self, text):
        self.status.config(text=text)

    def write_log(self, text):
        """Put bytes from the machine into the pane, readably.

        Control characters are turned into dots rather than handed to the
        Text widget, which would otherwise try to interpret them, and a
        long run of them -- the tail of a frame whose header was missed --
        is summarised instead of inserted.  150 kB of dots would freeze
        the window for the one case this tool exists for.
        """
        s = text.decode("latin-1").replace("\r\n", "\n").replace("\r", "\n")
        clean = "".join(c if (c in "\n\t" or " " <= c <= "~") else "." for c in s)
        if len(clean) > self.CHUNK_MAX:
            printable = sum(1 for c in clean if c != ".")
            clean = ("[gemview2: %d bytes, %d of them text -- a frame that "
                     "lost its header?]\n" % (len(clean), printable))

        at_bottom = self.log.yview()[1] > 0.999
        self.log.config(state="normal")
        self.log.insert("end", clean)
        over = int(self.log.index("end-1c").split(".")[0]) - self.LOG_LINES
        if over > 0:
            self.log.delete("1.0", "%d.0" % (over + 1))
        self.log.config(state="disabled")
        if at_bottom:
            self.log.see("end")

    def clear_log(self):
        self.log.config(state="normal")
        self.log.delete("1.0", "end")
        self.log.config(state="disabled")

    def show_auto(self):
        self.autobtn.config(text=("Auto %.0fs (F9)" % self.TICK) if self.auto
                            else "Held (F9)")

    # -- the clock
    #
    # One beat does all of it.  Three separate timers -- the queue, the
    # debounce after a keystroke, the idle tick -- would have to agree
    # about which of them is allowed to ask for a frame next; one beat
    # that reads the same three pieces of state cannot disagree with
    # itself.

    def beat(self):
        # The next beat is booked in a finally, because a beat that
        # throws would otherwise be the last one: the window would go on
        # drawing itself, accept typing, and quietly never look at the
        # port again.  A tool that is watched rather than read has to
        # fail loudly or not at all.
        try:
            self.pump()
            now = time.time()

            if self.sent and now - self.sent > self.TIMEOUT:
                self.sent = 0.0
                self.say("no answer in %.0f s -- is it stuck in a flash write?"
                         % self.TIMEOUT)

            if self.auto and not self.sent:
                if self.due and now >= self.due:
                    self.due = 0.0
                    self.ask()
                elif now - self.last >= self.TICK:
                    self.ask()
        except Exception as e:
            self.say("gemview2 tripped over itself: %s" % e)
            self.write_log(("[gemview2: %s: %s]\n"
                            % (type(e).__name__, e)).encode("latin-1", "replace"))
        finally:
            self.root.after(self.BEAT, self.beat)

    def pump(self):
        """Everything the reader has had to say since the last beat."""
        while True:
            try:
                kind, data = self.link.events.get_nowait()
            except queue.Empty:
                return

            if kind == "text":
                self.write_log(data)
            elif kind == "frame":
                self.got_frame(data)
            elif kind == "progress":
                got, total = data
                self.say("reading %d%% of %d bytes" % (got * 100 // total, total))
            elif kind == "gone":
                self.auto = False
                self.show_auto()
                self.say("the port has gone: %s" % data)
                self.write_log(b"[gemview2: the port has gone -- replug and "
                               b"start again]\n")

    # -- frames

    def ask(self):
        try:
            self.link.ask_shot()
        except Exception as e:
            self.say("could not ask: %s" % e)
            self.auto = False
            self.show_auto()
            return
        self.sent = time.time()

    def got_frame(self, frame):
        w, h, raw = frame
        self.sent = 0.0
        self.last = time.time()
        self.frame = frame

        z = self.zoom or (2 if h <= 400 else 1)
        try:
            img = tk.PhotoImage(data=to_ppm(w, h, raw), format="ppm")
        except Exception as e:
            self.say("cannot show %dx%d: %s" % (w, h, e))
            return
        if z > 1:
            img = img.zoom(z, z)
        self.photo = img
        self.canvas.config(width=w * z, height=h * z)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=self.photo)

        note = ""
        if self.save_next:
            self.save_next = False
            note = "  " + self.write_shot()
        self.say("%dx%d at %s%s" % (w, h, time.strftime("%H:%M:%S"), note))

    def write_shot(self):
        """A PNG of the frame in hand, in the shots directory.

        Only frames somebody asked for are written.  The automatic ones
        are a view, not a document, and a three-second tick would other-
        wise leave twelve hundred files an hour behind.
        """
        if not os.path.isdir(self.shotdir):
            os.makedirs(self.shotdir)
        path = os.path.join(self.shotdir, time.strftime("gem-%Y%m%d-%H%M%S.png"))
        self.save_png(path)
        return path

    def save_png(self, path):
        # gemview's own writer, through its module geometry -- the same
        # way gemshot.py uses it.  One PNG implementation, and the frame
        # keeps its RGB565 until something asks for a file.
        w, h, raw = self.frame
        gemview.W, gemview.H = w, h
        gemview.write_png(path, gemview.rgb565_to_rgb(raw))

    def save_as(self):
        if not self.frame:
            self.say("take a picture first")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".png", filetypes=[("PNG", "*.png")],
            initialfile=time.strftime("gem-%Y%m%d-%H%M%S.png"))
        if path:
            self.save_png(path)
            self.say("saved %s" % path)

    def shot_now(self):
        self.save_next = True
        self.due = 0.0
        if self.sent:
            self.say("already waiting for one")
            return
        self.ask()
        self.say("asking...")

    def toggle_auto(self):
        self.auto = not self.auto
        self.due = 0.0
        self.show_auto()
        self.say("refreshing by itself" if self.auto
                 else "held -- F5 still takes one")

    # -- the keyboard
    #
    # The tables are gemview's: one keyboard for both windows, because a
    # second one would drift and the machine would be blamed for it.

    def on_key(self, ev):
        if (ev.keysym in ("F5", "F9")
                or (ev.state & 4 and ev.keysym in ("s", "S", "c", "C", "v", "V"))
                or (ev.state & 1 and ev.keysym == "Insert")):
            # Not "break": with the focus in the log pane this binding is
            # the one that runs, and breaking here would stop the window's
            # own F5 and Ctrl+S from ever being reached.
            return None
        ch = gemview.sequence(ev.keysym, ev.state)
        if ch is None:
            ch = gemview.SPECIAL.get(ev.keysym)
        if ch is None and ev.char and " " <= ev.char <= "~":
            ch = ev.char
        if ch is None:
            self.say("%s: the machine has no scancode for that yet" % ev.keysym)
            return "break"
        try:
            self.link.type(ch)
            self.say("%s -> %s" % (ev.keysym,
                                   " ".join("%02x" % b
                                            for b in ch.encode("latin-1"))))
            # Something was typed, so something is about to change on the
            # screen.  Not now though: a word typed at speed would be ten
            # frames, and the interesting one is the one after the last
            # letter.
            self.due = time.time() + self.AFTER_INPUT
        except Exception as e:
            self.say("could not send: %s" % e)
        return "break"

    def paste(self):
        try:
            text = self.root.clipboard_get()
        except Exception:
            self.say("the clipboard is empty, or holds something that is not text")
            return
        line = ""
        for candidate in text.splitlines():
            if candidate.strip():
                line = candidate.strip()
                break
        if not line:
            self.say("nothing in the clipboard to type")
            return
        bad = sorted(set(c for c in line if not (" " <= c <= "~")))
        if bad:
            self.say("cannot type: %s" % " ".join("U+%04X" % ord(c) for c in bad))
            return
        try:
            for c in line:
                self.link.type(c)
                self.root.update()
                time.sleep(gemview.App.PASTE_PAUSE)
            self.say("typed %d characters -- Enter sends it" % len(line))
            self.due = time.time() + self.AFTER_INPUT
        except Exception as e:
            self.say("could not send: %s" % e)

    def copy_log(self):
        try:
            sel = self.log.get("sel.first", "sel.last")
        except Exception:
            self.say("nothing selected in the log")
            return
        self.root.clipboard_clear()
        self.root.clipboard_append(sel)
        self.say("%d characters copied" % len(sel))

    # Ctrl+C is the log's, not the machine's: gemview never sent control
    # characters either, and a console line that cannot be copied out of
    # the window has to be retyped into a bug report by hand.
    def key_copy(self, ev):
        self.copy_log()
        return "break"

    def key_shot(self, ev):
        self.shot_now()
        return "break"

    def key_auto(self, ev):
        self.toggle_auto()
        return "break"

    def key_save(self, ev):
        self.save_as()
        return "break"

    def key_paste(self, ev):
        self.paste()
        return "break"

    def on_click(self, ev):
        """Where that was on the machine's screen.

        Stage three of this window puts the mouse through, and it is
        cheaper than it looks: rp2350_touch.c already turns absolute
        coordinates into relative IKBD packets through call_mousevec(),
        so the firmware needs a door into that path and not a second
        input driver.  Until that door exists, the coordinate is at least
        worth reading off -- half of a touch calibration argument is
        knowing which pixel was meant.
        """
        if not self.frame:
            return
        w, h, _ = self.frame
        z = self.zoom or (2 if h <= 400 else 1)
        x, y = ev.x // z, ev.y // z
        if 0 <= x < w and 0 <= y < h:
            self.say("(%d, %d) -- no door in the firmware for the mouse yet"
                     % (x, y))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
            formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial port (found by itself)")
    ap.add_argument("-d", "--dir", default="shots",
                    help="where asked-for screenshots land (default: shots/)")
    ap.add_argument("-z", "--zoom", type=int, default=0,
                    help="pixels per pixel (default: 2 up to 400 rows, else 1)")
    ap.add_argument("--tick", type=float, default=None,
                    help="seconds of quiet before a frame by itself "
                         "(default %.0f)" % App.TICK)
    ap.add_argument("--rows", type=int, default=12,
                    help="lines of console shown (default 12)")
    ap.add_argument("--demo", action="store_true",
                    help="no board: a test pattern and an echo, to see the window")
    a = ap.parse_args()

    if tk is None:
        sys.exit("gemview2: this Python has no tkinter, so there can be no "
                 "window.  Try python3.  gemshot.py needs none.")

    if a.demo:
        link = FakeLink()
    else:
        try:
            link = Link(a.port)
        except SystemExit:
            raise
        except Exception as e:
            sys.exit("gemview2: no machine: %s" % e)

    root = tk.Tk()
    App(root, link, a.dir, zoom=a.zoom, tick=a.tick, log_rows=a.rows)
    try:
        root.mainloop()
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
