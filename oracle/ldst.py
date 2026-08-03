#!/usr/bin/env python3
# ldst.py <ours.o> <orig> <out> -- STRIPPED-image replay (a_syms == 0).
#
# A stripped executable is header + text + data.  Every content byte and
# the text/data/bss geometry come from OUR reassembled object; the header
# fields no assembly determines -- the magic (ld -n/-i/-z), a_entry,
# a_unused, a_flag (V6 ld leaves it clear, some images carry junk there)
# -- are link metadata copied from the original, exactly as ldnr treats
# the symbol order.  Any bytes past header-implied EOF copy through.
#
# 0405 (V1 12-byte header, a_text INCLUDES the header): the whole 12-byte
# header is copied verbatim (its words 2/3 are symbol/relocation sizes,
# zero on a stripped image); our text must tile a_text-12 exactly.
import struct, sys

ours, orig, outf = sys.argv[1], sys.argv[2], sys.argv[3]
wd = open(ours, 'rb').read()
od = open(orig, 'rb').read()

wt, wda, wbs, wsy, wen, wun, wfl = struct.unpack('<7H', wd[2:16])
if wsy:
    sys.stderr.write("ours carries %d symtab bytes; not a stripped replay\n" % wsy)
    sys.exit(1)
content = wd[16:16 + wt + wda]

omagic = struct.unpack('<H', od[0:2])[0]
if omagic == 0o405:
    ot = struct.unpack('<H', od[2:4])[0]
    if wt + wda != ot - 12:
        sys.stderr.write("0405 text mismatch: ours %d vs a_text-12 %d\n"
                         % (wt + wda, ot - 12))
        sys.exit(1)
    open(outf, 'wb').write(od[0:12] + content + od[ot:])
    sys.exit(0)

ot, oda, obs, osy, oen, oun, ofl = struct.unpack('<7H', od[2:16])
if osy:
    sys.stderr.write("orig carries a symbol table; use ldnr.py\n")
    sys.exit(1)
hdr = struct.pack('<8H', omagic, wt, wda, wbs, 0, oen, oun, ofl)
open(outf, 'wb').write(hdr + content + od[16 + ot + oda:])
