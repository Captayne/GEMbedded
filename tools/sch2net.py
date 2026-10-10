#!/usr/bin/env python3
"""sch2net.py - what is really wired to each pin, out of the schematic

    sch2net.py "Adafruit Fruit Jam.sch"             every GPIO and its net
    sch2net.py board.sch --parts                    the chips, not the passives
    sch2net.py board.sch --pin GP23                 one pin, in full
    sch2net.py board.sch --net PERIPH_RST           one net, in full
    sch2net.py board.sch --csv > board-nets.csv     for another program

A bought board tells you about itself three times, and the three do not
agree:

    the pinout page      what the marketing wants you to know
    the board definition what the vendor's own firmware happens to use
    the schematic        what is soldered on

Only the last one is a fact.  The first two are summaries, and a summary
leaves out whatever its author had no use for -- which is reliably the
part that costs a day.  On the Adafruit Fruit Jam, read on 10 October
2026, the schematic held three things that neither of the other two
mentioned at all:

  *  GP1/GP2 do not go to a USB socket.  They go to the UPSTREAM port of
     a CH334F hub, whose two downstream ports are the sockets.  One host
     controller, two devices -- keyboard AND mouse, not keyboard OR
     mouse, which is a different driver and a different plan.

  *  GP46 is the ESP chip select AND the output enable of a 74AHC1G125,
     a single tri-state buffer that puts the ESP's MISO on the shared
     line only while the ESP is selected.  The board arbitrates its own
     SPI bus in hardware.  Nothing in the pin list says so, and without
     it one would either avoid the bus or fight it.

  *  GP22 resets the audio codec and the ESP32-C6 together, and GP23 is
     the codec's interrupt and the ESP's bootstrap pin and a solder pad.
     Three jobs on two pins, none of them visible from a pin table.

So: read the schematic, every time, before writing a board profile.
This makes that cheap.

Eagle only, because that is what Adafruit, SparkFun and Pimoroni
publish.  An .sch file is XML, so no library is needed -- the parts are
in <parts> and the connections in <nets>, and the whole job is to turn
the second inside out so it is indexed by pin instead of by net.

What it does NOT do: it reports, it does not judge.  Whether a pin may
be used, and by whom, is gemprofile.py's question, and the answer lives
in a .gem profile that a person writes after reading this.
"""

import argparse
import csv
import re
import sys
import xml.etree.ElementTree as ET


# A supply symbol is a part in Eagle like any other, and there are
# hundreds of them: every ground pin on the sheet is its own GND part.
# They carry no information here -- the net name already says GND.
SUPPLY = re.compile(r"^(GND\d*|[APD]GND|VCC|VDD|VBUS|VIN|"
                    r"\d+(\.\d+)?V|P\+\d*)$", re.I)

# Resistors and capacitors are in the way when you want to see what a
# board is made of.  Crystals and diodes stay: a hub with its own
# crystal and a zener on an analog input are both the point.
PASSIVE = re.compile(r"^(RESISTOR|RESISTOR_\w+|CAP[_-]|CAPACITOR|"
                     r"TESTPOINT|FRAME\b|PINHD-|HEADER-)", re.I)

# The pin names on an MCU symbol spell out the alternate functions:
# "CS1/RX0/SCL0/PWM6B/GPIO29".  The number we want is the one after
# GPIO, and only that -- an ESP's "IO9/BOOT9" must not be read as a
# GPIO of the part we are indexing.
GPIO_PIN = re.compile(r"\bGPIO(\d+)\b", re.I)


def load(path):
    """parts {name: deviceset}, nets {name: [(part, pin), ...]}"""
    root = ET.parse(path).getroot()

    parts = {}
    for p in root.iter("part"):
        parts[p.get("name")] = (p.get("deviceset") or "",
                                p.get("value") or "")

    nets = {}
    for net in root.iter("net"):
        pins = [(pr.get("part"), pr.get("pin")) for pr in net.iter("pinref")]
        nets.setdefault(net.get("name"), []).extend(pins)
    return parts, nets


def find_mcu(parts, nets, given=None):
    """The part with the most GPIOn pins.

    Counted rather than matched by name.  The first version of this
    looked for a deviceset containing "2350" and also hid every part
    whose deviceset began with R as a resistor -- which hid the RP2350B
    itself.  Counting pins cannot make that mistake, and it works on a
    board whose MCU this program has never heard of.
    """
    if given:
        if given not in parts:
            sys.exit("sch2net: there is no part called %s" % given)
        return given
    score = {}
    for pins in nets.values():
        for part, pin in pins:
            if pin and GPIO_PIN.search(pin):
                score[part] = score.get(part, 0) + 1
    if not score:
        sys.exit("sch2net: no part has pins named GPIOn -- name one with --mcu")
    return max(score, key=score.get)


