#!/usr/bin/env python3
"""gemboards.py - every board profile, in the order of how real it is

    gemboards.py                      the pipeline
    gemboards.py --markdown           the same as a table, for a README
    gemboards.py --check              non-zero exit if any profile complains

A board does not arrive finished.  It is bought, then thought about,
then wired, then it boots, and each of those is a different kind of
truth.  The profile language already says which one a board is in --
STATE GEPLANT, VERDRAHTET, BESTAETIGT -- and this reads that off every
profile in the directory and lays them out in that order.

Why the state stays IN the file and not in the folder name: a board
moves.  A folder per state means moving the file every time something
is learned, which breaks every path that pointed at it and loses the
history at the moment it becomes interesting.  Worse, it would be a
second place where the state is written, and two places disagree
eventually.  The file is the truth; this is only a view of it.

    BESTAETIGT   has booted, and we have a picture to prove it.  This
                 is the one to fall back to when something else breaks.
    VERDRAHTET   the wire is pulled, the machine has never run.
    GEPLANT      a plan.  Possibly a very good one, still a plan.

The colours come from gemprofile.py, run as a program and not imported
for that purpose.  Section 13.5 of BoardRessourcesManager.md asks for
exactly this: the command line first, and everything else calls it, so
that a second implementation cannot drift away from the first.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gemprofile                   # noqa: E402  for the comment stripper

DEFAULT_DIR = os.path.join(os.path.dirname(HERE), "Hardware", "profiles")

# most real first
ORDER = ["BESTAETIGT", "VERDRAHTET", "GEPLANT"]
HEADING = {
    "BESTAETIGT": "LAEUFT        -- hat gebootet, Bild liegt vor",
    "VERDRAHTET": "VERDRAHTET    -- gebaut, nie gestartet",
    "GEPLANT":    "GEPLANT       -- ein Vorhaben, kein Draht",
    "?":          "OHNE STATE    -- das Profil sagt es nicht",
}


def summarise(path):
    """What one profile says about itself, plus what the checker says."""
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    code, parked = gemprofile.strip_comments(text)

    m = re.search(r'BOARD\s+"([^"]+)"', code)
    board = m.group(1) if m else os.path.basename(path)
    m = re.search(r"STATE\s+(\w+)", code)
    state = m.group(1).upper() if m else "?"
    m = re.search(r"CHIP=(\w+)", code)
    chip = m.group(1) if m else "?"

    # the screen, because it is the thing a board is chosen for
    screen = "--"
    for kw, what, sigs, bus, f, boot_only in gemprofile.parse(code):
        if what != "DISPLAY":
            continue
        bits = [f.get("CHIP", ""), f.get("RES", "")]
        if f.get("OUT") and f.get("OUT") != f.get("RES"):
            bits.append("-> " + f["OUT"])
        bits.append(f.get("TYPE", ""))
        screen = " ".join(b for b in bits if b)
        break

    out = subprocess.run(
        [sys.executable, os.path.join(HERE, "gemprofile.py"), path],
        capture_output=True, text=True)
    light, adc, warn = "?", "", 0
    for line in out.stdout.splitlines():
        s = line.strip()
        if s.startswith("rot "):
            light = s
        elif s.startswith("ADC:"):
            adc = s
        elif s.startswith("!"):
            warn += 1
    if out.returncode != 0:
        warn += 1

    return dict(file=os.path.basename(path), board=board, state=state,
                chip=chip, screen=screen, light=light, adc=adc,
                warn=warn, parked=len(parked),
                stderr=out.stderr.strip())


def collect(d):
    if not os.path.isdir(d):
        sys.exit("gemboards: there is no directory %s" % d)
    rows = [summarise(os.path.join(d, n))
            for n in sorted(os.listdir(d)) if n.endswith(".gem")]
    if not rows:
        sys.exit("gemboards: no .gem profiles in %s" % d)
    return rows


def text(rows, d):
    print("Die Pipeline -- %s" % d)
    for state in ORDER + ["?"]:
        here = [r for r in rows if r["state"] == state]
        if not here:
            continue
        print()
        print("  %s" % HEADING[state])
        for r in here:
            print()
            print("    %-26s %-9s %s" % (r["board"], r["chip"], r["screen"]))
            print("    %-26s %s" % (r["file"], r["light"]))
            if r["adc"]:
                print("    %-26s %s" % ("", r["adc"]))
            notes = []
            if r["warn"]:
                notes.append("%d Einwand%s des Pruefers"
                             % (r["warn"], "" if r["warn"] == 1 else "e"))
            if r["parked"]:
                notes.append("%d geparkte Stelle%s"
                             % (r["parked"], "" if r["parked"] == 1 else "n"))
            if notes:
                print("    %-26s %s" % ("", ", ".join(notes)))
            if r["stderr"]:
                print("    %-26s FEHLER: %s" % ("", r["stderr"].splitlines()[0]))
    print()
    print("  Ein Profil lesen:  python3 tools/gemprofile.py <datei>")


def markdown(rows):
    print("| Brett | Zustand | Chip | Bildschirm | Ampel | Datei |")
    print("| ----- | ------- | ---- | ---------- | ----- | ----- |")
    rank = {s: i for i, s in enumerate(ORDER)}
    for r in sorted(rows, key=lambda x: (rank.get(x["state"], 9), x["board"])):
        print("| %s | %s | %s | %s | %s | `%s` |"
              % (r["board"], r["state"], r["chip"], r["screen"],
                 r["light"] or "--", r["file"]))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", nargs="?", default=DEFAULT_DIR,
                    help="where the .gem profiles are (default: %(default)s)")
    ap.add_argument("--markdown", action="store_true",
                    help="a table instead of the pipeline")
    ap.add_argument("--check", action="store_true",
                    help="say nothing unless something is wrong, and exit"
                         " non-zero if it is")
    a = ap.parse_args()

    rows = collect(a.dir)

    if a.check:
        bad = [r for r in rows if r["warn"] or r["stderr"]]
        for r in bad:
            print("%s: %d Einwand/Einwaende%s"
                  % (r["file"], r["warn"],
                     "  " + r["stderr"].splitlines()[0] if r["stderr"] else ""))
        print("%d Profile, %d mit Einwaenden" % (len(rows), len(bad)))
        return 1 if bad else 0

    if a.markdown:
        markdown(rows)
    else:
        text(rows, a.dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
