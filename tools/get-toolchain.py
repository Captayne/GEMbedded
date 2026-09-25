#!/usr/bin/env python3
"""get-toolchain.py - fetch the ARM compiler into arduino/toolchain

GEMbedded is meant to be portable: everything it needs lives in this
folder and nothing is installed anywhere else.  The one piece too big for
a repository is the compiler, so it is fetched once, here, and the folder
is in .gitignore.  Copy the whole GEMbedded folder afterwards and the
compiler travels with it.

    tools/get-toolchain.py [--version 14.2.rel1] [--url URL] [--force]

Nothing but the Python standard library is needed.
"""

import argparse
import hashlib
import os
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

BASE = "https://developer.arm.com/-/media/Files/downloads/gnu"

# what to fetch, by what this computer is
BUILDS = {
    ("win32", "any"):    "mingw-w64-i686-arm-none-eabi.zip",
    ("linux", "x86_64"): "x86_64-arm-none-eabi.tar.xz",
    ("linux", "aarch64"): "aarch64-arm-none-eabi.tar.xz",
    ("darwin", "arm64"): "darwin-arm64-arm-none-eabi.tar.xz",
    ("darwin", "x86_64"): "darwin-x86_64-arm-none-eabi.tar.xz",
}


def build_name(version):
    import platform

    system = "win32" if os.name == "nt" else sys.platform
    machine = platform.machine().lower()
    if machine in ("amd64", "x86-64"):
        machine = "x86_64"
    key = (system, "any" if system == "win32" else machine)
    if key not in BUILDS:
        sys.exit("get-toolchain: no build for %s/%s -- use --url" % key)
    return "arm-gnu-toolchain-%s-%s" % (version, BUILDS[key])


def report(done, total):
    if total > 0:
        sys.stdout.write("\r  %5.1f MB of %5.1f MB (%2d%%)"
                         % (done / 1e6, total / 1e6, 100 * done // total))
    else:
        sys.stdout.write("\r  %5.1f MB" % (done / 1e6))
    sys.stdout.flush()


def fetch(url, path):
    print("fetching %s" % url)
    with urllib.request.urlopen(url) as src, open(path, "wb") as dst:
        total = int(src.headers.get("Content-Length", 0))
        done = 0
        while True:
            chunk = src.read(1 << 20)
            if not chunk:
                break
            dst.write(chunk)
            done += len(chunk)
            report(done, total)
    print()


def unpack(path, into):
    """The archives hold one top directory; its contents become 'into'."""
    print("unpacking into %s" % into)
    tmp = into + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    if path.endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            z.extractall(tmp)
    else:
        with tarfile.open(path) as t:
            t.extractall(tmp)

    entries = os.listdir(tmp)
    top = os.path.join(tmp, entries[0]) if len(entries) == 1 else tmp
    shutil.rmtree(into, ignore_errors=True)
    shutil.move(top, into)
    shutil.rmtree(tmp, ignore_errors=True)

    if os.name != "nt":
        for root, _, files in os.walk(os.path.join(into, "bin")):
            for f in files:
                os.chmod(os.path.join(root, f), 0o755)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    into = os.path.join(root, "arduino", "toolchain")

    ap = argparse.ArgumentParser(description=__doc__,
             formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="14.2.rel1")
    ap.add_argument("--url", help="take the archive from here instead")
    ap.add_argument("--keep", action="store_true",
                    help="keep the downloaded archive")
    ap.add_argument("--force", action="store_true",
                    help="fetch again even if it is already there")
    args = ap.parse_args()

    gcc = os.path.join(into, "bin",
                       "arm-none-eabi-gcc" + (".exe" if os.name == "nt" else ""))
    if os.path.exists(gcc) and not args.force:
        print("the compiler is already there: %s" % gcc)
        return 0

    name = os.path.basename(args.url) if args.url else build_name(args.version)
    url = args.url or "%s/%s/binrel/%s" % (BASE, args.version, name)

    os.makedirs(os.path.dirname(into), exist_ok=True)
    tmpdir = tempfile.mkdtemp(prefix="gemtool")
    archive = os.path.join(tmpdir, name)
    try:
        fetch(url, archive)
        unpack(archive, into)
    finally:
        if args.keep:
            shutil.move(archive, os.path.join(root, name))
        shutil.rmtree(tmpdir, ignore_errors=True)

    if not os.path.exists(gcc):
        sys.exit("get-toolchain: %s is not there after unpacking" % gcc)
    print("done: %s" % gcc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