def gpio_map(mcu, nets):
    """{gpio number: (net name, [(part, pin), ...] other than the MCU)}"""
    out = {}
    for name, pins in nets.items():
        mine = [(p, q) for p, q in pins if p == mcu and q and GPIO_PIN.search(q)]
        if not mine:
            continue
        others = [(p, q) for p, q in pins if p != mcu]
        for _, pin in mine:
            out[int(GPIO_PIN.search(pin).group(1))] = (name, others, pin)
    return out


def describe(parts, part, pin):
    ds, val = parts.get(part, ("", ""))
    what = val or ds
    return "%s%s.%s" % (part, "(%s)" % what[:16] if what else "", pin)


def show_parts(parts):
    print("%-9s %-32s %s" % ("PART", "DEVICE", "VALUE"))
    for n in sorted(parts, key=lambda s: (re.sub(r"\d+$", "", s), len(s), s)):
        ds, val = parts[n]
        if SUPPLY.match(ds) or SUPPLY.match(val) or PASSIVE.match(ds):
            continue
        print("%-9s %-32s %s" % (n, ds[:32], val[:28]))


def show_net(parts, nets, name):
    hits = [k for k in nets if k.upper() == name.upper()]
    if not hits:
        near = [k for k in nets if name.upper() in k.upper()]
        sys.exit("sch2net: no net %r%s" % (
            name, ("  did you mean: " + ", ".join(sorted(near)[:8]))
            if near else ""))
    for k in hits:
        print("%s:" % k)
        for part, pin in sorted(nets[k]):
            print("    %s" % describe(parts, part, pin))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("schematic", help="an Eagle .sch file")
    ap.add_argument("--mcu", help="the part to index by (guessed otherwise)")
    ap.add_argument("--parts", action="store_true",
                    help="the chips and connectors, without the passives")
    ap.add_argument("--net", help="everything on one net")
    ap.add_argument("--pin", help="everything on one pin's net, e.g. GP23")
    ap.add_argument("--full", action="store_true",
                    help="also print the MCU's own pin name, with its"
                         " alternate functions")
    ap.add_argument("--csv", action="store_true",
                    help="gpio,net,endpoints -- for another program")
    a = ap.parse_args()

    try:
        parts, nets = load(a.schematic)
    except ET.ParseError as e:
        sys.exit("sch2net: %s is not readable as Eagle XML (%s)"
                 % (a.schematic, e))

    if a.parts:
        show_parts(parts)
        return 0
    if a.net:
        show_net(parts, nets, a.net)
        return 0

    mcu = find_mcu(parts, nets, a.mcu)
    gpios = gpio_map(mcu, nets)

    if a.pin:
        m = re.search(r"(\d+)", a.pin)
        if not m or int(m.group(1)) not in gpios:
            sys.exit("sch2net: %s is not on %s, or is on no net" % (a.pin, mcu))
        name, others, pinname = gpios[int(m.group(1))]
        print("GP%s is %r on %s (%s):" % (m.group(1), name, mcu, pinname))
        for part, pin in sorted(others):
            print("    %s" % describe(parts, part, pin))
        return 0

    if a.csv:
        w = csv.writer(sys.stdout, lineterminator="\n")
        w.writerow(("gpio", "net", "endpoints"))
        for gp in sorted(gpios):
            name, others, _ = gpios[gp]
            w.writerow((gp, name,
                        "; ".join(describe(parts, p, q) for p, q in others)))
        return 0

    ds, val = parts.get(mcu, ("", ""))
    print("%s -- %s %s,  %d of its pins are on a net"
          % (a.schematic, mcu, val or ds, len(gpios)))
    print()
    for gp in sorted(gpios):
        name, others, pinname = gpios[gp]
        who = ", ".join(describe(parts, p, q) for p, q in others)
        print("  GP%-2d  %-20s %s" % (gp, name[:20], who or "-- nothing else --"))
        if a.full:
            print("        %s" % pinname)

    gaps = [g for g in range(max(gpios) + 1) if g not in gpios]
    if gaps:
        print()
        print("  on no net at all: %s"
              % " ".join("GP%d" % g for g in gaps))
        print("  (a pin with no net is free in the strongest sense there is:")
        print("   nothing is soldered to it.  It may still be unreachable --")
        print("   whether it comes out on a header is a question for the")
        print("   board, not for the netlist.)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
