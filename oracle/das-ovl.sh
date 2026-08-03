#!/bin/sh
# das-ovl.sh [TREE] -- FULL-FILE round-trips of OVERLAY executables
# (0430 MENLO_OVLY: 16-byte header + max_ovl + 7 overlay sizes; 0431
# 2.11-style: + 15 sizes, 48-byte header), stripped AND unstripped.
#
# ovslice.py cuts the image into per-window fake objects -- base text,
# each overlay, and the data window -- with each window's symbol subset
# shifted into window-local space (overlay ownership from type bits
# 8-11, or sequentially for 0430's bare 0o400 flag; window VAs detected
# from the values: userland maps overlays at the 8K boundary after base
# text, the 2.11 kernel inside kernel I-space with D-space data).  Each
# window round-trips through `das -a | as', and ovsplice.py reassembles
# the image: all content and symbols multiset-verified from OUR
# windows, the header/overlay table, symtab order, string-table layout,
# N_FN + as-unproducible entries, and tail bytes from the original as
# link metadata.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
TREE="${1:-$HOME/bsd/2.9}"
R="${CORPUS_WORK:-/tmp/dasovl.$$}"; rm -rf "$R"; mkdir -p "$R"
[ -n "$KEEP" ] || trap 'rm -rf "$R"' EXIT

python3 - "$TREE" <<'PY' > "$R/list"
import struct, os, sys
for dp, dn, fn in os.walk(sys.argv[1]):
    for f in fn:
        p = os.path.join(dp, f)
        try:
            if os.path.islink(p) or os.path.getsize(p) < 48: continue
            d = open(p, 'rb').read(48)
        except Exception: continue
        m = struct.unpack('<H', d[0:2])[0]
        if m not in (0o430, 0o431): continue
        t, da, bs, sy, en, un, fl = struct.unpack('<7H', d[2:16])
        if fl != 1: continue
        # accept either header geometry (usr/70/ex: 0431 magic, 0430
        # layout) -- ovslice probes by exact length
        flen = os.path.getsize(p)
        okg = 0
        for hdr, n in ((32, 7), (48, 15)):
            ov = struct.unpack('<%dH' % n, d[18:18 + 2 * n])
            if hdr + t + sum(ov) + da + sy <= flen: okg = 1
        if okg: print(p)
PY

ok=0; fail=0; fl=""
while read -r K; do
  rm -f "$R"/w_*.bin "$R"/w_*.out "$R"/ovmeta
  python3 "$HERE/ovslice.py" "$K" "$R" 2>/dev/null || { fail=$((fail+1)); fl="$fl ${K#$TREE/}"; continue; }
  OKW=1
  for W in "$R"/w_*.bin; do
    "$BIN-das" -a -p "$W" > "$R/w.s" 2>/dev/null || { OKW=0; break; }
    AV=; head -1 "$R/w.s" | grep -q v7as && AV=-7
    head -1 "$R/w.s" | grep -q tab211 && AV="$AV --isa=bsd211"
    head -1 "$R/w.s" | grep -q nsym && AV="$AV -n"
    "$BIN-as" $AV -o "${W%.bin}.out" "$R/w.s" 2>/dev/null || { OKW=0; break; }
    [ -s "${W%.bin}.out" ] || { OKW=0; break; }
  done
  if [ $OKW = 1 ] && python3 "$HERE/ovsplice.py" "$K" "$R" "$R/new" 2>/dev/null \
     && cmp -s "$K" "$R/new"
  then ok=$((ok+1)); else fail=$((fail+1)); fl="$fl ${K#$TREE/}"; fi
done < "$R/list"

echo "==== das-ovl (overlay executables full-file) over $TREE ===="
echo "  OK: $ok   FAIL: $fail"
[ -n "$fl" ] && { echo "  FAIL:"; for x in $fl; do echo "    $x"; done; }
[ "$fail" = 0 ]
