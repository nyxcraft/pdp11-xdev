#!/bin/sh
# ld-sweep.sh [TREE...] -- ld byte-match oracle: link 2.9 programs with OUR ld
# and the native /bin/ld (under apsim) on IDENTICAL inputs and byte-compare.
#
# Isolates ld the same way the other oracles isolate cc/as/ar/ranlib: both
# linkers get the same object files (compiled with our proven cc/as) and the same
# libraries, so any byte difference is ld's fault.  The acceptance metric is
# DIFFER == 0; the clean/partial/skip split is informational (partial = the link
# left undefined externals but both linkers still agreed to the byte; skip = our
# per-dir object-set heuristic didn't match the program -- multi-program dirs,
# grammar variants, etc. -- not an ld result).
#
# Libraries are BUILT here, not shipped: libc.a is compiled from 2.9 source via
# the distribution's own compall+mklib (our cc/as/ar/ranlib), 175 members, and the
# shipped libcurses/libtermcap/libm are re-ranlib'd (a fresh copy's mtime > the
# 1985 __.SYMDEF date, which ld would otherwise call "out of date" and then scan
# the archive directly -- choking on libcurses.a's non-object curses.h member).
#
# Needs sim/native/{ld,crt0.o} (extract-rootdump.py).  Link recipe mirrors cc(1):
# `ld -X crt0.o -o out OBJ... -lcurses -ltermlib -lm -lc' (extra libs are pulled
# only if referenced, so harmless for programs that don't use them).
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
APSIM="$BIN-apsim"; NAT="$HERE/native"; SRC="${SRC:-$HOME/bsd/2.9/usr/src}"
LIBSRC="$HOME/bsd/2.9/usr/src/lib/c"; SYSS="$HOME/bsd/2.9/usr/include/sys.s"
for f in ld crt0.o; do [ -f "$NAT/$f" ] || { echo "missing $NAT/$f -- run extract-rootdump.py"; exit 1; }; done

R="${CORPUS_WORK:-$HOME/.pdp11-corpus/ld.$$}"; rm -rf "$R"; mkdir -p "$R/bin" "$R/lib" "$R/tmp"
cp "$NAT/ld" "$R/bin/ld"; cp "$NAT/crt0.o" "$R/lib/crt0.o"
trap 'rm -rf "$R"' EXIT
XI="-I$SRC/cmd/uucp/LIBNDIR -I$SRC/cmd/mip -I$SRC/cmd/pcc -I$SRC/cmd/lint"
mkf(){ [ -f "$1/Makefile" ] && echo "$1/Makefile" || echo "$1/makefile"; }
dflags(){ sed 's/#.*//' "$(mkf "$1")" 2>/dev/null | grep -hoE '\-D[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?' | sort -u | tr '\n' ' '; }

# --- build the full 2.9 libc.a via the distribution's own compall + mklib ------
echo "building libc.a (compall + mklib, our cc/as/ar/ranlib)..."
LB="$R/libc"; rm -rf "$LB"; cp -a "$LIBSRC"/. "$LB/"; ( cd "$LB"
  while read -r line; do case "$line" in
    cc\ *) l=$(echo "$line"|sed "s#^cc #\"$BIN-cc\" #")
           # errlst.c's sys_errlist[] closing brace sits inside #ifdef UCB_NET
           case "$line" in *errlst.c*) l=$(echo "$l"|sed 's#-c #-c -DUCB_NET #');; esac
           eval "$l";;
    as\ *) eval "$(echo "$line"|sed "s#^as #\"$BIN-as\" #; s#/usr/include/sys.s#$SYSS#")";;
  esac; done < compall
  eval "$(grep -v '^$' mklib|tr -d '\\'|tr '\n' ' '|sed "s#^ar #\"$BIN-ar\" #")"
  "$BIN-ranlib" libc.a ) >/dev/null 2>&1
cp "$LB/libc.a" "$R/lib/libc.a"
for L in libcurses libtermcap libm; do cp "$HOME/bsd/2.9/usr/lib/$L.a" "$R/lib/$L.a" 2>/dev/null; done
(cd "$R/lib" && "$BIN-ranlib" libc.a lib*.a) 2>/dev/null
LIBS="libcurses.a libtermcap.a libm.a libc.a"
echo "  libc.a: $("$BIN-ar" t "$R/lib/libc.a" 2>/dev/null | grep -vc SYMDEF) members"

