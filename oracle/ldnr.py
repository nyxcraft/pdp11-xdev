#!/usr/bin/env python3
# ldnr.py <ours.o> <orig> <out>
#
# Replay a RELOCATION-STRIPPED, symbol-bearing 0407 kernel image (V4/V5/V6
# unix: ld kept the symbol table, dropped the relocation).  Our `das -y | as'
# object carries the same content and the same symbol SET, but as output has
# relocation (dropped here, relflg set) and first-mention symbol order (ld's
# per-input-file order is link metadata nothing in the file references --
# there are no relocation entries to cite it -- so it is restored from the
# original, like the N_FN member names).  Every symbol's name, type and
# value must come from OUR object: a mismatch in the multiset fails.
import struct, sys

ours, orig, outf = sys.argv[1], sys.argv[2], sys.argv[3]

def newfmt(d, base, sy):
    # 2.11 "Newsym": 8-byte entries {strx: PDP-11 long HIGH word first,
    # type, value}, then the string table whose first long is its total
    # length.  Detect by that length matching the remaining bytes.
    st = base + sy
    if sy % 8 or st + 4 > len(d): return False
    hi, lo = struct.unpack('<2H', d[st:st + 4])
    return ((hi << 16) | lo) == len(d) - st

def rd(f, hasrel):
    d = open(f, 'rb').read()
    t, da, bs, sy, en, un, fl = struct.unpack('<7H', d[2:16])
    base = 16 + (t + da) * (2 if (hasrel and not fl) else 1)
    syms = []
    if newfmt(d, base, sy):
        st = base + sy
        for i in range(sy // 8):
            r = base + 8 * i
            hi, lo, ty, v = struct.unpack('<4H', d[r:r + 8])
            strx = (hi << 16) | lo
            nm = d[st + strx:d.index(b'\0', st + strx)] if strx else b''
            syms.append((nm, (ty, v)))
        return d, (t, da, bs, sy, en, un, fl), syms, True
    for i in range(sy // 12):
        r = base + 12 * i
        syms.append((d[r:r + 8], struct.unpack('<2H', d[r + 8:r + 12])))
    return d, (t, da, bs, sy, en, un, fl), syms, False

wd, wh, wsyms, wnew = rd(ours, True)

# V1 0405 (First Edition a.out(V), 11/3/71): 12-byte header a_text
# INCLUDES; symtab (12-byte entries, V1 flags: 00 undef / 01 abs / 02
# register / 03 relocatable, |40 global) right after text; then the
# RELOCATION BITS -- a stream of 2-bit codes MSB-first in 16-bit words,
# one per text word (header excluded), zero-filled to the even area
# size.  das decodes the stream into its relocation model (translating
# the V1 symbol flags to modern base types); here the stream is
# RE-ENCODED from OUR object's parallel relocation and the flags are
# translated back -- everything is regenerated, nothing copied through
# but the 12-byte header (link metadata) and the symtab order.
if struct.unpack('<H', open(orig, 'rb').read(2))[0] == 0o405:
    od = open(orig, 'rb').read()
    ot, osy, orl = struct.unpack('<3H', od[2:8])
    osyms = [(od[r:r+8], struct.unpack('<2H', od[r+8:r+12]))
             for r in range(ot, ot + osy, 12)]
    wt, wda = wh[0], wh[1]
    if orl:
        # our symbol types are the das-translated modern ones: invert
        BACK = {0: 0, 1: 1, 0o24: 2, 2: 3, 4: 3}   # TEXT and the data
        # area (das maps beyond-text V1 type 3 to N_BSS) both fold back
        wsyms = [(nm, (BACK.get(tv[0] & 0o37, tv[0] & 0o37) | (tv[0] & 0o40), tv[1]))
                 for nm, tv in wsyms]
    from collections import Counter
    if Counter(wsyms) != Counter(osyms):
        oo = Counter(osyms) - Counter(wsyms); ww = Counter(wsyms) - Counter(osyms)
        sys.stderr.write("0405 symbol multiset mismatch: orig-only %d, ours-only %d\n"
                         % (sum(oo.values()), sum(ww.values())))
        for k in list(oo)[:5]: sys.stderr.write("  orig: %r\n" % (k,))
        for k in list(ww)[:5]: sys.stderr.write("  ours: %r\n" % (k,))
        sys.exit(1)
    ents = b''.join(nm + struct.pack('<2H', *tv) for nm, tv in osyms)
    if orl:
        # re-encode the V1 stream from OUR parallel relocation
        base = 16 + (wt + wda) * (2 if not wh[6] else 1)
        bits = []
        # parallel reloc words follow text+data in our .o
        roff = 16 + wt + wda
        for w in range(wt // 2):
            r = struct.unpack('<H', wd[roff + 2*w:roff + 2*w + 2])[0]
            k = r & 0o17
            if k == 0:      bits += [0, 0]
            elif k == 0o2:  bits += [0, 1]
            elif k == 0o11: bits += [1, 0]
            elif k == 0o10: bits += [1, 1, 0, 1]
            else:
                sys.stderr.write("0405: unencodable reloc %o at word %d\n" % (r, w))
                sys.exit(1)
        stream = bytearray()
        for i in range(0, len(bits), 16):
            chunk = bits[i:i+16] + [0] * (16 - len(bits[i:i+16]))
            v = 0
            for b in chunk: v = (v << 1) | b
            stream += struct.pack('<H', v)
        if len(stream) < orl: stream += b'\0' * (orl - len(stream))
        if len(stream) != orl:
            sys.stderr.write("0405: stream size %d != a_reloc %d\n" % (len(stream), orl))
            sys.exit(1)
        open(outf, 'wb').write(od[0:12] + wd[16:16 + wt + wda] + ents + bytes(stream))
    else:
        open(outf, 'wb').write(od[0:12] + wd[16:16 + wt + wda] + ents + od[ot + osy:])
    sys.exit(0)

od, oh, osyms, onew = rd(orig, False)

# `ld -i' images keep DATA/BSS symbol values data-space-relative; ours are
# unified -- re-relativize ours when the original is -i style.  0411
# (separate I&D) restarts D-space at 0 by definition -- always relativize.
t = wh[0]
omagic = struct.unpack('<H', od[0:2])[0]
def seg(tv): return tv[0] & 0o37
odd = [tv for _, tv in osyms if seg(tv) in (3, 4)]
delta = 0
if omagic == 0o411 or (omagic == 0o407 and odd
                       and sum(1 for tv in odd if tv[1] < oh[0]) > len(odd) // 2):
    delta = -t                       # D-space restarts at 0
elif omagic == 0o410:
    # shared text: data VA usually at the next 8K boundary after text --
    # but some images (m11.x) carry UNIFIED values; vote on the original
    g = ((t + 8191) // 8192) * 8192 - t
    va = sum(1 for tv in odd if tv[1] >= t + g)
    uni = sum(1 for tv in odd if t <= tv[1] < t + g)
    if va > uni: delta = g
if delta:
    wsyms = [(nm, (tv[0], (tv[1] + delta) & 0xffff) if seg(tv) in (3, 4) else tv)
             for nm, tv in wsyms]

# the original's N_FN filename entries are link metadata (the ld input
# list) -- inject them, like ldr72 does for members.  Entries as cannot
# PRODUCE (garbage-charset names, alien type bits, a non-ext UNDEF with
# a value: the lisp l1100.out symtab) ride through the same way -- das
# blanks them on read, the output symtab copies them from the original.
IDCH = set(b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._~')
def unspellable(nm, tv):
    ty, v = tv
    n = nm.split(b'\0')[0]
    if not n or any(c not in IDCH for c in n): return True
    if ty & ~0o77: return True
    if ty == 0 and v: return True
    return False
fns = [(nm, tv) for nm, tv in osyms if (tv[0] & 0o37) == 0o37]
osyms_cmp = [(nm, tv) for nm, tv in osyms
             if (tv[0] & 0o37) != 0o37 and not unspellable(nm, tv)]

from collections import Counter
if Counter(wsyms) != Counter(osyms_cmp):
    only_o = Counter(osyms_cmp) - Counter(wsyms)
    only_w = Counter(wsyms) - Counter(osyms_cmp)
    sys.stderr.write("symbol multiset mismatch: orig-only %d, ours-only %d\n"
                     % (sum(only_o.values()), sum(only_w.values())))
    for k in list(only_o)[:5]: sys.stderr.write("  orig: %r\n" % (k,))
    for k in list(only_w)[:5]: sys.stderr.write("  ours: %r\n" % (k,))
    sys.exit(1)

t, da, bs = wh[0], wh[1], wh[2]
if onew:
    # entries in orig ORDER with orig strx (string-table layout is link
    # metadata, like the order itself); every (name,type,value) is
    # multiset-verified against OUR object above.  The string table is
    # reproduced from the verified names at the orig offsets.
    obase = 16 + oh[0] + oh[1]
    ents = od[obase:obase + oh[3]]
    strtab = od[obase + oh[3]:]
    hdr = struct.pack('<8H', omagic, t, da, bs, oh[3], oh[4], oh[5], oh[6])
    open(outf, 'wb').write(hdr + wd[16:16 + t + da] + ents + strtab)
else:
    ents = b''.join(nm + struct.pack('<2H', *tv) for nm, tv in osyms)
    # bytes past the last whole entry -- a truncated trailing entry
    # inside a_syms, or data APPENDED to the image (the PUCC sendmail
    # carries a 64K appendage) -- are outside the a.out grammar:
    # copied through, like the symbol order itself.  a_flag copies
    # verbatim: V6-style ld leaves it CLEAR on no-reloc output
    # (PUCC's empty .comm-carrier ttyold.o).
    obase = 16 + oh[0] + oh[1] * (1 if oh[6] else 2)
    tail = od[obase + len(ents):]
    hdr = struct.pack('<8H', omagic, t, da, bs, oh[3], oh[4], oh[5], oh[6])
    open(outf, 'wb').write(hdr + wd[16:16 + t + da] + ents + tail)
