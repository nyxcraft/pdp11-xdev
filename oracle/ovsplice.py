#!/usr/bin/env python3
# ovsplice.py <orig> <workdir> <out> -- reassemble a symbol-bearing
# overlay executable from the per-window `das | as' outputs (w_NN*.out
# in workdir, produced from ovslice.py fakes).  Every content byte and
# every symbol name/type/value is multiset-verified from OUR windows
# (values shifted back to their window VAs); the original supplies only
# link metadata: the header+overlay table, the symtab ORDER (and, for
# Newsym, the string-table layout), N_FN and as-unproducible entries,
# and any tail bytes.
import struct, sys, os
from collections import Counter

orig, work, outf = sys.argv[1], sys.argv[2], sys.argv[3]
d = open(orig, 'rb').read()
m = struct.unpack('<H', d[0:2])[0]
t, da, bs, sy, en, un, fl = struct.unpack('<7H', d[2:16])
def ovgeom(d, flen):
    # header layout by GEOMETRY, not magic: 2.9's /usr/70/ex is 0431
    # with the 0430-sized header (32 bytes, 7-slot table).  Prefer an
    # EXACT length (or Newsym strtab) match; images with tail bytes
    # (the 2.10 kernels' 168-byte appendage, ovadb) fall back to the
    # magic's own layout.
    t, da, bs, sy = struct.unpack('<4H', d[2:10])
    order = ((32, 7), (48, 15)) if struct.unpack('<H', d[0:2])[0] == 0o430 else ((48, 15), (32, 7))
    for hdr, n in order:
        if len(d) < hdr: continue
        ov = struct.unpack('<%dH' % n, d[18:18 + 2 * n])
        symoff = hdr + t + sum(ov) + da
        if symoff + sy == flen: return hdr, ov
        st = symoff + sy
        if sy % 8 == 0 and st + 4 <= flen:
            hi, lo = struct.unpack('<2H', d[st:st+4])
            if ((hi << 16) | lo) == flen - st: return hdr, ov
    hdr, n = order[0]
    ov = struct.unpack('<%dH' % n, d[18:18 + 2 * n])
    if hdr + t + sum(ov) + da + sy <= flen: return hdr, ov
    return None, None
hdr, ov = ovgeom(d, len(d))
if hdr is None:
    sys.stderr.write("unrecognized overlay geometry\n"); sys.exit(1)
maxov = max([x for x in ov if x] or [0])

meta = open(os.path.join(work, 'ovmeta')).read().split()
ovbase, database = int(meta[1]), int(meta[2])

IDCH = set(b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._~')
def unspellable(nm, ty, v):
    n = nm.split(b'\0')[0]
    if not n or any(c not in IDCH for c in n): return True
    if ty & ~(0o7400 | 0o77): return True
    if ty == 0 and v: return True
    return False

symoff = hdr + t + sum(ov) + da
def newfmt():
    st = symoff + sy
    if sy % 8 or st + 4 > len(d): return False
    hi, lo = struct.unpack('<2H', d[st:st+4])
    return ((hi << 16) | lo) == len(d) - st
NEW = newfmt()

syms = []
if NEW:
    st = symoff + sy
    for i in range(sy // 8):
        r = symoff + 8 * i
        hi, lo, ty, v = struct.unpack('<4H', d[r:r+8])
        strx = (hi << 16) | lo
        nm = d[st+strx:d.index(b'\0', st+strx)] if strx else b''
        syms.append((nm, ty, v))
else:
    for i in range(sy // 12):
        r = symoff + 12 * i
        syms.append((d[r:r+8], *struct.unpack('<2H', d[r+8:r+12])))

explicit = any((ty >> 8) & 0o17 > 1 for nm, ty, v in syms)
wof = []
cur = 0; prevov = -1
for nm, ty, v in syms:
    bt = ty & 0o37
    if bt == 0o37 or unspellable(nm, ty, v):
        wof.append(-2); continue
    if bt == 2:
        k = (ty >> 8) & 0o17
        if k:
            if not explicit:
                if v < prevov: cur += 1
                if cur == 0: cur = 1
                prevov = v
                k = cur
            wof.append(k)
        else:
            wof.append(0)
        continue
    wof.append(-1)

# old-format names compare padded to 8; newsym full
def okey(nm, ty, v):
    n = nm if NEW else nm.split(b'\0')[0][:8]
    return (n, ty & 0o77, v)

def rdout(path, vabase, isdata):
    o = open(path, 'rb').read()
    ot, oda, obs, osy = struct.unpack('<4H', o[2:10])
    ofl = struct.unpack('<H', o[14:16])[0]
    base = 16 + (ot + oda) * (1 if ofl else 2)
    ents = Counter()
    def nnew():
        st2 = base + osy
        if osy % 8 or st2 + 4 > len(o): return False
        hi, lo = struct.unpack('<2H', o[st2:st2+4])
        return ((hi << 16) | lo) == len(o) - st2
    if nnew():
        st2 = base + osy
        it = []
        for i in range(osy // 8):
            r = base + 8 * i
            hi, lo, ty, v = struct.unpack('<4H', o[r:r+8])
            strx = (hi << 16) | lo
            nm = o[st2+strx:o.index(b'\0', st2+strx)] if strx else b''
            it.append((nm, ty, v))
    else:
        it = []
        for i in range(osy // 12):
            r = base + 12 * i
            nm = o[r:r+8].split(b'\0')[0]
            ty, v = struct.unpack('<2H', o[r+8:r+12])
            it.append((nm, ty, v))
    for nm, ty, v in it:
        bt = ty & 0o37
        if isdata:
            nv = (v + database) & 0xffff if bt in (3, 4) else v
        else:
            nv = (v + vabase) & 0xffff if bt == 2 else v
        ents[(nm, ty & 0o77, nv)] += 1
    return o[16:16 + ot + oda], ents

outs = sorted(f for f in os.listdir(work) if f.startswith('w_') and f.endswith('.out'))
content = b''
wents = {}
k = 0
for f in outs:
    isdata = 'data' in f
    vabase = 0 if 'base' in f else ovbase
    c, e = rdout(os.path.join(work, f), vabase, isdata)
    content += c
    if isdata: wents[-1] = e
    elif 'base' in f: wents[0] = e
    else: k += 1; wents[k] = e

bad = 0
for (nm, ty, v), w in zip(syms, wof):
    if w == -2: continue
    key = okey(nm, ty, v)
    if wents.get(w, Counter())[key] > 0:
        wents[w][key] -= 1
    else:
        bad += 1
        if bad <= 5:
            sys.stderr.write("missing in window %s: %r ty=%o v=%o\n" % (w, nm, ty, v))
if bad:
    sys.stderr.write("total missing: %d\n" % bad)
    sys.exit(1)
extra = sum(c for e in wents.values() for c in e.values() if c > 0)
if extra:
    for e in wents.values():
        for kk, c in e.items():
            if c > 0: sys.stderr.write("ours-extra: %r\n" % (kk,))
    sys.stderr.write("total extra: %d\n" % extra)
    sys.exit(1)

# symtab bytes: every entry verified above, so the original's encoding
# (order, and for Newsym the string-table layout) is reproduced verbatim
open(outf, 'wb').write(d[0:hdr] + content + d[symoff:])
