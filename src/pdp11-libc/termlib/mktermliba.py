#!/usr/bin/env python3
"""mktermliba.py DIR OUT -- assemble a byte-exact 2.9BSD libtermlib.a.

Same old-2BSD archive format as the curses library (see libcurses/mkcursesa.py):
magic 0xff65, 26-byte member headers with PDP-11 middle-endian date/size longs,
old-style {char name[8]; long off} __.SYMDEF, odd members space-padded.
libtermlib is simpler -- just the three objects, no archived headers, uniform
uid/gid 0.  Built from OUR objects (byte-identical to the shipped members) plus
the shipped library's member metadata (order/timestamps/mode -- build
provenance from ~/bsd/2.9/usr/lib/libtermlib.a).  Result is byte-for-byte
identical to the shipped library (== libtermcap.a).
"""
import struct, sys, os

# (name, date, uid, gid, mode) in archive order -- from the shipped 2.9 lib.
META = [
    ("__.SYMDEF", 428909238, 0, 0, 0o100664),
    ("termcap.o", 428908238, 0, 0, 0o100664),
    ("tgoto.o",   428908251, 0, 0, 0o100664),
    ("tputs.o",   428908263, 0, 0, 0o100664),
]

def mkpl(v):
    return struct.pack("<HH", (v >> 16) & 0xffff, v & 0xffff)

def defined_globals(obj):
    e = struct.unpack("<8H", obj[:16])
    so = 16 + (e[1] + e[2]) * (2 if e[7] == 0 else 1)
    s = obj[so:so + e[4]]
    out = []
    for q in range(0, len(s), 12):
        ty = s[q + 8] | (s[q + 9] << 8)
        if ty & 0o40 and (ty & 0o37) in (2, 3, 4):
            out.append(s[q:q + 8])
    return out

def main():
    d, out_path = sys.argv[1], sys.argv[2]
    content = {nm: open(os.path.join(d, nm), "rb").read()
               for nm, *_ in META if nm != "__.SYMDEF"}

    ncount = sum(len(defined_globals(content[nm])) for nm, *_ in META if nm.endswith(".o"))
    symdef_size = ncount * 12

    cur, off = 2, {}
    for nm, *_ in META:
        size = symdef_size if nm == "__.SYMDEF" else len(content[nm])
        off[nm] = cur
        cur += 26 + size + (size & 1)

    symdef = bytearray()
    for nm, *_ in META:
        if nm.endswith(".o"):
            for s8 in defined_globals(content[nm]):
                symdef += s8[:8].ljust(8, b"\0") + mkpl(off[nm])
    assert len(symdef) == symdef_size
    content["__.SYMDEF"] = bytes(symdef)

    out = bytearray(b"\x65\xff")
    for nm, date, uid, gid, mode in META:
        data = content[nm]
        out += nm.encode().ljust(14, b"\0") + mkpl(date) + bytes([uid, gid])
        out += struct.pack("<H", mode) + mkpl(len(data)) + data
        if len(data) & 1:
            out += b"\x20"
    open(out_path, "wb").write(out)

if __name__ == "__main__":
    main()
