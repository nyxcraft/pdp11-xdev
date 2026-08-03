#!/usr/bin/env python3
"""mkcursesa.py DIR OUT -- assemble a byte-exact 2.9BSD libcurses.a.

Our ${PREFIX}-ar / -ranlib write a modern little-endian archive; the shipped
2.9 libcurses.a is old-2BSD format (magic 0xff65, 26-byte member headers with
PDP-11 middle-endian date/size longs, an old-style __.SYMDEF of
{char name[8]; long off}, odd members padded with a space).  It also archives
the curses headers (curses.h/unctrl.h/cr_ex.h/curses.ext) ahead of the objects.

This builds that exact archive from OUR compiled objects (verified byte-
identical to the shipped members) and the source headers.  The per-member
metadata below -- link/member order, era timestamps, uid/gid/mode -- is
reproduction data taken from the shipped ~/bsd/2.9/usr/lib/libcurses.a (it is
build provenance, not derivable from source, like the roguestrings seed).
The result is byte-for-byte identical to the shipped library.
"""
import struct, sys, os

# (name, date, uid, gid, mode) in archive order -- from the shipped 2.9 lib.
META = [
    ("__.SYMDEF", 417611306, 11, 3, 0o100664),
    ("curses.h",  402113840,  3, 3, 0o100660),
    ("unctrl.h",  362022676,  3, 3, 0o100660),
    ("cr_ex.h",   362022669,  3, 3, 0o100660),
    ("curses.ext",362022670,  3, 3, 0o100660),
    ("box.o",     417610828, 11, 3, 0o100664),
    ("clear.o",   417610840, 11, 3, 0o100664),
    ("initscr.o", 417610853, 11, 3, 0o100664),
    ("endwin.o",  417610865, 11, 3, 0o100664),
    ("mvprintw.o",417610876, 11, 3, 0o100664),
    ("mvscanw.o", 417610888, 11, 3, 0o100664),
    ("mvwin.o",   417610899, 11, 3, 0o100664),
    ("newwin.o",  417610918, 11, 3, 0o100664),
    ("overlay.o", 417610932, 11, 3, 0o100664),
    ("overwrite.o",417610944,11, 3, 0o100664),
    ("printw.o",  417610956, 11, 3, 0o100664),
    ("scanw.o",   417610968, 11, 3, 0o100664),
    ("refresh.o", 417610997, 11, 3, 0o100664),
    ("touchwin.o",417611008, 11, 3, 0o100664),
    ("erase.o",   417611021, 11, 3, 0o100664),
    ("clrtobot.o",417611034, 11, 3, 0o100664),
    ("clrtoeol.o",417611047, 11, 3, 0o100664),
    ("cr_put.o",  417611075, 11, 3, 0o100664),
    ("cr_tty.o",  417611093, 11, 3, 0o100664),
    ("longname.o",417611101, 11, 3, 0o100664),
    ("delwin.o",  417611112, 11, 3, 0o100664),
    ("insertln.o",417611125, 11, 3, 0o100664),
    ("deleteln.o",417611137, 11, 3, 0o100664),
    ("scroll.o",  417611149, 11, 3, 0o100664),
    ("getstr.o",  417611160, 11, 3, 0o100664),
    ("getch.o",   417611173, 11, 3, 0o100664),
    ("addstr.o",  417611183, 11, 3, 0o100664),
    ("addch.o",   417611198, 11, 3, 0o100664),
    ("move.o",    417611208, 11, 3, 0o100664),
    ("curses.o",  417611220, 11, 3, 0o100775),
    ("unctrl.o",  417611232, 11, 3, 0o100775),
    ("standout.o",417611243, 11, 3, 0o100664),
    ("tstp.o",    417611255, 11, 3, 0o100664),
    ("insch.o",   417611267, 11, 3, 0o100664),
    ("delch.o",   417611279, 11, 3, 0o100664),
]

def mkpl(v):                     # PDP-11 middle-endian long
    return struct.pack("<HH", (v >> 16) & 0xffff, v & 0xffff)

def defined_globals(obj):        # 8-byte names of external text/data/bss defs
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
    content = {}
    for nm, *_ in META:
        if nm == "__.SYMDEF":
            continue
        content[nm] = open(os.path.join(d, nm), "rb").read()

    # __.SYMDEF size is fixed by the symbol count; compute it, then the member
    # offsets, then fill the ran_off fields.
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

    out = bytearray(b"\x65\xff")                       # ARMAG 0xff65
    for nm, date, uid, gid, mode in META:
        data = content[nm]
        out += nm.encode().ljust(14, b"\0") + mkpl(date) + bytes([uid, gid])
        out += struct.pack("<H", mode) + mkpl(len(data)) + data
        if len(data) & 1:
            out += b"\x20"                             # odd members padded with space
    open(out_path, "wb").write(out)

if __name__ == "__main__":
    main()
