#!/usr/bin/env python3
"""rsc_add_menuitem.py - add a menu item to a GEM resource (.rsc + .def)

What one would otherwise do with a resource editor on an Atari: insert a
G_STRING menu entry into a menu, right after an existing item, and give it
a name in the definition file.  Old-style (non-TOS4) big-endian RSC files
only, the format of pTOS' desk/desktop.rsc.

    rsc_add_menuitem.py <base> <after-item> <new-name> <text>

e.g.  rsc_add_menuitem.py desk/desktop RESITEM TOUCITEM "  Touch calibration..."

The new object goes to the end of the menu tree's object range, so that
no existing object changes its index; in the menu it follows <after-item>,
the items below it move down one line and the drop-down box grows by one.
"""

import struct
import sys

HDR_FIELDS = ("vrsn object tedinfo iconblk bitblk frstr string imdata frimg "
              "trindex nobs ntree nted nib nbb nstring nimages rssize").split()
OFFSET_FIELDS = ("object", "tedinfo", "iconblk", "bitblk", "frstr",
                 "string", "imdata", "frimg", "trindex", "rssize")

G_BOX, G_TEXT, G_BOXTEXT, G_IMAGE = 20, 21, 22, 23
G_BUTTON, G_BOXCHAR, G_STRING, G_FTEXT = 26, 27, 28, 29
G_FBOXTEXT, G_ICON, G_TITLE, G_IBOX = 30, 31, 32, 25
SPEC_IS_OFFSET = {G_TEXT, G_BOXTEXT, G_IMAGE, G_BUTTON, G_STRING, G_FTEXT,
                  G_FBOXTEXT, G_ICON, G_TITLE}
LASTOB = 0x20
OBJ_SIZE = 24


class Rsc:
    def __init__(self, data):
        self.d = bytearray(data)
        self.h = dict(zip(HDR_FIELDS, struct.unpack_from(">18H", self.d, 0)))
        if self.h["vrsn"] & 0x04:
            sys.exit("rsc_add_menuitem: TOS4 resource format not supported")

    def save_header(self):
        struct.pack_into(">18H", self.d, 0, *[self.h[f] for f in HDR_FIELDS])

    def u16(self, off):
        return struct.unpack_from(">H", self.d, off)[0]

    def s16(self, off):
        return struct.unpack_from(">h", self.d, off)[0]

    def u32(self, off):
        return struct.unpack_from(">I", self.d, off)[0]

    def put16(self, off, v):
        struct.pack_into(">H", self.d, off, v & 0xffff)

    def put32(self, off, v):
        struct.pack_into(">I", self.d, off, v)

    # -- every place that holds a file offset --------------------------------

    def offset_slots(self):
        """(position, size) of every field in the file holding an offset"""
        h = self.h
        slots = []
        for i in range(h["ntree"]):
            slots.append((h["trindex"] + 4 * i, 4))
        for i in range(h["nstring"]):
            slots.append((h["frstr"] + 4 * i, 4))
        for i in range(h["nimages"]):
            slots.append((h["frimg"] + 4 * i, 4))
        for i in range(h["nted"]):
            base = h["tedinfo"] + 28 * i
            slots += [(base, 4), (base + 4, 4), (base + 8, 4)]
        for i in range(h["nib"]):
            base = h["iconblk"] + 34 * i
            slots += [(base, 4), (base + 4, 4), (base + 8, 4)]
        for i in range(h["nbb"]):
            slots.append((h["bitblk"] + 14 * i, 4))
        for i in range(h["nobs"]):
            base = h["object"] + OBJ_SIZE * i
            if (self.u16(base + 6) & 0xff) in SPEC_IS_OFFSET:
                slots.append((base + 12, 4))
        return slots

    def insert(self, at, blob):
        """insert blob at file offset 'at', fixing every offset >= at"""
        n = len(blob)
        slots = self.offset_slots()
        # offsets in the header are 16-bit
        for f in OFFSET_FIELDS:
            if self.h[f] >= at and not (f == "rssize"):
                self.h[f] += n
        self.h["rssize"] += n
        # fix the slots (their positions still refer to the old layout)
        for pos, size in slots:
            v = self.u32(pos)
            if v >= at:
                self.put32(pos, v + n)
        self.d[at:at] = blob
        self.save_header()


