#!/usr/bin/env python3
"""gemdeploy.py - send programs to a GEMbedded machine over its USB console

DEPLOY must be running on the machine; it writes what arrives to F:\\ and,
unless told otherwise, starts the last file that arrives.

    gemdeploy.py [-p PORT] [-n NAME] [--no-run] FILE [FILE...]
    gemdeploy.py --list

Several files go over in the order given -- put the resource first and the
program last, so that the program starts with its resource in place.

Nothing but the Python standard library is needed: the serial port is
opened through termios on Linux and macOS and through the Windows API on
Windows.  That matters because this is what the Arduino IDE calls when
someone presses Upload, and nobody should have to install anything first.

The transfer (see examples/deploy/deploy.c):

     "PTUP1"   5 bytes
     flags     2 bytes         bit 0: please run it
     namelen   2 bytes
     name      namelen bytes   8.3, upper case
     size      4 bytes
     crc32     4 bytes
     data      size bytes

all numbers little-endian.  The machine answers with one line, "+ ..." when
it worked, "- ..." when it did not.
"""

import argparse
import glob
import os
import struct
import sys
import time
import zlib

BAUD = 115200
VID, PID = 0x1209, 0x0001       # what rp2350_usbcon.c announces
PRODUCT = "pTOS"


# ---------------------------------------------------------------- ports

def windows_ports():
    """[(port, description)], the GEMbedded one first if it is there"""
    import winreg

    found, ours = [], set()
    # The machine is a composite device, so Windows files it under
    # VID_1209&PID_0001&MI_00 rather than under the plain pair: match the
    # beginning of the name, and walk every instance below it.
    prefix = "VID_%04X&PID_%04X" % (VID, PID)
    try:
        usb = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                             r"SYSTEM\CurrentControlSet\Enum\USB")
        for i in range(winreg.QueryInfoKey(usb)[0]):
            device = winreg.EnumKey(usb, i)
            if not device.upper().startswith(prefix):
                continue
            key = winreg.OpenKey(usb, device)
            for j in range(winreg.QueryInfoKey(key)[0]):
                child = winreg.EnumKey(key, j)
                try:
                    params = winreg.OpenKey(key, child + r"\Device Parameters")
                    ours.add(winreg.QueryValueEx(params, "PortName")[0])
                except OSError:
                    pass
    except OSError:
        pass

    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                             r"HARDWARE\DEVICEMAP\SERIALCOMM")
        for i in range(winreg.QueryInfoKey(key)[1]):
            _, port, _ = winreg.EnumValue(key, i)
            found.append((port, "GEMbedded" if port in ours else ""))
    except OSError:
        pass

    found.sort(key=lambda p: p[1] != "GEMbedded")
    return found


def posix_ports():
    found = []
    for path in sorted(glob.glob("/dev/serial/by-id/*")):
        name = os.path.basename(path)
        real = os.path.realpath(path)
        found.append((real, "GEMbedded" if PRODUCT in name else name))
    if not found:
        for pattern in ("/dev/cu.usbmodem*", "/dev/ttyACM*", "/dev/ttyUSB*"):
            for path in sorted(glob.glob(pattern)):
                found.append((path, ""))
    found.sort(key=lambda p: p[1] != "GEMbedded")
    return found


def list_ports():
    return windows_ports() if os.name == "nt" else posix_ports()


def pick_port():
    ports = list_ports()
    if not ports:
        sys.exit("gemdeploy: no serial port found -- is the machine plugged in?")
    if ports[0][1] == "GEMbedded" or len(ports) == 1:
        return ports[0][0]
    sys.exit("gemdeploy: several ports, and none of them says GEMbedded.\n"
             "  Name one with -p:  " + ", ".join(p for p, _ in ports))


# ---------------------------------------------------------------- serial

