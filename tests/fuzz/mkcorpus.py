#!/usr/bin/env python3
"""mkcorpus.py OUTDIR -- deterministic malformed a.out + archive corpus for the
object parsers (ld, ar, nm, size, strip, objcopy).

No randomness: the corpus is a pure function of this file, so a sanitizer hit
reproduces byte-for-byte.  The cases deliberately include the shapes the recent
hardening closed -- a __.SYMDEF with a bit-31 size (ld's signed tnum), a member
name that fills all 14 bytes (nm's %s over-read), oversized a_text/a_syms, and
truncations.
"""
import os, struct, sys

VALUES = [0, 1, 2, 0o405, 0o407, 0o410, 0o411, 0o430,
          0x3fff, 0x7fff, 0x8000, 0xfffe, 0xffff]
TRUNCS = [0, 1, 2, 8, 13, 15, 16, 17, 25, 26, 32, 51, 52]

ARMAG = 0o177545
OARMAG = 0o177555


def pdpl(v):			# 32-bit "middle-endian": high 16-bit word first
    v &= 0xffffffff
    return ((v & 0xffff) << 16) | (v >> 16)


def aout(magic, text=b'', data=b'', syms=b'', bss=0, flag=1):
    hdr = struct.pack('<8H', magic, len(text), len(data), bss,
                      len(syms), 0, 0, flag)
    return hdr + text + data + syms


def nlist(name, typ, val):
    return name.encode()[:8].ljust(8, b'\0') + struct.pack('<hH', typ, val)


def arhdr(name, size, date=0):	# 26-byte packed ar_hdr, longs stored PDPL
    return (name.encode()[:14].ljust(14, b'\0')
            + struct.pack('<I', pdpl(date)) + b'\4\4' + struct.pack('<h', 0o644)
            + struct.pack('<I', pdpl(size)))


def member(name, body, size=None):
    if size is None:
        size = len(body)
    pad = b'\0' if (len(body) & 1) else b''
    return arhdr(name, size) + body + pad


def main(out):
    os.makedirs(out, exist_ok=True)
    n = 0

    def put(name, data):
        nonlocal n
        open(os.path.join(out, name), 'wb').write(data)
        n += 1

    text = struct.pack('<HHHH', 0o012700, 0, 0o104401, 0)
    data = b'\1\2\3\4'
    syms = nlist('_main', 0o44, 0) + nlist('_x', 0o43, 8)

    # ---- a.out seeds (with a symbol table so nm/ld exercise symtab reads) ----
    seeds = {
        '407': aout(0o407, text, data, syms),
        '410': aout(0o410, text, data, syms),
        '411': aout(0o411, text, data, syms),
        '405': aout(0o405, b'\0' * 12 + text),		 # First Edition
        '430': aout(0o430, struct.pack('<8H', 64, 64, 0, 0, 0, 0, 0, 0) + text),
        'reloc': aout(0o407, text, data, syms, flag=0),	 # reloc region present
    }
    for tag, seed in seeds.items():
        put(f's-{tag}', seed)
        for w in range(8):				 # mutate each exec-header word
            for v in VALUES:
                b = bytearray(seed)
                struct.pack_into('<H', b, w * 2, v)
                put(f'h-{tag}-{w}-{v:04x}', bytes(b))
        for t in TRUNCS:
            put(f't-{tag}-{t}', seed[:t])

    # ---- archive seeds (ar/nm/ld/ranlib parse these) ------------------------
    obj = aout(0o407, text, data, syms)
    symdef = struct.pack('<8s I', b'_main\0\0\0', pdpl(26))	 # one ranlib entry
    good = struct.pack('<H', ARMAG) + member('__.SYMDEF', symdef) + member('a.o', obj)
    put('a-good', good)
    put('a-oldmag', struct.pack('<H', OARMAG) + member('v1.o', obj))

    # __.SYMDEF whose size is bit-31-set / huge / odd -> ld tnum, off arithmetic
    for tag, sz in [('neg', 0x80000018), ('huge', 0x7fffffff), ('odd', 27), ('zero', 0)]:
        a = struct.pack('<H', ARMAG) + member('__.SYMDEF', symdef, size=sz) + member('a.o', obj)
        put(f'a-symdef-{tag}', a)
    # a member name filling all 14 bytes (no NUL) -> nm %s over-read
    put('a-name14', struct.pack('<H', ARMAG) + member('ABCDEFGHIJKLMN', obj))
    # a member claiming a huge / bit-31 body size -> read/seek past EOF
    for tag, sz in [('huge', 0x7fffffff), ('neg', 0x80000010)]:
        put(f'a-msize-{tag}', struct.pack('<H', ARMAG) + arhdr('a.o', sz) + obj)
    for t in [2, 14, 20, 28, 40]:			 # archive truncation ladder
        put(f'a-trunc-{t}', good[:t])

    print(f'mkcorpus: {n} corpus files in {out}')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'corpus')
