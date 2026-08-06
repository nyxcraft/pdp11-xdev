#!/usr/bin/env python3
"""mkfuzz.py OUTDIR -- deterministic malformed-a.out corpus for the loader.

Seeds: one minimal runnable program per magic apsim loads (0405, 0407,
0410, 0411, 0430).  Mutations: every exec-header word crossed with a fixed
list of hostile values, every 0430 overlay-header word likewise, and a
truncation ladder over each seed.  No randomness -- the corpus is a pure
function of this file, so a sanitizer hit reproduces byte-for-byte.
"""
import os, struct, sys

VALUES = [0, 1, 2, 3, 0o405, 0o407, 0o410, 0o411, 0o430, 0o777,
          0x3fff, 0x7fff, 0x8000, 0xfffe, 0xffff]
TRUNCS = [0, 1, 2, 8, 15, 16, 17, 24, 31, 32, 33, 47, 48, 64, 100]

# text: mov $0,r0; sys exit  (+ padding so claimed sizes have bytes to eat)
TEXT = struct.pack('<HHHH', 0o012700, 0, 0o104401, 0) + b'\0' * 56


def exec_hdr(magic, text, data=0, bss=0, syms=0, entry=0, flag=1):
    return struct.pack('<8H', magic, text, data, bss, syms, entry, 0, flag)


def seeds():
    s = {}
    for m in (0o407, 0o410, 0o411):
        s[f'{m:o}'] = exec_hdr(m, len(TEXT)) + TEXT
    # 0430: exec + ovlhdr{max,ov_siz[7]} + base text + one overlay + data
    ov = struct.pack('<8H', 64, 64, 0, 0, 0, 0, 0, 0)
    s['430'] = exec_hdr(0o430, len(TEXT)) + ov + TEXT + b'\1' * 64
    # 0405 First Edition: the whole file loads at 040000
    s['405'] = exec_hdr(0o405, 12 + len(TEXT)) + TEXT
    return s


def main(out):
    os.makedirs(out, exist_ok=True)
    n = 0
    for name, seed in seeds().items():
        open(os.path.join(out, f's-{name}'), 'wb').write(seed)
        n += 1
        for word in range(8):                     # exec header words
            for v in VALUES:
                b = bytearray(seed)
                struct.pack_into('<H', b, word * 2, v)
                open(os.path.join(out, f'h-{name}-{word}-{v:04x}'), 'wb').write(b)
                n += 1
        if name == '430':
            for word in range(8):                 # overlay header words
                for v in VALUES:
                    b = bytearray(seed)
                    struct.pack_into('<H', b, 16 + word * 2, v)
                    open(os.path.join(out, f'o-{word}-{v:04x}'), 'wb').write(b)
                    n += 1
        for t in TRUNCS:                          # truncation ladder
            open(os.path.join(out, f't-{name}-{t}'), 'wb').write(seed[:t])
            n += 1
    print(f'mkfuzz: {n} corpus files in {out}')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'fuzzwork')
