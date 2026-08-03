#!/bin/sh
# das-stray.sh -- FULL-FILE round-trips of PDP-11 a.outs OUTSIDE the
# canonical trees: the strays hiding in VAX distributions (the chess
# blobs in six 4.x releases, Research V10's adb PDP-11 test fixture)
# and anything else that parses as a plausible PDP-11 image.  VAX
# a.outs are excluded by their long magic (zero high word where a
# PDP-11 a_text lives).  Ladder: plain | ldnr | ldst | ldr72 with the
# -6/-2 personalities.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
R="${CORPUS_WORK:-/tmp/dasstray.$$}"; rm -rf "$R"; mkdir -p "$R"
[ -n "$KEEP" ] || trap 'rm -rf "$R"' EXIT

python3 - <<'PY' > "$R/list"
import struct, os
HOME = os.path.expanduser('~')
CANON = set(os.path.join(HOME, t) for t in
            ['bsd/2.9', 'bsd/2.9pucc', 'bsd/2.10', 'bsd/2.11', 'bsd/2.8',
             'bsd/2.79', 'bsd/2bsd', 'bsd/1bsd', 'bsd/tuhs',
             'unix/v1', 'unix/v2', 'unix/v4', 'unix/v5', 'unix/v6', 'unix/v7'])
for root in (os.path.join(HOME, 'bsd'), os.path.join(HOME, 'unix')):
    for dp, dn, fn in os.walk(root):
        if any((dp + '/').startswith(t + '/') for t in CANON): continue
        for f in fn:
            p = os.path.join(dp, f)
            try:
                if os.path.islink(p) or os.path.getsize(p) < 16: continue
                d = open(p, 'rb').read(16)
            except Exception: continue
            m, t, da, bs, sy, en, un, fl = struct.unpack('<8H', d)
            if m not in (0o405, 0o407, 0o410, 0o411) or t == 0: continue
            sz = os.path.getsize(p)
            if m == 0o405:
                if not (t >= 12 and sz >= t): continue
            else:
                if 16 + t + da > sz: continue
                if not (sz in (16 + t + da, 16 + t + da + sy, 16 + 2*(t+da) + sy)
                        or (sy % 8 == 0 and sz > 16 + t + da + sy)): continue
            print(p)
PY

ok=0; fail=0; fl=""
while read -r p; do
  done1=0
  for F in "" "-6" "-2"; do
    "$BIN-das" -a -p $F "$p" > "$R/s.s" 2>/dev/null || continue
    AV=; head -1 "$R/s.s" | grep -q v7as && AV=-7
    head -1 "$R/s.s" | grep -q tab211 && AV="$AV --isa=bsd211"
    head -1 "$R/s.s" | grep -q ovas && AV=-V
    head -1 "$R/s.s" | grep -q nsym && AV="$AV -n"
    "$BIN-as" $AV -o "$R/s.o" "$R/s.s" 2>/dev/null
    [ -s "$R/s.o" ] || continue
    cmp -s "$p" "$R/s.o" && { done1=1; break; }
    python3 "$HERE/ldnr.py" "$R/s.o" "$p" "$R/s.2o" 2>/dev/null \
      && cmp -s "$p" "$R/s.2o" && { done1=1; break; }
    python3 "$HERE/ldst.py" "$R/s.o" "$p" "$R/s.2o" 2>/dev/null \
      && cmp -s "$p" "$R/s.2o" && { done1=1; break; }
    for M in "" "-k" "-X" "-x"; do
      python3 "$HERE/ldr72.py" "$R/s.o" "$p" "$R/s.2o" $M 2>/dev/null \
        && cmp -s "$p" "$R/s.2o" && { done1=1; break; }
    done
    [ $done1 = 1 ] && break
  done
  if [ $done1 = 1 ]; then ok=$((ok+1)); else fail=$((fail+1)); fl="$fl $p"; fi
done < "$R/list"

echo "==== das-stray (PDP-11 a.outs outside the canonical trees) ===="
echo "  OK: $ok   FAIL: $fail"
[ -n "$fl" ] && { echo "  FAIL:"; for x in $fl; do echo "    $x"; done; }
[ "$fail" = 0 ]
