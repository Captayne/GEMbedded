#!/usr/bin/env python3
"""gemflash.py - put a new OS image on a GEMbedded machine

The machine is running; this reboots it into the bootrom's BOOTSEL mode,
copies the image onto the drive that appears, and waits for the machine to
come back.  No button, no cable.

    gemflash.py [-p COM20] [rtcore/ptos+rtcore.uf2]

Setting the USB console to 1200 baud is what asks for the reboot -- the
same convention the Raspberry Pi pico-sdk uses, implemented in
bios/machine/rp2350/rp2350_usbcon.c.  Everything a running pTOS holds in
RAM is gone afterwards; the flash drive F: survives, it is in the QSPI
flash above the image.

Standard library only, like gemdeploy.py: a Python that Windows, Linux or
macOS ships is enough.
"""

import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gemdeploy                        # noqa: E402  (port finding, VID/PID)

DEFAULT_IMAGE = os.path.join("rtcore", "ptos+rtcore.uf2")
MARKER = "INFO_UF2.TXT"                 # every bootrom drive has this


def touch_1200(name):
    """Ask the machine to reboot into BOOTSEL, by setting 1200 baud."""
    cls = gemdeploy.WindowsPort if os.name == "nt" else gemdeploy.PosixPort

    # Setting the speed is the request, and the machine acts on it at once:
    # it is gone before the call that asked reports back.  So a failure here
    # is the usual outcome, not an error -- what counts is whether the
    # bootrom drive turns up.  Only being unable to open the port at all
    # says the machine was never there.
    try:
        port = cls(name, 1200)
    except OSError as e:
        if "cannot open" in str(e):
            sys.exit("gemflash: %s" % e)
        return
    time.sleep(0.05)
    try:
        port.close()
    except OSError:
        pass


def uf2_drives():
    """Every mounted volume that looks like a bootrom drive."""
    found = []

    if os.name == "nt":
        import ctypes

        mask = ctypes.WinDLL("kernel32").GetLogicalDrives()
        for i in range(26):
            if mask & (1 << i):
                root = "%s:\\" % chr(ord("A") + i)
                try:
                    if os.path.isfile(root + MARKER):
                        found.append(root)
                except OSError:
                    pass
    else:
        for base in ("/media", "/run/media", "/mnt", "/Volumes"):
            for root, dirs, files in os.walk(base):
                if MARKER in files:
                    found.append(root)
                dirs[:] = [] if MARKER in files else dirs
    return found


def wait_for_drive(timeout):
    """The drive takes a moment to appear, and the OS a moment to mount it."""
    deadline = time.time() + timeout

    while time.time() < deadline:
        drives = uf2_drives()
        if drives:
            return drives[0]
        time.sleep(0.2)
    return None


def wait_for_port(name, timeout):
    """The machine boots and the console comes back."""
    deadline = time.time() + timeout

    while time.time() < deadline:
        if any(p == name for p, _ in gemdeploy.list_ports()):
            return True
        time.sleep(0.3)
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", nargs="?", default=DEFAULT_IMAGE,
                    help="the .uf2 to flash (default: %s)" % DEFAULT_IMAGE)
    ap.add_argument("-p", "--port", help="the machine's USB console")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.image):
        sys.exit("gemflash: no such image: %s" % args.image)

    def say(*a):
        if not args.quiet:
            print(*a)
            sys.stdout.flush()

    # A drive already there means the machine is in BOOTSEL already --
    # after a failed attempt, say.  Then there is nothing to reboot.
    drive = uf2_drives()
    if drive:
        drive = drive[0]
        port = args.port
        say("%s is already waiting" % drive)
    else:
        port = args.port or gemdeploy.pick_port()
        if not port:
            sys.exit("gemflash: no GEMbedded machine found")
        say("%s -> BOOTSEL" % port)
        touch_1200(port)
        drive = wait_for_drive(15)
        if not drive:
            sys.exit("gemflash: no bootrom drive appeared; hold BOOTSEL and "
                     "power up, then run this again")
        say("%s appeared" % drive)

    size = os.path.getsize(args.image)
    say("%s -> %s (%d bytes)" % (os.path.basename(args.image), drive, size))
    try:
        shutil.copyfile(args.image, os.path.join(drive, os.path.basename(args.image)))
    except OSError as e:
        # The bootrom reboots the moment the last block lands, so the
        # close may well fail.  That is success, not failure.
        say("  (the drive went away while writing: %s)" % e)

    port = port or gemdeploy.pick_port()
    if port and wait_for_port(port, 20):
        say("%s is back" % port)
    else:
        say("the machine is booting")
    return 0


if __name__ == "__main__":
    sys.exit(main())
