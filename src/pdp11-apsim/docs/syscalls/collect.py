#!/usr/bin/env python3
"""Extract the PDP-11 syscall numbers for every universe apsim implements,
from each system's own authoritative table, and emit numbers.tsv.

apsim stands in for the kernel at the `sys` trap boundary, so a syscall
number has no meaning until you say *whose*: 2.9's vfork is 57, System
III's utssys is also 57.  This table is the ground truth for that
question -- the input `gaps.py` cross-checks apsim's dispatch against, and
the record `sweep.py` attributes unimplemented calls to.

The number lives in a different shape in each era, so there are two
readers (the VAX sibling of this script needs `set` and `define` too;
the PDP-11 line only needs these):

  sysent  the V5/V6/V7 and 2.10/2.11 kernel tables, where every row
          carries the number in a  /* N = name */  comment (the leading
          integers are argument counts, not the syscall number)
  equ     the 2.8/2.9 libc number header, PDP-11 assembler `name = N.`

Vendor trees are not committed (Caldera / Berkeley terms); this script
regenerates numbers.tsv + MANIFEST.tsv from them in place.  A missing
source is reported and skipped, so a partial checkout still produces a
partial table.
"""
import os, re, hashlib

HOME = os.path.expanduser("~")
HERE = os.path.dirname(os.path.abspath(__file__))

# (id, label, path, format) in chronological / inheritance order, so the
# ascending universe ids match the era order apsim's dispatch assumes.
SOURCES = [
    ("v5",     "Fifth Edition",   HOME + "/unix/v5/usr/sys/ken/sysent.c",                          "sysent"),
    ("v6",     "Sixth Edition",   HOME + "/unix/v6/usr/sys/ken/sysent.c",                          "sysent"),
    ("v7",     "Seventh Edition", HOME + "/unix/v7/usr/sys/sys/sysent.c",                          "sysent"),
    ("bsd28",  "2.8BSD",          HOME + "/pdp11-xdev/src/pdp11-libc/bsd28/libc/include/sys.s",    "equ"),
    ("bsd29",  "2.9BSD",          HOME + "/pdp11-xdev/src/pdp11-libc/bsd29/libc/include/sys.s",    "equ"),
    ("bsd210", "2.10BSD",         HOME + "/bsd/2.10/root/usr/src/sys/sys/init_sysent.c",           "sysent"),
    ("bsd211", "2.11BSD",         HOME + "/bsd/2.11/root/usr/sys/sys/init_sysent.c",               "sysent"),
]


def read_sysent(text):
    """Kernel sysent tables: the number is in a `/* N = name */` comment.
    (V5/V6 rows start with one arg-count field, V7 with two, 2.10/2.11
    with one -- none of which is the number; only the comment is.)"""
    out = {}
    for m in re.finditer(r"/\*\s*(\d+)\s*=\s*([\w /]+?)\s*\*/", text):
        out.setdefault(int(m.group(1)), m.group(2).split()[0])
    return out


def read_equ(text):
    """PDP-11 assembler number header: `name = N.` (the trailing dot is
    the 2BSD `as` decimal marker)."""
    out = {}
    for m in re.finditer(r"^\s*(\w+)\s*=\s*(\d+)\.?\s*$", text, re.M):
        out.setdefault(int(m.group(2)), m.group(1))
    return out


READERS = {"sysent": read_sysent, "equ": read_equ}


def main():
    rows = []
    manifest = []
    for sid, label, path, fmt in SOURCES:
        if not os.path.exists(path):
            print("MISSING %-8s %s" % (sid, path))
            continue
        raw = open(path, "rb").read()
        nums = READERS[fmt](raw.decode("latin-1"))
        for n, name in nums.items():
            rows.append((sid, n, name))
        disp = path.replace(HOME, "~")
        manifest.append((sid, label, fmt, len(nums),
                         hashlib.sha256(raw).hexdigest()[:16], disp))
        print("%-8s %-16s %s: %d numbers" % (sid, label, fmt, len(nums)))

    with open(HERE + "/numbers.tsv", "w") as fh:
        fh.write("universe\tnumber\tname\n")
        for sid, n, name in sorted(rows, key=lambda r: (r[0], r[1])):
            fh.write("%s\t%d\t%s\n" % (sid, n, name))
    with open(HERE + "/MANIFEST.tsv", "w") as fh:
        fh.write("universe\tlabel\tformat\tcount\tsha256_16\tsource\n")
        for row in manifest:
            fh.write("%s\t%s\t%s\t%d\t%s\t%s\n" % row)
    print("wrote numbers.tsv (%d rows), MANIFEST.tsv (%d sources)"
          % (len(rows), len(manifest)))


if __name__ == "__main__":
    main()