# link OBJ... under both linkers (cwd=$R/tmp which is also apsim /tmp); sets
# LDMATCH=1/0/-1 (match / differ / one-side-empty) and LDUNDEF=1 if undefined refs.
dolink(){ objs="$1"
  cp "$R/lib/crt0.o" "$R/tmp/crt0.o"   # (multi-source loop's `*.o' cleanup nukes it)
  rm -f "$R/tmp/our.out" "$R/tmp/nat.out"
  oe=$( (cd "$R/tmp" && "$BIN-ld" -X crt0.o -o our.out $objs $LIBS) 2>&1 )
  (cd "$R/tmp" && timeout 40 env APSIM_ROOT="$R" "$APSIM" "$R/bin/ld" -X crt0.o -o nat.out $objs $LIBS) 2>/dev/null
  echo "$oe" | grep -q Undefined && LDUNDEF=1 || LDUNDEF=0
  if [ -s "$R/tmp/our.out" ] && [ -s "$R/tmp/nat.out" ]; then
    cmp -s "$R/tmp/our.out" "$R/tmp/nat.out" && LDMATCH=1 || LDMATCH=0
  else LDMATCH=-1; fi; }

cp "$R/lib"/crt0.o "$R/lib"/*.a "$R/tmp/"
cm=0; pm=0; d=0; sk=0; dl=""

# --- single-source programs (top-level .c in each tree) ------------------------
for tree in "$@"; do
 for c in $(find "$SRC/$tree" -maxdepth 1 -name '*.c' 2>/dev/null | sort); do
  b=$(basename "$c" .c); rm -f "$R/tmp/$b.o"; cp "$c" "$R/tmp/$b.c"
  (cd "$R/tmp" && "$BIN-cc" -c -O -I"$SRC/$tree" "$b.c") 2>/dev/null; rm -f "$R/tmp/$b.c"
  [ -s "$R/tmp/$b.o" ] || { sk=$((sk+1)); continue; }
  dolink "$b.o"; rm -f "$R/tmp/$b.o"
  case $LDMATCH in 1) [ $LDUNDEF = 1 ] && pm=$((pm+1)) || cm=$((cm+1));;
                   0) d=$((d+1)); dl="$dl $tree/$b(1src)";; *) sk=$((sk+1));; esac
 done
done

# --- multi-source program dirs (compile every .c in the dir, link the set) -----
for tree in "$@"; do
 for dir in $(find "$SRC/$tree" -mindepth 1 -type d 2>/dev/null | sort); do
  ls "$dir"/*.c >/dev/null 2>&1 || continue
  { [ -f "$dir/Makefile" ] || [ -f "$dir/makefile" ]; } || continue
  echo "$dir" | grep -qE '/old|OLD_|\.old|oldcsh|syskludge' && continue
  find "$R/tmp" -maxdepth 1 -name '*.o' ! -name crt0.o -delete; ok=1; objs=""
  for c in "$dir"/*.c; do bn=$(basename "$c" .c)
    (cd "$R/tmp" && "$BIN-cc" -c -O $(dflags "$dir") -I"$dir" $XI "$c") 2>/dev/null
    [ -s "$R/tmp/$bn.o" ] && objs="$objs $bn.o" || ok=0; done
  [ "$ok" = 1 ] || { sk=$((sk+1)); find "$R/tmp" -maxdepth 1 -name '*.o' ! -name crt0.o -delete; continue; }
  dolink "$objs"; find "$R/tmp" -maxdepth 1 -name '*.o' ! -name crt0.o -delete
  case $LDMATCH in 1) [ $LDUNDEF = 1 ] && pm=$((pm+1)) || cm=$((cm+1));;
                   0) d=$((d+1)); dl="$dl ${dir#$SRC/}";; *) sk=$((sk+1));; esac
 done
done

echo "==== ld-sweep (our ld vs native /bin/ld under apsim) ===="
echo "  CLEAN-match: $cm   PARTIAL-match: $pm   DIFFER: $d   skip: $sk"
[ -n "$dl" ] && { echo "  DIFFER:"; for x in $dl; do echo "    $x"; done; }
[ "$d" = 0 ]
