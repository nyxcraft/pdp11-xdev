#!/usr/bin/env python3
# ldxr211.py <in.o> <member-name> <out.o> [-x]
#
# Emulate 2.11BSD `ld -X -r member.o` (or `ld -x -r` with -x) on a SINGLE
# new-format (string-table) object: insert the N_FN filename symbol at slot 0,
# re-sort the symtab into [filename, locals, globals] (each block keeping its
# relative order), drop L-prefixed locals (-X) or all locals (-x), and remap
# the REXT relocation indices.  Text, data and relocation types are unchanged
# (a single input at base 0 relocates to itself).
import struct, sys

inf, member, outf = sys.argv[1], sys.argv[2], sys.argv[3]
strip_all = len(sys.argv) > 4 and sys.argv[4] == '-x'

d = open(inf, 'rb').read()
t, da, bs, sy, en, un, fl = struct.unpack('<7H', d[2:16])
base = 16 + (t + da) * (1 if fl else 2)
st = base + sy

syms = []
for i in range(sy // 8):
    r = base + 8 * i
    hi, lo, ty, v = struct.unpack('<4H', d[r:r + 8])
    sx = (hi << 16) | lo
    nm = d[st + sx:d.index(b'\0', st + sx)].decode('latin1')
    syms.append([nm, ty, v])

def isext(ty): return ty & 0o40

keep = []                       # (old_index, [nm,ty,v]) locals then globals
for i, s in enumerate(syms):    # locals block
    if isext(s[1]): continue
    if strip_all: continue
    if s[0].startswith('L'): continue
    keep.append((i, s))
nloc = len(keep)
for i, s in enumerate(syms):    # globals block
    if isext(s[1]): keep.append((i, s))

# `-x' strips all locals AND omits the filename symbol; `-X' keeps
# non-L locals and inserts the N_FN (matches 2.9/2.10 ld behavior)
fnhdr = [] if strip_all else [[member, 0o37, 0]]
out = fnhdr + [s for _, s in keep]
remap = {}
for new, (old, _) in enumerate(keep):
    remap[old] = new + len(fnhdr)

# remap REXT reloc indices
rel = bytearray(d[16 + t + da:16 + t + da + (0 if fl else t + da)])
for off in range(0, len(rel), 2):
    w = rel[off] | (rel[off + 1] << 8)
    if (w & 0o16) == 0o10:      # REXT
        old = w >> 4
        if old in remap:
            w = (remap[old] << 4) | (w & 0o17)
            rel[off] = w & 0xff; rel[off + 1] = w >> 8
    # an REXT citing a stripped local cannot occur (locals relocate internal)

# write
strtab = bytearray(4)
ents = bytearray()
for nm, ty, v in out:
    sx = len(strtab)
    strtab += nm.encode('latin1') + b'\0'
    ents += struct.pack('<4H', (sx >> 16) & 0xffff, sx & 0xffff, ty, v)
struct.pack_into('<2H', strtab, 0, (len(strtab) >> 16) & 0xffff, len(strtab) & 0xffff)

hdr = struct.pack('<8H', struct.unpack('<H', d[0:2])[0], t, da, bs, len(ents), en, un, fl)
open(outf, 'wb').write(hdr + d[16:16 + t + da] + bytes(rel) + bytes(ents) + bytes(strtab))
