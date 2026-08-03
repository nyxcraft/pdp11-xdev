#!/usr/bin/env python3
"""
datacmp.py -- reloc-masked, link-order data comparison for the rogue
reconstruction.

The linked target's data segment is crt0's data, then each rogue .o's data in
link order, then library data.  Comparing the linked blobs directly is useless
because the 7 size-mismatched text functions shift addresses, so every POINTER
word in data differs (cascade noise).  This tool removes that noise: it walks
our build's .o data sections in link order, and for each word consults the .o's
own relocation records -- a relocated word is a pointer (masked); only the
LITERAL (absolute) data words are compared, against the target's data segment
(offset by crt0's data size).

Metric is matched-chunk growth, NOT size: it reports the first literal
divergence with .o attribution so a source-structure fix can extend the match.

V7 PDP-11 .o layout (a_flag==word8==0 => reloc present):
  16 hdr | text | data | textreloc(=text) | datareloc(=data) | syms | strs
  data word i is a pointer iff datareloc word i != 0.
"""
import os, struct, sys

RG = os.path.join(os.path.expanduser("~"), "rogue3.4")
BUILD = os.path.join(RG, "build")
CRT0 = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "lib", "bsd29", "crt0.o")
TARGET = os.path.join(RG, "rogue3.4.obj")
ORDER = ("vers main rooms passages command move fight chase init things potions "
         "scrolls wizard list io rip daemon daemons weapons monsters save options "
         "armor").split()


def load_o(path):
    """Return (data_bytes, [is_pointer per word])."""
    b = open(path, "rb").read()
    magic, text, data, bss, syms, entry, w7, flag = struct.unpack("<8H", b[:16])
    dstart = 16 + text
    dbytes = b[dstart:dstart + data]
    nwords = data // 2
    mask = [False] * nwords
    if flag == 0 and data:                 # relocation present
        drel = 16 + 2 * text + data        # after hdr+text+data+textreloc
        rel = b[drel:drel + data]
        for i in range(nwords):
            if i * 2 + 1 < len(rel):
                rw = rel[i * 2] | (rel[i * 2 + 1] << 8)
                mask[i] = (rw != 0)         # nonzero reloc word => pointer
    return dbytes, mask


def crt0_dsize():
    b = open(CRT0, "rb").read()
    return struct.unpack("<H", b[4:6])[0]


def target_data():
    b = open(TARGET, "rb").read()
    text, data = struct.unpack("<HH", b[2:6])
    return b[16 + text:16 + text + data]


def main():
    # our concatenated rogue .o data + pointer mask, in link order
    our = bytearray(); mask = []; bounds = []     # bounds: (name, start_word)
    for name in ORDER:
        p = os.path.join(BUILD, name + ".o")
        if not os.path.exists(p):
            print("  (skip missing %s.o)" % name); continue
        d, m = load_o(p)
        bounds.append((name, len(mask)))
        our += d; mask += m
    base = crt0_dsize()                            # rogue data starts here in target
    tgt = target_data()
    print("our rogue .o data: %d bytes (%d words); target data seg: %d bytes; "
          "rogue base in target: %d" % (len(our), len(mask), len(tgt), base))

    def owner(word_idx):
        nm = "?"
        for n, s in bounds:
            if word_idx >= s: nm = n
            else: break
        return nm

    # difflib alignment on the LITERAL word streams (pointers excluded) to see
    # the first real insertion/deletion -- i.e. what's extra or missing.
    import difflib
    def lit_words(words_data, words_mask, off=0):
        out = []
        for i in range(len(words_mask)):
            if words_mask[i]:
                continue
            o = off + 2*i
            if o+1 < (len(words_data) if off == 0 else len(tgt)):
                src = words_data if off == 0 else tgt
                out.append((i, src[o] | (src[o+1] << 8)))
        return out
    ours_lw = [(i, our[2*i] | (our[2*i+1] << 8)) for i in range(len(mask))
               if not mask[i] and 2*i+1 < len(our)]
    tgt_lw = [(i, tgt[base+2*i] | (tgt[base+2*i+1] << 8)) for i in range(len(mask))
              if not mask[i] and base+2*i+1 < len(tgt)]
    sm = difflib.SequenceMatcher(None, [v for _, v in ours_lw], [v for _, v in tgt_lw], autojunk=False)
    def decode(words):
        bs = bytearray()
        for v in words:
            bs.append(v & 0xff); bs.append((v >> 8) & 0xff)
        return "".join(chr(c) if 32 <= c < 127 else "." for c in bs)
    print("\n=== difflib alignment of literal words (first 3 non-equal chunks) ===")
    shown = 0
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        wi = ours_lw[i1][0] if i1 < len(ours_lw) else "?"
        print(" [%s] our-word~%s  ours=%r  tgt=%r" % (
            tag, wi, decode([v for _, v in ours_lw[i1:i2]])[:48],
            decode([v for _, v in tgt_lw[j1:j2]])[:48]))
        shown += 1
        if shown >= 3:
            break

    matched = 0; first = None; diffs = 0
    for i in range(len(mask)):
        ours = our[2*i] | (our[2*i+1] << 8) if 2*i+1 < len(our) else None
        toff = base + 2*i
        tw = tgt[toff] | (tgt[toff+1] << 8) if toff+1 < len(tgt) else None
        if mask[i]:                                # pointer -> skip (cascade noise)
            continue
        if ours is None or tw is None:
            break
        if ours == tw:
            matched += 1
            if first is None:
                last_match_word = i
        else:
            diffs += 1
            if first is None:
                first = i
                wstart = max(0, i - 4)
                print("\n=== FIRST literal divergence at word %d (%s.o), byte 0%o ===" %
                      (i, owner(i), 2*i))
                print("    matched %d literal words before it" % matched)
                print("    ours : " + " ".join("%06o" % ((our[2*j]|(our[2*j+1]<<8)) if not mask[j] else 0)
                                                 for j in range(wstart, i+5) if 2*j+1 < len(our)))
                print("    tgt  : " + " ".join("%06o" % ((tgt[base+2*j]|(tgt[base+2*j+1]<<8)) if not mask[j] else 0)
                                                 for j in range(wstart, i+5) if base+2*j+1 < len(tgt)))
                print("    mask : " + " ".join(("P" if mask[j] else ".") for j in range(wstart, i+5)))
                print("    at word %d, ours=%06o tgt=%06o" % (i, ours, tw))
    print("\nliteral words matched: %d, diverged: %d (first at word %s)"
          % (matched, diffs, first))


if __name__ == "__main__":
    main()
