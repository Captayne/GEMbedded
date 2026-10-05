#!/usr/bin/env python3
"""gemprofile.py - read a board profile and say what is left for a sketch

    gemprofile.py Hardware/profiles/weact-ak-ili9341.gem

The traffic light is always somebody's view, and the somebody here is the
GEMduino sketch: red is not "in use", it is "you may not".  What the
operating system defines, the sketch loses.

    ROT     taken, and nobody can join -- a chip select, a clock, a
            point-to-point line
    GELB    taken, but shareable -- the SCK/MOSI/MISO of a bus another
            device can hang off with its own chip select
    GRUEN   nobody has touched it

Two kinds of comment, and they do different jobs:

    # ...        to the end of the line, and -- ... as well, for prose:
                 why a pin sits where it sits.  That is the content no
                 tool can reconstruct, so it is the content worth having.

    /* ... */    switches a declaration off, nesting allowed.  This is
                 the what-if: comment out the DCF77 clock on COM#1 and
                 GP4/GP5 go green again, so you can see what that device
                 is costing you before you decide to fit it.

Nesting is the one place this deliberately differs from C.  Wrapping a
block that already holds a comment is exactly what switching things off
looks like, and having it break would be a daily annoyance for no gain.

Standard library only, like the rest of tools/.
"""

import argparse
import csv
import os
import re
import sys

PINS_CSV = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "rp2350_pins.csv")

# Signals a second device can hang off, by the name the chip gives them.
# A chip select cannot be shared: that is what a chip select is for.
SHAREABLE = ("_sclk", "_rx", "_tx", "_sda", "_scl")
NOT_SHAREABLE = ("_ss_n",)


def load_pins(path=PINS_CSV):
    """{gpio: {function name, ...}} plus the ADC channel, from the SVD table."""
    pins, adc = {}, {}
    with open(path) as fh:
        for row in csv.DictReader(fh):
            gp = int(row["gp"])
            pins[gp] = set(v for k, v in row.items()
                           if k.startswith("f") and v)
            if row["adc"]:
                adc[gp] = int(row["adc"])
    return pins, adc


def strip_comments(text):
    """Returns (code, switched_off).

    Line comments vanish.  Block comments vanish too, but what was inside
    them comes back as text, because a switched-off device is worth
    reporting: forgetting that you parked something is easy, and the file
    is the only place that remembers.

    One pass, three states, because two passes get it wrong: a line
    comment that happens to contain /* must not open a block, which is
    how C reads it and how anyone writing "/* parks a device" in prose
    expects it to read.

    Comment markers:
        //  and  --   to the end of the line, for prose
        /* ... */     switches a declaration off, and NESTS, which C
                      does not.  Wrapping a block that already holds a
                      comment is exactly what switching off looks like,
                      and having it break would be a daily annoyance for
                      no gain.

    "#" is not a comment at all any more: it means the instance number
    in (#1, ...) and nothing else.
    """
    out, parked = [], []
    depth, i, start, n = 0, 0, 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if depth == 0 and two in ("//", "--"):
            while i < n and text[i] != "\n":
                i += 1
            continue
        if two == "/*":
            if depth == 0:
                start = i + 2
            depth += 1
            i += 2
            continue
        if depth and two == "*/":
            depth -= 1
            i += 2
            if depth == 0:
                parked.append(text[start:i - 2])
            continue
        if depth:
            i += 1
            continue
        out.append(text[i])
        i += 1

    if depth:
        sys.exit("gemprofile: unterminated /* -- the rest of the file is off")
    return "".join(out), parked


def split_top(body):
    """["KEY=VALUE", ...] -- split on commas OUTSIDE parentheses.

    TIMING=(HS=6, HBP=30, ...) is one field, not four, and a comma inside
    it means something different from a comma outside.  A flat split
    cannot tell them apart, which is how a nested field ends up looking
    decorative: it parses into pieces nobody can put back together.
    """
    out, depth, cur = [], 0, []
    for ch in body:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    if "".join(cur).strip():
        out.append("".join(cur).strip())
    return out


def fields_of(body):
    """{KEY: value} for one statement, nested values kept whole."""
    f = {}
    for part in split_top(body):
        m = re.match(r"(\w+)\s*=\s*(.*)$", part, re.S)
        if m:
            f[m.group(1).upper()] = m.group(2).strip()
        elif part.startswith("#"):
            f["#"] = part[1:].strip()
    return f


