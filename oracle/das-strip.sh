#!/bin/sh
# das-strip.sh [TREE] -- FULL-FILE round-trips of STRIPPED executables
# (a_syms == 0): `das -a -p | as', then ldst.py rebuilds the image --
# every content byte and the text/data/bss geometry from OUR object, the
# fields no assembly determines (magic, a_entry, a_unused, a_flag: V6 ld
# leaves it clear, some images carry junk words) copied from the original
# as link metadata.  Complements das-exec.sh (content tier, which also
# covers unstripped images whose symtabs are handled by das-kernels/
# das-sepid) by proving the whole file.
#
# 0405 (V1 12-byte header) qualifies when its symbol/relocation size
# words are zero; relocatable 0405s (unix.out) belong to das-kernels.
# Overlay (0430/0431) images stay with das-linked's per-window tier.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
TREE="${1:-$HOME/bsd/2.9}"
R="${CORPUS_WORK:-/tmp/dasstrip.$$}"; rm -rf "$R"; mkdir -p "$R"
[ -n "$KEEP" ] || trap 'rm -rf "$R"' EXIT

python3 - "$TREE" <<'PY' > "$R/list"
import os,struct,sys
for dp,_,fns in os.walk(sys.argv[1]):
    for fn in fns:
        p=os.path.join(dp,fn)
        if fn.endswith(('.o','.a')): continue
        try:
            if os.path.islink(p): continue
            d=open(p,'rb').read(16)
        except OSError: continue
        if len(d)<16: continue
        m,t,da,bs,sy,en,un,fl=struct.unpack('<8H',d)
        if m==0o405:
            # V1: a_text includes the 12-byte header; words 2/3 are
            # symbol and relocation sizes -- stripped means both zero
            if t>=12 and da==0 and bs==0 and os.path.getsize(p)==t: print(p)
            continue
        if m not in (0o407,0o410,0o411) or sy: continue
        if os.path.getsize(p)==16+t+da: print(p)
PY

ok=0; fail=0; fl=""
while read p; do
  OKF=0
  for SYSF in "" -6 -2; do
    "$BIN-das" -a -p $SYSF "$p" > "$R/e.s" 2>/dev/null || continue
    AV=; head -1 "$R/e.s" | grep -q nsym && AV="-n"
    "$BIN-as" $AV -o "$R/e.o" "$R/e.s" 2>/dev/null
    [ -s "$R/e.o" ] || continue
    python3 "$HERE/ldst.py" "$R/e.o" "$p" "$R/e.out" 2>/dev/null || continue
    cmp -s "$p" "$R/e.out" && { OKF=1; break; }
  done
  if [ $OKF = 1 ]; then ok=$((ok+1)); else fail=$((fail+1)); fl="$fl ${p#$TREE/}"; fi
done < "$R/list"

echo "==== das-strip full-file round-trip over $TREE ===="
echo "  OK: $ok   FAIL: $fail"
[ -n "$fl" ] && { echo "  FAIL:"; for x in $fl; do echo "    $x"; done; }
[ "$fail" = 0 ]
