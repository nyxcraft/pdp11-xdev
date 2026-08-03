#!/bin/sh
# kernel-link.sh -- the heaviest ld test: link the whole GENERIC kernel `unix'
# with OUR ld and the native /bin/ld (under apsim) and byte-compare.
#
# Exercises the OVERLAY linker (0430 a.out): `ld -X -n -o unix <CONFOBJ>
# -Z <seg1>... -Z <seg7> -L <base> vers.o param.o' -- 57 objects across seven
# overlay segments plus the resident base, exactly per sys/GENERIC/Makefile.
# Objects are built with our cc/as (kernel recipe: cc -S -O -DKERNEL -I GENERIC
# then as -V; locore l.s/mch.s get -DLOCORE + the config-generated assym.s
# prepended; param.c gets -DMAXUSERS=4).  Both linkers get the identical object
# set, so a byte difference is ld's.
#
# Needs sim/native/ld (extract-rootdump.py) and a configured GENERIC (assym.s,
# ioconf.c, l.s, c.c, param.c, vers.c present -- they are, in this tree).
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
APSIM="$BIN-apsim"; NAT="$HERE/native"; SRC="${SRC:-$HOME/bsd/2.9/usr/src}"
G="$SRC/sys/GENERIC"; SYS="$SRC/sys/sys"; DEV="$SRC/sys/dev"; INC="$SRC/include"
[ -f "$NAT/ld" ] || { echo "missing $NAT/ld -- run extract-rootdump.py"; exit 1; }
[ -f "$G/assym.s" ] || { echo "no $G/assym.s -- configure the GENERIC kernel first"; exit 1; }

R="${CORPUS_WORK:-$HOME/.pdp11-corpus/klink.$$}"; rm -rf "$R"; mkdir -p "$R/bin" "$R/o"
cp "$NAT/ld" "$R/bin/ld"; trap 'rm -rf "$R"' EXIT

# overlay segments + resident base + config objects, per sys/GENERIC/Makefile
OV1="sys2 sys4 fio pipe alloc iget"; OV2="text ureg malloc sys1 main mem"
OV3="bio dkleave subr rm machdep sys3 syslocal"; OV4="ttynew tty prim partab"
OV5="acct prf ioctl kl sys ttyold rk dz tm lp"; OV6="ts xp dh hs"
OV7="nami ht rp hk rl vp"; BASE="sig slp sysent clock trap rdwri dsort"
CONF="l mch c ioconf boot"; LOCORE=" l mch "

resolve(){ for d in "$G" "$SYS" "$DEV"; do for e in c s; do [ -f "$d/$1.$e" ] && { echo "$d/$1.$e"; return; }; done; done; }
built=0; fail=""
for o in $CONF $OV1 $OV2 $OV3 $OV4 $OV5 $OV6 $OV7 $BASE vers param; do
  src=$(resolve "$o"); [ -z "$src" ] && { fail="$fail $o(no-src)"; continue; }
  rm -f "$R/o/$o.o"
  case "$src" in
    *.c) mx=""; [ "$o" = param ] && mx="-DMAXUSERS=4"
         cp "$src" "$R/o/t.c"; (cd "$R/o" && "$BIN-cc" -O $mx -DKERNEL -I"$G" -I"$(dirname "$src")" -S t.c) 2>/dev/null
         [ -s "$R/o/t.s" ] && (cd "$R/o" && "$BIN-as" -V -o "$o.o" t.s) 2>/dev/null; rm -f "$R/o/t.c" "$R/o/t.s";;
    *.s) dl=""; case "$LOCORE" in *" $o "*) dl="-DLOCORE";; esac
         "$BIN-cpp" -P -DKERNEL $dl -I"$G" -I"$INC" "$src" > "$R/o/f.i" 2>/dev/null
         case "$LOCORE" in *" $o "*) cat "$G/assym.s" "$R/o/f.i" > "$R/o/u.s";; *) cp "$R/o/f.i" "$R/o/u.s";; esac
         (cd "$R/o" && "$BIN-as" -V -o "$o.o" u.s) 2>/dev/null; rm -f "$R/o/f.i" "$R/o/u.s";;
  esac
  [ -s "$R/o/$o.o" ] && built=$((built+1)) || fail="$fail $o"
done
echo "kernel objects built: $built/57${fail:+   FAILED:$fail}"
[ -n "$fail" ] && { echo "cannot link -- missing objects"; exit 1; }

oo(){ for x in $1; do printf '%s.o ' "$x"; done; }
LINK="-X -n -o unix $(oo "$CONF") -Z $(oo "$OV1") -Z $(oo "$OV2") -Z $(oo "$OV3") -Z $(oo "$OV4") -Z $(oo "$OV5") -Z $(oo "$OV6") -Z $(oo "$OV7") -L $(oo "$BASE") vers.o param.o"
(cd "$R/o" && "$BIN-ld" $LINK) 2>/dev/null
cp "$R/o"/*.o "$R/bin/../"; mkdir -p "$R/tmp"; cp "$R/o"/*.o "$R/tmp/"
(cd "$R/tmp" && timeout 90 env APSIM_ROOT="$R" "$APSIM" "$R/bin/ld" $LINK) 2>/dev/null

echo "==== kernel-link (GENERIC unix: our ld vs native ld/apsim) ===="
if [ -s "$R/o/unix" ] && [ -s "$R/tmp/unix" ]; then
  mg=$(od -An -tx1 -N2 "$R/o/unix" | tr -d ' ')
  if cmp -s "$R/o/unix" "$R/tmp/unix"; then
    echo "  unix BYTE-IDENTICAL ($(wc -c <"$R/o/unix") B, magic $mg = 0430 overlay) ✅"; exit 0
  else echo "  DIFFER:"; cmp "$R/o/unix" "$R/tmp/unix" | head; exit 1; fi
else echo "  link failed (our=$(wc -c <"$R/o/unix" 2>/dev/null) nat=$(wc -c <"$R/tmp/unix" 2>/dev/null))"; exit 1; fi
