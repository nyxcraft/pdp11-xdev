#!/usr/bin/env python3
# ovslice.py <orig> <workdir> -- slice a SYMBOL-BEARING overlay executable
# (0430/0431, relocation stripped) into per-window fake objects, each
# carrying its window's symbol subset shifted into window-local space.
#
# Overlay TEXT symbols carry the overlay number in type bits 8-11 (the
# 2.11 Newsym images number them explicitly; the older 0430 images have
# only the 0o400 flag, and ownership is sequential: the symtab is in ld
# input order, so a value reset among flagged symbols marks the next
# window).  The overlay-window VA and the data VA are DETECTED from the
# symbol values (the 2.11 kernel maps its overlay window inside kernel
# I-space and restarts data in D-space; userland uses the 8K boundary
# after base text).  N_FN and as-unproducible entries are link metadata
# (reinjected by ovsplice).  Newsym symtabs produce newsym fakes so
# 32-char names survive das | as -n.
import struct, sys, os

orig, work = sys.argv[1], sys.argv[2]
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
novl = sum(1 for x in ov if x)
maxov = max([x for x in ov if x] or [0])

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
        nm = d[r:r+8]
        ty, v = struct.unpack('<2H', d[r+8:r+12])
        syms.append((nm, ty, v))

# --- partition ---
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

# --- VA base detection ---
ovvals = [v for (nm, ty, v), w in zip(syms, wof) if w >= 1]
# candidates: the 8K boundary after base text (userland), or any 8K
# frame the values sit in (the 2.11 kernel maps the window inside
# I-space); vote -- a stray symbol below the window (jove) must not
# drag the base down, it survives via das's cast route
cands = {((t + 8191) // 8192) * 8192} | {(v // 8192) * 8192 for v in ovvals}
def ovfit(B):
    return sum(1 for v in ovvals if B <= v < B + maxov)
ovbase = max(sorted(cands), key=ovfit) if ovvals else ((t + 8191) // 8192) * 8192
datavals = [v for (nm, ty, v), w in zip(syms, wof)
            if w == -1 and (ty & 0o37) in (3, 4)]
cand = [((ovbase + maxov + 8191) // 8192) * 8192, 0]
def fits(B):
    return sum(1 for v in datavals if B <= v < B + da + bs + 2)
database = max(cand, key=fits) if datavals else cand[0]

# --- window geometry ---
wins = [('base', 0, t, hdr)]
off = hdr + t
k = 0
for s in ov:
    if not s: continue
    k += 1
    wins.append((f'ov{k:02d}', ovbase, s, off))
    off += s
datafoff = off

def emit(name, text, data, entries, wbs=0):
    if NEW:
        strtab = bytearray(); ents = b''
        strx = 4
        for nm, ty, v in entries:
            if nm:
                ents += struct.pack('<4H', strx >> 16, strx & 0xffff, ty, v)
                strtab += nm + b'\0'; strx += len(nm) + 1
            else:
                ents += struct.pack('<4H', 0, 0, ty, v)
        tl = 4 + len(strtab)
        stb = struct.pack('<2H', tl >> 16, tl & 0xffff) + bytes(strtab)
        open(os.path.join(work, name), 'wb').write(
            struct.pack('<8H', 0o407, len(text), len(data), wbs, len(ents), 0, 0, 1)
            + text + data + ents + stb)
    else:
        ents = b''.join(nm + struct.pack('<2H', ty, v) for nm, ty, v in entries)
        open(os.path.join(work, name), 'wb').write(
            struct.pack('<8H', 0o407, len(text), len(data), wbs, len(ents), 0, 0, 1)
            + text + data + ents)

for wi, (wn, vabase, size, foff) in enumerate(wins):
    entries = []
    for (nm, ty, v), w in zip(syms, wof):
        if w != wi: continue
        entries.append((nm, ty & 0o77, (v - vabase) & 0xffff))
    emit(f'w_{wi:02d}{wn}.bin', d[foff:foff+size], b'', entries)

entries = []
for (nm, ty, v), w in zip(syms, wof):
    if w != -1: continue
    bt = ty & 0o37
    nv = (v - database) & 0xffff if bt in (3, 4) else v
    entries.append((nm, ty & 0o77, nv))
emit('w_99data.bin', b'', d[datafoff:datafoff+da], entries, wbs=bs)

open(os.path.join(work, 'ovmeta'), 'w').write(
    '%d %d %d %d %d %d\n' % (hdr, ovbase, database, datafoff + da, novl, 1 if NEW else 0))