class PosixPort:
    def __init__(self, name, baud):
        import termios
        self.termios = termios
        self.fd = os.open(name, os.O_RDWR | os.O_NOCTTY)
        speed = getattr(termios, "B%d" % baud)
        a = termios.tcgetattr(self.fd)
        a[0] = termios.IGNPAR                       # iflag
        a[1] = 0                                    # oflag
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL   # cflag
        a[3] = 0                                    # lflag: raw
        a[4] = a[5] = speed
        a[6] = list(a[6])
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 2                     # 0.2 s
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        termios.tcflush(self.fd, termios.TCIFLUSH)
        self._dtr()

    def _dtr(self):
        """Some hosts leave DTR down, and the console then drops what we
           send.  Raise it, and shrug if the platform will not say how."""
        import fcntl
        import termios
        try:
            fcntl.ioctl(self.fd, termios.TIOCMBIS,
                        struct.pack("I", termios.TIOCM_DTR))
        except (AttributeError, OSError):
            pass

    def flush_input(self):
        self.termios.tcflush(self.fd, self.termios.TCIFLUSH)

    def write(self, data):
        while data:
            n = os.write(self.fd, data)
            data = data[n:]

    def read(self, n):
        try:
            return os.read(self.fd, n)
        except OSError:
            return b""

    def close(self):
        os.close(self.fd)


class WindowsPort:
    def __init__(self, name, baud):
        import ctypes
        from ctypes import wintypes

        self.ctypes, self.wintypes = ctypes, wintypes
        self.k32 = ctypes.WinDLL("kernel32", use_last_error=True)

        class DCB(ctypes.Structure):
            _fields_ = [("DCBlength", wintypes.DWORD),
                        ("BaudRate", wintypes.DWORD),
                        ("flags", wintypes.DWORD),
                        ("wReserved", wintypes.WORD),
                        ("XonLim", wintypes.WORD),
                        ("XoffLim", wintypes.WORD),
                        ("ByteSize", ctypes.c_byte),
                        ("Parity", ctypes.c_byte),
                        ("StopBits", ctypes.c_byte),
                        ("XonChar", ctypes.c_char),
                        ("XoffChar", ctypes.c_char),
                        ("ErrorChar", ctypes.c_char),
                        ("EofChar", ctypes.c_char),
                        ("EvtChar", ctypes.c_char),
                        ("wReserved1", wintypes.WORD)]

        class TIMEOUTS(ctypes.Structure):
            _fields_ = [("ReadIntervalTimeout", wintypes.DWORD),
                        ("ReadTotalTimeoutMultiplier", wintypes.DWORD),
                        ("ReadTotalTimeoutConstant", wintypes.DWORD),
                        ("WriteTotalTimeoutMultiplier", wintypes.DWORD),
                        ("WriteTotalTimeoutConstant", wintypes.DWORD)]

        self.handle = self.k32.CreateFileW(r"\\.\%s" % name,
                                           0xC0000000,      # read | write
                                           0, None,
                                           3,               # OPEN_EXISTING
                                           0, None)
        if self.handle == wintypes.HANDLE(-1).value:
            raise OSError("cannot open %s (%d)"
                          % (name, ctypes.get_last_error()))

        dcb = DCB()
        dcb.DCBlength = ctypes.sizeof(DCB)
        if not self.k32.GetCommState(self.handle, ctypes.byref(dcb)):
            raise OSError("GetCommState failed (%d)" % ctypes.get_last_error())
        dcb.BaudRate = baud
        dcb.ByteSize = 8
        dcb.Parity = 0
        dcb.StopBits = 0
        # fBinary, and DTR held up; no flow control of any kind
        dcb.flags = (1 << 0) | (1 << 4)
        if not self.k32.SetCommState(self.handle, ctypes.byref(dcb)):
            raise OSError("SetCommState failed (%d)" % ctypes.get_last_error())

        t = TIMEOUTS(0, 0, 200, 0, 5000)    # 0.2 s to read, 5 s to write
        self.k32.SetCommTimeouts(self.handle, ctypes.byref(t))
        self.flush_input()

    def flush_input(self):
        self.k32.PurgeComm(self.handle, 0x0008)     # PURGE_RXCLEAR

    def write(self, data):
        written = self.wintypes.DWORD(0)
        buf = (self.ctypes.c_char * len(data)).from_buffer_copy(data)
        if not self.k32.WriteFile(self.handle, buf, len(data),
                                  self.ctypes.byref(written), None):
            raise OSError("WriteFile failed (%d)"
                          % self.ctypes.get_last_error())
        return written.value

    def read(self, n):
        buf = (self.ctypes.c_char * n)()
        got = self.wintypes.DWORD(0)
        if not self.k32.ReadFile(self.handle, buf, n,
                                 self.ctypes.byref(got), None):
            return b""
        return bytes(buf[:got.value])

    def close(self):
        self.k32.CloseHandle(self.handle)