def inner(value):
    """The pairs inside TIMING=(...) or S3W(...), as {KEY: value}."""
    m = re.search(r"\((.*)\)\s*$", value or "", re.S)
    if not m:
        return {}
    d = {}
    for part in split_top(m.group(1)):
        m2 = re.match(r"(\w+)\s*=\s*(.*)$", part, re.S)
        if m2:
            d[m2.group(1).upper()] = m2.group(2).strip()
    return d


# The bus the framebuffer is read over, when it lives in the PSRAM.
# clk_sys 150 MHz, divisor 2 (memory.c:158), quad -> 4 bits a clock.
QMI_PEAK_BYTES = 75e6 * 4 / 8


def evaluate_display(f, out):
    """Everything RES, FORMAT, REFRESH and TIMING decide without saying so."""
    m = re.match(r"(\d+)x(\d+)$", f.get("RES", ""))
    if not m:
        return
    w, h = int(m.group(1)), int(m.group(2))
    bpp = {"RGB565": 16, "RGB666": 18, "RGB888": 24}.get(f.get("FORMAT", ""), 0)
    if not bpp:
        out.append("  ! FORMAT fehlt oder ist unbekannt -- ohne das ist die"
                   " Leitungszahl nur eine Zahl")
        return
    fb = w * h * (bpp // 8)
    out.append("  i %dx%d %s -> Framebuffer %d + 2048 = %d B (%.2f MiB)"
               % (w, h, f["FORMAT"], fb, fb + 2048, (fb + 2048) / 1048576.0))

    t = inner(f.get("TIMING"))
    hz = f.get("REFRESH")
    if t:
        need = ("HS", "HBP", "HFP", "VS", "VBP", "VFP")
        missing = [k for k in need if k not in t]
        if missing:
            out.append("  ! TIMING unvollstaendig, fehlt: %s" % ", ".join(missing))
        else:
            htot = w + int(t["HS"]) + int(t["HBP"]) + int(t["HFP"])
            vtot = h + int(t["VS"]) + int(t["VBP"]) + int(t["VFP"])
            out.append("  i HTOT %d  VTOT %d  -> %d DCLK je Bild"
                       % (htot, vtot, htot * vtot))
            if hz:
                dclk = htot * vtot * float(hz)
                out.append("  i REFRESH %s Hz -> DCLK %.3f MHz"
                           % (hz, dclk / 1e6))
    elif f.get("TYPE", "").upper() == "RGBPANEL":
        out.append("  ! TIMING fehlt.  Bei RGBPANEL ist RES nur die aktive"
                   " Flaeche; ohne Porches laesst sich das PIO nicht"
                   " programmieren")

    if hz:
        rate = fb * float(hz)
        pct = rate / QMI_PEAK_BYTES * 100
        line = ("  %s PSRAM-Read %.2f MB/s = %.0f %% der QMI-Spitze"
                " (%.1f MB/s, theoretisch)"
                % ("!" if pct > 80 else "i", rate / 1e6, pct,
                   QMI_PEAK_BYTES / 1e6))
        out.append(line)
        if pct > 100:
            out.append("    Das geht nicht.  Der Bus kann es nicht liefern,"
                       " auch nicht teilweise.")
        elif pct > 55:
            out.append("    Eng: davon geht der XIP-Code des Prozessors noch"
                       " ab.  Niedriger anfangen und hochtasten.")

    if f.get("VRAM", "AUTO").upper() == "AUTO":
        stram = 456 * 1024
        where = "ST-RAM" if fb + 2048 + 256 * 1024 <= stram else "PSRAM"
        out.append("  i VRAM=AUTO -> %s" % where)
        if where == "PSRAM":
            out.append("    Folge: im Betrieb kann nicht ins Flash geschrieben"
                       " werden -- 23 ms ohne beide QSPI-Chip-Selects lassen"
                       " das Scanout-FIFO leerlaufen (3.2).")
        else:
            out.append("    Folge: im Betrieb darf gespeichert werden -- der"
                       " Scanout wird aus dem SRAM gefuettert, die 23 ms"
                       " kosten nur die CPU.")


def parse(code):
    """[(keyword, name, {signal: gpio}, bus)] -- one entry per statement."""
    items = []
    for stmt in re.split(r";", code):
        stmt = " ".join(stmt.split())
        if not stmt:
            continue
        # search, not match: the first chunk still carries BOARD and
        # STATE in front of it, and anchoring would drop the statement.
        m = re.search(r"(DEFINE|RESERVE)\s+(\w+)\s*\(?(.*?)\)?$", stmt, re.I)
        if not m:
            continue
        kw, what, body = m.group(1).upper(), m.group(2).upper(), m.group(3)
        bus = None
        m2 = re.search(r"BUS\s*=\s*(\w+)", body, re.I)
        if m2:
            bus = m2.group(1)
        sigs = {}
        for sig, gp in re.findall(r"(\w+)\s*=\s*GP(\d+)", body):
            sigs[sig.upper()] = int(gp)

        # RESERVE names pins rather than signals: what the user takes for
        # himself has no role in the system, only a reason, and the
        # reason is the quoted string.  Without this the fourth source of
        # claims is silently missing and grey never becomes green.
        if kw == "RESERVE" and not sigs:
            why = re.search(r'"([^"]*)"', stmt)
            for gp in re.findall(r"\bGP(\d+)", stmt):
                sigs[(why.group(1) if why else "reserviert")[:16]] = int(gp)

        # A parallel bus is written as its first pin and its format, not
        # as sixteen numbers: BASE says where it starts, FORMAT says how
        # wide it is, and the direction is fixed (LSB first).  Spelling
        # them out here is what makes them show up red one by one in the
        # table, which is the point -- "HSTX D1-" is an answer and
        # "the display" is not.
        if "BASE" in sigs:
            width = {"RGB565": 16, "RGB666": 18, "RGB888": 24}
            m3 = re.search(r"FORMAT\s*=\s*(\w+)", body, re.I)
            n = width.get(m3.group(1).upper() if m3 else "", 0)
            base = sigs.pop("BASE")
            lanes = (["B%d" % i for i in range(5)]
                     + ["G%d" % i for i in range(6)]
                     + ["R%d" % i for i in range(5)]) if n == 16 else []
            for i in range(n):
                sigs[lanes[i] if i < len(lanes) else "D%d" % i] = base + i

        # The config channel is its own kind, with its own pins, and that
        # is the whole reason for writing it as CFG=S3W(...) instead of
        # scattering the lines: it says these are needed WHILE BOOTING
        # and not afterwards.  A handover is not a collision, and only a
        # channel with a name can express the difference.
        boot_only = set()
        cfg = fields_of(body).get("CFG")
        if cfg and cfg.upper() != "NONE":
            for sig, gp in inner(cfg).items():
                sigs[sig.upper()] = int(re.sub(r"\D", "", gp) or -1)
                boot_only.add(sig.upper())

        items.append((kw, what, sigs, bus, fields_of(body), boot_only))
    return items


def colour(what, signal, gpio, funcs, bus=None):
    """What this pin is for a sketch, and why."""
    if what in ("RESERVE",):
        return "GELB", "vom Anwender reserviert"

    # Which chip function this signal actually is.  Naming the wrong
    # one is worse than naming none: it reads like an answer.
    # A stream a PIO generates is not a bus: there is no address, no
    # second chip select and nothing to arbitrate.  It looks like SPI on
    # the wire and shares nothing.
    if (bus and bus.upper().startswith("PIO")
            and signal in ("SCK", "SCLK", "MOSI", "MISO", "SDA", "SCL")):
        return "ROT", "PIO-Strom (%s) -- sieht aus wie SPI, teilt aber nichts" % bus

    want = {"SCK": "_sclk", "SCLK": "_sclk", "MOSI": "_tx", "MISO": "_rx",
            "SDA": "_sda", "SCL": "_scl"}.get(signal)
    if not want:
        return "ROT", "exklusiv"
    cand = sorted(f for f in funcs if f.endswith(want))
    if not cand:
        return "ROT", "exklusiv"
    return "GELB", "Bus %s -- ein weiteres Geraet kann mit" % cand[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("profile")
    ap.add_argument("--pins", default=PINS_CSV)
    args = ap.parse_args()

    pins, adc = load_pins(args.pins)
    with open(args.profile, encoding="utf-8") as fh:
        text = fh.read()
    code, parked = strip_comments(text)

    board = "?"
    m = re.search(r'BOARD\s+"([^"]+)"', code)
    if m:
        board = m.group(1)
    state = "?"
    m = re.search(r"STATE\s+(\w+)", code)
    if m:
        state = m.group(1)

    taken, notes = {}, []
    pio_use, pio_window = {}, {}
    for kw, what, sigs, bus, f, boot_only in parse(code):
        if what == "DISPLAY":
            evaluate_display(f, notes)
        if bus and bus.upper().startswith("PIO"):
            blk = int(re.sub(r"\D", "", bus) or 0)
            instr = f.get("INSTR")
            pio_use.setdefault(blk, []).append(
                (what, int(instr) if instr and instr.isdigit() else None))
            # What has to share one window: every data line and the clock.
            pio_window[what] = set(
                gp for sig, gp in sigs.items()
                if re.match(r"^([BGR]\d|D\d+|DCLK|SCK|MOSI)$", sig))
        for sig, gp in sigs.items():
            if gp not in pins:
                print("! GP%d existiert nicht auf diesem Chip" % gp)
                continue
            if sig in boot_only:
                col, why = "GELB", "Konfigkanal: nur beim Booten, danach frei"
            else:
                col, why = colour(kw, sig, gp, pins[gp], bus)
            if gp in taken:
                print("! GP%d doppelt: %s und %s" % (gp, taken[gp][0], what))
            taken[gp] = ("%s %s" % (what, sig), col, why)

    # --- PIO: Fenster und Instruktionsspeicher ------------------------
    #
    # The state machines are not what runs out first.  A block has four
    # of them and thirty-two instruction slots between them, and a
    # scanout can empty the slots while three machines still read as
    # free.  A traffic light that counts machines and not slots says
    # green in exactly that case.
    #
    # And the window: PIO sees 32 pins at a time, based at 0 or at 16
    # (PIO0.GPIOBASE, bit 4 only -- SVD).  Every data line AND the clock
    # must fall inside the same one, which is a constraint nobody sees
    # in a pin list.
    for blk in sorted(pio_use):
        used, unknown, who = 0, [], []
        for name, instr in pio_use[blk]:
            who.append(name)
            if instr is None:
                unknown.append(name)
            else:
                used += instr
        notes.append("  i PIO%d: %s, %d von 32 Instruktionen%s"
                     % (blk, ", ".join(who), used,
                        "" if not unknown
                        else " -- ohne %s, INSTR= fehlt dort"
                             % ", ".join(unknown)))
        if used > 32:
            notes.append("    ! mehr als der Block hat.  Nicht die State"
                         " Machines gehen zuerst aus, sondern die Plaetze.")

    for name, pinset in pio_window.items():
        if not pinset:
            continue
        lo, hi = min(pinset), max(pinset)
        ok = [b for b in (0, 16) if lo >= b and hi <= b + 31]
        if ok:
            notes.append("  i %s: GP%d..GP%d -> PIO-Fenster Basis %s, passt"
                         % (name, lo, hi, " oder ".join(str(b) for b in ok)))
        else:
            notes.append("  ! %s: GP%d..GP%d passt in kein PIO-Fenster."
                         "  Es gibt nur Basis 0 (GP0..GP31) und Basis 16"
                         " (GP16..GP47); Daten UND Takt muessen in dasselbe."
                         % (name, lo, hi))

    if notes:
        print("Was RES, FORMAT, REFRESH und TIMING still mitentscheiden:")
        for line in notes:
            print(line)
        print()

    print('Ampel fuer einen GEMduino-Sketch -- Brett "%s", STATE %s'
          % (board, state))
    if state != "BESTAETIGT":
        print("  Die ganze Tabelle ist eine Vorhersage, keine Auskunft:")
        print("  erst STATE BESTAETIGT heisst, dass res_claim() sie bestaetigt hat.")
    print()

    counts = {"ROT": 0, "GELB": 0, "GRUEN": 0}
    for gp in sorted(pins):
        if gp in taken:
            who, col, why = taken[gp]
            counts[col] += 1
            print("  GP%-2d [%-5s] %-18s %s" % (gp, col, who, why))
        else:
            counts["GRUEN"] += 1
    free = [gp for gp in sorted(pins) if gp not in taken]
    print("\n  GRUEN, %d Pins: %s"
          % (len(free), " ".join("GP%d" % g for g in free)))
    if counts["GELB"]:
        print("\n  Zu GELB, mit Vorbehalt: ein geteilter Bus braucht einen"
              "\n  Schiedsrichter, und es gibt noch keinen.  rp2350_spi.c kennt"
              "\n  weder claim noch release und stellt den Taktteiler fuer sich"
              "\n  um (spi_clock_sd()).  Ein zweites Geraet schriebe also mitten"
              "\n  in eine fremde Uebertragung.  Bis das steht: gelb wie rot"
              "\n  lesen.")

    print("\n  rot %d   gelb %d   gruen %d"
          % (counts["ROT"], counts["GELB"], counts["GRUEN"]))

    free_adc = [adc[g] for g in sorted(adc) if g not in taken]
    print("  ADC: %d von %d Kanaelen frei%s"
          % (len(free_adc), len(adc),
             (" (ADC" + ", ADC".join(str(a) for a in free_adc) + ")")
             if free_adc else ""))

    if parked:
        print("\n  Abgeschaltet mit /* */ -- waere sonst weg:")
        for block in parked:
            first = next((l.strip() for l in block.split("\n") if l.strip()), "")
            print("    %s" % first[:70])


if __name__ == "__main__":
    main()
