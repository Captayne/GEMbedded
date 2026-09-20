# GEMbedded

**A graphical operating system on a microcontroller, where real-time control
is an ordinary application.**

GEMbedded runs [pTOS](https://github.com/kelihlodversson/pTOS) — a portable
descendant of EmuTOS, the free Atari TOS — on an RP2350 board, with a
display, touch, files and GEM windows, while one processor core stays free
for control loops that must be on time.

A program has two halves:

* the **normal half** runs on the system core: windows, dialogues, files,
  written like any GEM program;
* the **real-time half** runs on the second core under IRKernel, called on
  a fixed period and undisturbed by whatever the user interface is doing.

The reference application is a 3D printer: stepper pulses on the real-time
core, the user interface on the system core, on one chip for a few euros.

## What runs today

On a Waveshare RP2350-PiZero with a 2.8" SPI display:

* GEM desktop on 320x240, touch as the mouse (tap, double tap, drag),
  touch calibration in the Options menu and at boot;
* files on SD card (FAT16) and on a 4 MB drive in the flash, with wear
  levelling;
* programs sent from the PC over USB onto the machine and started there
  (`tools/ptosdeploy.py`, with `DEPLOY.PRG` running on the machine);
* real-time tasks on core 1 through the `_RTX` interface: the stepper
  demonstrator holds a 1 ms grid to within 8 us while the system core
  reads from the SD card.

## What is in here

| Directory | |
|---|---|
| `pTOS/` | the operating system itself, a separate repository (see below) |
| `rtcore/` | the real-time runtime for core 1: IRKernel plus the `_RTX` mailbox |
| `apps/` | programs for the machine, and `apps/lib`, the beginnings of the SDK |
| `tools/` | host side: deploy, screenshots, diagnostics |
| `docs/` | plans, notes, licensing |
| `board-test/` | bring-up sketches for the bare board |

`pTOS/` is not part of this repository; it is the pTOS fork with the
RP2350 port. See `docs/repositories.md`.

## Building

Needs the Arm GNU toolchain and a Unix-like shell (WSL is fine):

    cd pTOS && make rp2350_defconfig && make    # the operating system
    cd rtcore && make                           # + core 1, gives ptos+rtcore.uf2

Flashing: open the machine's USB port at 1200 baud (it reboots into the
bootloader) and copy `ptos+rtcore.uf2` onto the drive that appears.

Programs:

    cd apps/deploy && make                      # DEPLOY.PRG
    python3 tools/ptosdeploy.py -p COM20 apps/deploy/DEPLOY.PRG

## Licence

The code in this repository is under the MIT licence (see `LICENSE`), so
that programs written against it are not bound by the operating system's
licence.  pTOS itself is GPL v2 or later, and IRKernel has its own licence;
`docs/licensing.md` explains what that means for the parts that are built
together.
