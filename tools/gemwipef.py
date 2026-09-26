#!/usr/bin/env python3
"""gemwipef.py - erase the flash drive F: of a GEMbedded machine

    gemwipef.py [-p COM20] [-o wipef.uf2]

It writes a UF2 file that covers the flash drive's own region of the QSPI
flash with 0xff and hands it to the bootrom, which erases each 4 KB sector
before programming it.  The drive comes back empty: on the next boot
rp2350_flashdisk_init() finds no mapping for logical sector 0 and formats
a fresh FAT16 (bios/machine/rp2350/rp2350_flashdisk.c).

WHY THE BOOTROM AND NOT THE MACHINE

Because the machine cannot do it to itself when it most needs to.  The
drive is log structured, GEMDOS never says that a file was deleted, and so
the log fills up however little is actually stored on it.  Once it is full
every single sector write first runs garbage collection, and every erase in
that holds the processor with interrupts masked for 23 milliseconds
(run_flash_op(), rp2350_flash.c).  USB is not served meanwhile, the host
loses patience, and it drops the whole device -- console, disk and all.
The bootrom has no such problem: pTOS is not running at all.

EVERYTHING ON F: IS LOST

The image itself is not touched: the operating system lives below
0x00900000 and the drive above it.  But every file on F: goes, including
DEPLOY.ACC -- so put that back first afterwards, through "Share flash via
USB", before expecting a deploy to work.

Standard library only, like the other tools here.
"""

import argparse
import os
import shutil
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gemdeploy                        # noqa: E402  (port finding)
import gemflash                         # noqa: E402  (the 1200 baud touch)

# from bios/machine/rp2350/rp2350_flashdisk.c
FD_FLASH_OFFSET = 0x00900000
FD_FLASH_SIZE = 0x00700000
FLASH_START = 0x10000000

# from tools/elf2uf2.py, which is where the bootrom's expectations are
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
RP2350_ARM_S_FAMILY_ID = 0xE48BFF59
PAGE_SIZE = 256


def build_uf2(path):
    """A UF2 of nothing but 0xff, covering the drive's region."""
    base = FLASH_START + FD_FLASH_OFFSET
    count = FD_FLASH_SIZE // PAGE_SIZE
    payload = b"\xff" * PAGE_SIZE + bytes(476 - PAGE_SIZE)

    with open(path, "wb") as f:
        for n in range(count):
            f.write(struct.pack("<8I", UF2_MAGIC_START0, UF2_MAGIC_START1,
                                UF2_FLAG_FAMILY_ID_PRESENT,
                                base + n * PAGE_SIZE, PAGE_SIZE, n, count,
                                RP2350_ARM_S_FAMILY_ID))
            f.write(payload)
            f.write(struct.pack("<I", UF2_MAGIC_END))
    return base, count


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="the machine's USB console")
    ap.add_argument("-o", "--out", default="wipef.uf2",
                    help="where to write the UF2 (default: wipef.uf2)")
    ap.add_argument("--build-only", action="store_true",
                    help="write the UF2 and stop, touching no machine")
    args = ap.parse_args()

    base, count = build_uf2(args.out)
    print("%s: %d blocks, 0x%08x-0x%08x (%d bytes)"
          % (args.out, count, base, base + FD_FLASH_SIZE,
             os.path.getsize(args.out)))
    if args.build_only:
        return 0

    # A drive already there means the machine is in BOOTSEL already.
    drive = gemflash.uf2_drives()
    if drive:
        drive = drive[0]
        print("%s is already waiting" % drive)
    else:
        port = args.port or gemdeploy.pick_port()
        if not port:
            sys.exit("gemwipef: no GEMbedded machine found; hold BOOT and "
                     "plug it in, then run this again")
        print("%s -> BOOTSEL" % port)
        gemflash.touch_1200(port)
        drive = gemflash.wait_for_drive(15)
        if not drive:
            sys.exit("gemwipef: no bootrom drive appeared; hold BOOT and "
                     "power up, then run this again")
        print("%s appeared" % drive)

    print("erasing 0x%08x-0x%08x; this takes a while" % (base, base + FD_FLASH_SIZE))
    try:
        shutil.copyfile(args.out, os.path.join(drive, os.path.basename(args.out)))
    except OSError as e:
        # The bootrom reboots when the last block lands, so the close may
        # well fail.  That is success, not failure.
        print("  (the drive went away while writing: %s)" % e)

    print("done -- the machine reboots and formats F: afresh")
    print("put DEPLOY.ACC back before expecting a deploy to work")
    return 0


if __name__ == "__main__":
    sys.exit(main())
