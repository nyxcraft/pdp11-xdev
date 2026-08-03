#!/bin/sh
# das-sepid.sh [TREE] -- FULL-FILE round-trips of UNSTRIPPED linked
# executables (relocation gone, symbol table kept): 0411 separate-I&D
# (the V7 kernels, adb, pascal, ex/Mail, the 2.11 ingres suite), 0410
# shared-text (w, zork -- data/bss symbol values are VAs from the next
# 8K boundary after text), and plain 0407 (wump).  Same replay as the
# stripped-kernel tier of das-kernels.sh: `das -y' keeps the symtab,
# as reassembles, ldnr.py restores ld's symbol order, re-relativizes
# the per-magic data/bss value convention, and strips our relocation.
# t+d+bss can exceed 64K; symtab values are 16-bit so the wrap is
# exact mod 2^16 (high bss symbols whose masked address lands outside
# bss go through das's `name = value ^ donor' casts).  ldnr also
# speaks the 2.11 Newsym
# string-table format (as -n on our side), copies through entries no
# assembly can produce (the lisp l1100.out's alien symtab: $-names,
# type bits outside EXT|BASE, undef-with-value) plus any bytes past
# the last whole symtab entry (the PUCC sendmail's 64K appendage).
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
TREE="${1:-$HOME/unix/v7}"
R="${CORPUS_WORK:-/tmp/dassep.$$}"; rm -rf "$R"; mkdir -p "$R"
[ -n "$KEEP" ] || trap 'rm -rf "$R"' EXIT

python3 - "$TREE" <<'PY' > "$R/list"
import struct, os, sys
for dp, dn, fn in os.walk(sys.argv[1]):
    for f in fn:
        p = os.path.join(dp, f)
        try:
            if os.path.islink(p) or os.path.getsize(p) < 16: continue
            d = open(p, 'rb').read(16)
        except Exception: continue
        m, t, da, bs, sy, en, un, fl = struct.unpack('<8H', d)
        if m == 0o405:
            # V1 12-byte header: a_text includes it; words 2/3 are the
            # symtab and relocation-bitmap sizes
            if t >= 12 and da > 0 and os.path.getsize(p) == t + da + bs: print(p)
            continue
        if m not in (0o407, 0o410, 0o411) or sy == 0: continue
        sz = os.path.getsize(p)
        # no-reloc geometry: plain (fl=1 honest or V6-style lying) or
        # 2.11 newsym (strtab, first long = its length, follows symtab)
        if sz == 16 + t + da + sy: print(p); continue
        if sy % 8 == 0 and sz > 16 + t + da + sy + 4:
            dd = open(p, 'rb').read(16 + t + da + sy + 4)
            st = 16 + t + da + sy
            tl = (struct.unpack('<H', dd[st:st+2])[0] << 16) | struct.unpack('<H', dd[st+2:st+4])[0]
            if tl >= 4 and st + tl in (sz, sz - 1): print(p)
PY

ok=0; fail=0; fl=""
while read -r K; do
  done1=0
  for F in "-y" "-y -6" "-y -2"; do
    "$BIN-das" -a -p $F "$K" > "$R/k.s" 2>/dev/null || continue
    AV=; head -1 "$R/k.s" | grep -q v7as && AV=-7
    head -1 "$R/k.s" | grep -q tab211 && AV="$AV --isa=bsd211"
    head -1 "$R/k.s" | grep -q nsym && AV="$AV -n"
    "$BIN-as" $AV -o "$R/k.o" "$R/k.s" 2>/dev/null
    [ -s "$R/k.o" ] || continue
    python3 "$HERE/ldnr.py" "$R/k.o" "$K" "$R/k2.o" 2>/dev/null || continue
    cmp -s "$K" "$R/k2.o" && { done1=1; break; }
  done
  if [ $done1 = 1 ]; then ok=$((ok+1)); else fail=$((fail+1)); fl="$fl ${K#$TREE/}"; fi
done < "$R/list"

echo "==== das-sepid (0411 separate-I&D full-file) over $TREE ===="
echo "  OK: $ok   FAIL: $fail"
[ -n "$fl" ] && { echo "  FAIL:"; for x in $fl; do echo "    $x"; done; }
[ "$fail" = 0 ]