def load_def(path):
    d = open(path, "rb").read()
    entries = [bytearray(d[i:i + 16]) for i in range(0, len(d), 16)]
    return entries


def find_def(entries, name):
    for e in entries:
        if e[8:16].rstrip(b"\0").decode() == name:
            return e[4], e[5]       # tree, object
    sys.exit("rsc_add_menuitem: %s not in the definition file" % name)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    base, after, new_name, text = sys.argv[1:]
    if len(new_name) > 8:
        sys.exit("rsc_add_menuitem: names have at most 8 characters")

    rsc = Rsc(open(base + ".rsc", "rb").read())
    defs = load_def(base + ".def")
    tree, after_idx = find_def(defs, after)

    # 1. the text, at the end of the string area (just before image data)
    s = text.encode("ascii") + b"\0"
    if len(s) & 1:
        s += b"\0"
    string_at = rsc.h["imdata"]
    rsc.insert(string_at, s)

    # 2. the object, at the end of the menu tree's object range
    h = rsc.h
    roots = [rsc.u32(h["trindex"] + 4 * i) for i in range(h["ntree"])]
    root = roots[tree]
    following = sorted(r for r in roots if r > root)
    tree_end = following[0] if following else h["object"] + OBJ_SIZE * h["nobs"]
    new_idx = (tree_end - root) // OBJ_SIZE
    if new_idx > 127:
        sys.exit("rsc_add_menuitem: tree too large for the definition file")

    def obj(i):
        return root + OBJ_SIZE * i

    after_obj = obj(after_idx)
    # the parent (drop-down box): follow ob_next until it points upwards
    i = after_idx
    while True:
        nxt = rsc.s16(obj(i))
        if rsc.s16(obj(nxt) + 4) == i:      # nxt's tail is i: nxt is the parent
            parent = nxt
            break
        i = nxt

    # new object: a copy of <after> one line lower, linked in after it
    new = bytearray(rsc.d[after_obj:after_obj + OBJ_SIZE])
    struct.pack_into(">h", new, 0, rsc.s16(after_obj))       # ob_next
    struct.pack_into(">h", new, 2, -1)
    struct.pack_into(">h", new, 4, -1)
    struct.pack_into(">H", new, 8, rsc.u16(after_obj + 8) & ~LASTOB)
    struct.pack_into(">H", new, 10, 0)                        # ob_state: NORMAL
    struct.pack_into(">I", new, 12, string_at)                # ob_spec
    after_y = rsc.u16(after_obj + 18)
    struct.pack_into(">H", new, 18, after_y + 1)

    # items below <after> move down a line
    i = rsc.s16(after_obj)
    while i != parent:
        o = obj(i)
        rsc.put16(o + 18, rsc.u16(o + 18) + 1)
        i = rsc.s16(o)
    if rsc.s16(obj(parent) + 4) == after_idx:                 # was the last one
        rsc.put16(obj(parent) + 4, new_idx)
    rsc.put16(after_obj, new_idx)                             # after -> new
    rsc.put16(obj(parent) + 22, rsc.u16(obj(parent) + 22) + 1)  # box grows

    # LASTOB moves to the new object, the last one of the tree
    last = obj(new_idx - 1)
    if rsc.u16(last + 8) & LASTOB:
        rsc.put16(last + 8, rsc.u16(last + 8) & ~LASTOB)
        flags = struct.unpack_from(">H", new, 8)[0]
        struct.pack_into(">H", new, 8, flags | LASTOB)

    rsc.insert(tree_end, bytes(new))
    rsc.h["nobs"] += 1
    rsc.save_header()

    # 3. the name
    entry = bytearray(16)
    entry[4] = tree
    entry[5] = new_idx
    entry[6] = 1
    entry[7] = 0
    entry[8:8 + len(new_name)] = new_name.encode("ascii")
    # the count in the first entry covers the real entries; a record of
    # zeros after them ends the file
    count = struct.unpack_from(">H", defs[0], 0)[0]
    struct.pack_into(">H", defs[0], 0, count + 1)
    defs.insert(count, entry)

    open(base + ".rsc", "wb").write(rsc.d)
    open(base + ".def", "wb").write(b"".join(defs))
    print("%s: %s = object %d of tree %d, after %s" %
          (base, new_name, new_idx, tree, after))


if __name__ == "__main__":
    main()
