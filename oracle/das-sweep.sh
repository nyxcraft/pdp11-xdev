#!/bin/sh
# das-sweep.sh -- das ROUND-TRIP oracle: disassemble each object with `das -a'
# (reassemblable source), reassemble with OUR as, and byte-compare to the
# original object.  A full round-trip (das -> as -> identical .o) is the
# strongest disassembler-fidelity test: it proves das recovered EVERY byte of
# the text, data, relocation AND symbol table (order included) losslessly.
#
# Corpus = the full 2.9 libc.a built from source via the distribution's own
# compall (our cc/as), 175 objects -- the same set ld-sweep links.  Objects are
# real c1 output, exercising the register/param decls, local labels, forward
# branches, external refs and per-word relocation that make round-trip hard.
#
# Reports, per object: OK (byte-identical), DIFF@<off> (reassembled but a byte
# differs -- prints first differing offset + a size note), FAIL (das or as
# errored / no output).  Metric = OK count; DIFF/FAIL are the work remaining.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
LIBSRC="$HOME/bsd/2.9/usr/src/lib/c"; SYSS="$HOME/bsd/2.9/usr/include/sys.s"
DAS="$BIN-das"; AS="$BIN-as"; CC="$BIN-cc"
R="${CORPUS_WORK:-$HOME/.pdp11-corpus/das.$$}"; rm -rf "$R"; mkdir -p "$R"
[ -n "$KEEP" ] || trap 'rm -rf "$R"' EXIT

# --- build the libc objects (compall, our cc/as) ------------------------------
LB="$R/libc"; cp -a "$LIBSRC"/. "$LB/"; ( cd "$LB"
  while read -r line; do case "$line" in
    cc\ *) l=$(echo "$line"|sed "s#^cc #\"$CC\" #")
           case "$line" in *errlst.c*) l=$(echo "$l"|sed 's#-c #-c -DUCB_NET #');; esac
           eval "$l";;
    as\ *) eval "$(echo "$line"|sed "s#^as #\"$AS\" #; s#/usr/include/sys.s#$SYSS#")";;
  esac; done < compall ) >/dev/null 2>&1

ok=0; diff=0; fail=0; tot=0; dl=""; fl=""
for o in "$LB"/*.o; do
  [ -f "$o" ] || continue
  b=$(basename "$o" .o); tot=$((tot+1))
  s="$R/$b.das.s"; ro="$R/$b.rt.o"
  if ! "$DAS" -a "$o" > "$s" 2>/dev/null; then fail=$((fail+1)); fl="$fl $b(das)"; continue; fi
  if ! (cd "$R" && "$AS" -o "$ro" "$s") 2>/dev/null || [ ! -s "$ro" ]; then
    fail=$((fail+1)); fl="$fl $b(as)"; continue; fi
  if cmp -s "$o" "$ro"; then ok=$((ok+1)); else
    diff=$((diff+1))
    off=$(cmp "$o" "$ro" 2>/dev/null | sed -n 's/.*differ: byte \([0-9]*\).*/\1/p')
    z1=$(stat -c%s "$o"); z2=$(stat -c%s "$ro")
    dl="$dl $b@$off${z1:+($z1/$z2)}"
  fi
done

echo "==== das round-trip (das -a | as == original .o), $LIBSRC ===="
echo "  OK: $ok   DIFF: $diff   FAIL: $fail   (of $tot)"
[ -n "$dl" ] && { echo "  DIFF (name@first-diff-byte(orig/rt size)):"; for x in $dl; do echo "    $x"; done; }
[ -n "$fl" ] && { echo "  FAIL:"; for x in $fl; do echo "    $x"; done; }
[ "$diff" = 0 ] && [ "$fail" = 0 ]
