#!/bin/sh
# das-kernels.sh -- FULL-FILE round-trips of the research-Unix kernels.
#
# Two classes:
#   - RELOCATABLE kernels (the 1972 pair): plain das -a | as, byte compare,
#     with the -6/-2 sysent personalities.
#   - RELOCATION-STRIPPED, symbol-bearing 0407 kernels (V4/V5/V6 unix:
#     ld kept symbols, dropped relocs): `das -y' keeps the symtab, ldnr.py
#     restores ld's symbol order (link metadata nothing in the file cites),
#     re-relativizes `ld -i' data/bss values, injects the N_FN input list,
#     and strips our relocation.  Every name/type/value must come from OUR
#     disassembly.  v6/unix additionally carries TEXT-typed symbols at
#     D-space addresses (interrupt-vector aliases); das spells those as
#     `name = value ^ donor' type-casts.
# The V7 kernels are 0411 (separate I&D: D-space overlaps text, addresses
# ambiguous) -- covered by the content tier in das-exec only.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
R="${CORPUS_WORK:-/tmp/dasker.$$}"; rm -rf "$R"; mkdir -p "$R"
[ -n "$KEEP" ] || trap 'rm -rf "$R"' EXIT

ok=0; fail=0; fl=""

# relocatable 1972 kernels: plain full-file round-trip
for K in "$HOME/unix/v1/unix72/fs/usr/boot/unix.out" \
         "$HOME/unix/v1/unix72/fs/usr/sys/a.out" \
         "$HOME/unix/v2/usr/sys/a.out"; do
  [ -f "$K" ] || continue
  done1=0
  for F in "" -6 -2; do
    "$BIN-das" -a -p $F "$K" > "$R/k.s" 2>/dev/null || continue
    AV=; head -1 "$R/k.s" | grep -q v7as && AV=-7
    head -1 "$R/k.s" | grep -q tab211 && AV="$AV --isa=bsd211"
    "$BIN-as" $AV -o "$R/k.o" "$R/k.s" 2>/dev/null
    [ -s "$R/k.o" ] || continue
    cmp -s "$K" "$R/k.o" && { done1=1; break; }
    # unnameable symtab entries (garbage-byte names das must blank) are
    # reinserted from the original with REXT index remapping
    python3 "$HERE/ldgn.py" "$R/k.o" "$K" "$R/k2.o" 2>/dev/null \
      && cmp -s "$K" "$R/k2.o" && { done1=1; break; }
  done
  if [ $done1 = 1 ]; then ok=$((ok+1)); else fail=$((fail+1)); fl="$fl $K"; fi
done

# relocation-stripped 0407 kernels: das -y + ldnr replay
for K in "$HOME/unix/v4/unix" "$HOME/unix/v5/unix" "$HOME/unix/v6/unix" \
         "$HOME/unix/v6/rkunix" "$HOME/unix/v6/rpunix" "$HOME/unix/v6/hpunix"; do
  [ -f "$K" ] || continue
  done1=0
  for F in "-y" "-y -6" "-y -2"; do
    "$BIN-das" -a -p $F "$K" > "$R/k.s" 2>/dev/null || continue
    AV=; head -1 "$R/k.s" | grep -q v7as && AV=-7
    head -1 "$R/k.s" | grep -q tab211 && AV="$AV --isa=bsd211"
    "$BIN-as" $AV -o "$R/k.o" "$R/k.s" 2>/dev/null
    [ -s "$R/k.o" ] || continue
    python3 "$HERE/ldnr.py" "$R/k.o" "$K" "$R/k2.o" 2>/dev/null || continue
    cmp -s "$K" "$R/k2.o" && { done1=1; break; }
  done
  if [ $done1 = 1 ]; then ok=$((ok+1)); else fail=$((fail+1)); fl="$fl $K"; fi
done

echo "==== das-kernels full-file round-trip ===="
echo "  OK: $ok   FAIL: $fail"
[ -n "$fl" ] && { echo "  FAIL:"; for x in $fl; do echo "    $x"; done; }
[ "$fail" = 0 ]