def open_port(name):
    return WindowsPort(name, BAUD) if os.name == "nt" else PosixPort(name, BAUD)


# ---------------------------------------------------------------- transfer

def tos_name(path, given=None):
    """The name the file gets on the machine.  A name that was asked for
       has to be 8.3 already; one taken from a sketch is cut down to it,
       because "GEMbedClk.ino.PRG" is no name for a TOS drive."""
    if given:
        name = given.upper()
        stem, _, ext = name.partition(".")
        if len(stem) > 8 or len(ext) > 3 or "." in ext:
            sys.exit("gemdeploy: \"%s\" is not an 8.3 file name" % name)
        return name

    name = os.path.basename(path).upper()
    stem, _, ext = name.partition(".")
    if ext in ("INO", "ELF") or "." in ext:     # sketch.ino.prg and friends
        parts = name.split(".")
        stem, ext = parts[0], parts[-1]
    keep = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~!#%&-{}()@^"
    stem = "".join(c if c in keep else "_" for c in stem)[:8] or "PROGRAM"
    return stem + ("." + ext[:3] if ext else "")


def send(port, path, name, run, quiet=False):
    with open(path, "rb") as f:
        data = f.read()

    head = (b"PTUP1"
            + struct.pack("<HH", 1 if run else 0, len(name))
            + name.encode("ascii")
            + struct.pack("<II", len(data), zlib.crc32(data) & 0xffffffff))

    port.flush_input()
    port.write(head)
    sent = 0
    while sent < len(data):
        sent += port.write(data[sent:sent + 4096])
        if not quiet:
            sys.stdout.write("\r  %s %7d / %d bytes" % (name, sent, len(data)))
            sys.stdout.flush()
    if not quiet:
        sys.stdout.write("\n")

    line, deadline = b"", time.time() + 10.0
    while time.time() < deadline:
        c = port.read(1)
        if not c:
            continue
        if c in b"\r\n":
            if line:
                break
            continue
        line += c

    if not line:
        return "- no answer -- is DEPLOY running on the machine?"
    return line.decode("latin-1")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="what to send, program last")
    ap.add_argument("-p", "--port", help="serial port (found by itself if left out)")
    ap.add_argument("-n", "--name", help="8.3 name for a single file")
    ap.add_argument("--no-run", action="store_true",
                    help="only copy, do not start anything")
    ap.add_argument("--list", action="store_true", help="show the serial ports")
    args = ap.parse_args()

    if args.list:
        for port, what in list_ports():
            print("%-12s %s" % (port, what))
        return 0

    if not args.files:
        ap.error("nothing to send")
    if args.name and len(args.files) > 1:
        ap.error("-n names a single file")

    port_name = args.port or pick_port()
    try:
        port = open_port(port_name)
    except OSError as e:
        sys.exit("gemdeploy: %s" % e)

    print("%s -> %s" % (", ".join(os.path.basename(f) for f in args.files),
                        port_name))
    try:
        for i, path in enumerate(args.files):
            last = (i == len(args.files) - 1)
            answer = send(port, path, tos_name(path, args.name),
                          last and not args.no_run)
            print(answer)
            if not answer.startswith("+"):
                return 1
    finally:
        port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
