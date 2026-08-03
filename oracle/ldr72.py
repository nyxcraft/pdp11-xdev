#!/usr/bin/env python3
# ldr72.py <ours.o> <orig.o> <out.o> [-X|-x|-k]
#
# Emulate the early (V4-era, OLD 12-byte-symbol format) `ld -r file1.o
# file2.o ...' on OUR single reassembled object, using the ORIGINAL member
# only for the link RECIPE it records: its N_FN filename entries (names,
# values, and their positions among the locals stream).  ld -r output order
# is [fn1, locals1, fn2, locals2, ..., globals]; our das replay produces the
# same locals/globals relative orders, so injecting the fn entries at their
# recorded local-offsets and remapping the REXT relocation indices
# reconstructs the member.  -X strips L-prefixed locals, -x strips all
# locals, -k keeps all (plain ld -r).
import struct, sys

ours, orig, outf = sys.argv[1], sys.argv[2], sys.argv[3]
mode = sys.argv[4] if len(sys.argv) > 4 else '-k'

def rd(f):
    d = open(f, 'rb').read()
    t, da, bs, sy, en, un, fl = struct.unpack('<7H', d[2:16])
    base = 16 + (t + da) * (1 if fl else 2)
    syms = []
    for i in range(sy // 12):
        r = base + 12 * i
        nm = d[r:r + 8].split(b'\0')[0].decode('latin1')
        ty, v = struct.unpack('<2H', d[r + 8:r + 12])
        syms.append([nm, ty, v])
    return d, (t, da, bs, sy, en, un, fl), syms

od, oh, osyms = rd(orig)
wd, wh, wsyms = rd(ours)

# recipe: fn entries and how many locals precede each in the original
fns = []          # (locals_before, name, type, value)
nloc = 0
for nm, ty, v in osyms:
    if (ty & 0o37) == 0o37 and not (ty & 0o40):
        fns.append((nloc, nm, ty, v))
    elif not (ty & 0o40):
        nloc += 1

# partition ours (skip any fn already present)
locs, globs = [], []
for i, (nm, ty, v) in enumerate(wsyms):
    if (ty & 0o37) == 0o37 and not (ty & 0o40):
        continue
    if ty & 0o40:
        globs.append((i, [nm, ty, v]))
    else:
        if mode == '-x': continue
        if mode == '-X' and nm.startswith('L'): continue
        locs.append((i, [nm, ty, v]))

out = []
remap = {}
li = 0
for k, (nb, nm, ty, v) in enumerate(fns):
    while li < nb and li < len(locs):
        remap[locs[li][0]] = len(out); out.append(locs[li][1]); li += 1
    out.append([nm, ty, v])
while li < len(locs):
    remap[locs[li][0]] = len(out); out.append(locs[li][1]); li += 1
for i, s in globs:
    remap[i] = len(out); out.append(s)

t, da = wh[0], wh[1]
rel = bytearray(wd[16 + t + da:16 + t + da + t + da])
for off in range(0, len(rel), 2):
    w = rel[off] | (rel[off + 1] << 8)
    if (w & 0o16) == 0o10:
        oldi = w >> 4
        if oldi in remap:
            w = (remap[oldi] << 4) | (w & 0o17)
            rel[off] = w & 0xff; rel[off + 1] = w >> 8
ents = bytearray()
for nm, ty, v in out:
    ents += nm.encode('latin1')[:8].ljust(8, b'\0') + struct.pack('<2H', ty, v)
hdr = struct.pack('<8H', struct.unpack('<H', wd[0:2])[0], t, da, wh[2],
                  len(ents), wh[4], wh[5], wh[6])
open(outf, 'wb').write(hdr + wd[16:16 + t + da] + bytes(rel) + bytes(ents))
