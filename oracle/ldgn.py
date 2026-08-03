#!/usr/bin/env python3
# ldgn.py <ours.o> <orig> <out>
#
# Reinsert UNNAMEABLE symbol-table entries (names with bytes outside as's
# identifier charset -- das blanks them because no assembly can spell them;
# the 1972 unix.out kernel carries one such) at their original indices,
# remapping the REXT relocation indices in our object to match.  Everything
# else -- content, relocation types, every nameable symbol -- must come from
# OUR object in the SAME relative order.
import struct, sys

ours, orig, outf = sys.argv[1], sys.argv[2], sys.argv[3]

def rd(f):
    d = open(f, 'rb').read()
    t, da, bs, sy, en, un, fl = struct.unpack('<7H', d[2:16])
    base = 16 + (t + da) * (1 if fl else 2)
    syms = [d[base + 12 * i:base + 12 * (i + 1)] for i in range(sy // 12)]
    return d, (t, da, bs, sy, en, un, fl), syms

wd, wh, wsyms = rd(ours)
od, oh, osyms = rd(orig)

IDCH = set(b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._~')
def nameable(e):
    # das blanks (and this script reinserts) every entry as cannot
    # produce: garbage-charset names, alien type bits, non-ext UNDEF
    # with a value -- keep this predicate in step with das readsyms
    nm = e[:8].split(b'\0')[0]
    ty, v = struct.unpack('<2H', e[8:12])
    if not nm or any(c not in IDCH for c in nm): return False
    if ty & ~0o77: return False
    if ty == 0 and v: return False
    return True

bad = [(i, e) for i, e in enumerate(osyms) if not nameable(e)]
if len(wsyms) + len(bad) != len(osyms):
    sys.stderr.write("count mismatch: ours %d + bad %d != orig %d\n"
                     % (len(wsyms), len(bad), len(osyms)))
    sys.exit(1)

out = list(wsyms)
remap = {}
for i, e in bad:
    out.insert(i, e)
for newi, e in enumerate(out):
    pass
# old (ours) index -> new index: ours skipped the bad slots
oldi = 0
for newi in range(len(out)):
    if any(newi == i for i, _ in bad):
        continue
    remap[oldi] = newi
    oldi += 1

t, da, fl = wh[0], wh[1], wh[6]
rel = bytearray(wd[16 + t + da:16 + t + da + (0 if fl else t + da)])
for off in range(0, len(rel), 2):
    w = rel[off] | (rel[off + 1] << 8)
    if (w & 0o16) == 0o10:
        o2 = w >> 4
        if o2 in remap:
            w = (remap[o2] << 4) | (w & 0o17)
            rel[off] = w & 0xff; rel[off + 1] = w >> 8

hdr = struct.pack('<8H', struct.unpack('<H', wd[0:2])[0], t, da, wh[2],
                  12 * len(out), wh[4], wh[5], fl)
open(outf, 'wb').write(hdr + wd[16:16 + t + da] + bytes(rel) + b''.join(out))
