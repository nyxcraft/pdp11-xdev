#!/bin/sh
# kernel-sweep.sh -- assembler/compiler oracle for the KERNEL, which a generic
# sweep can't reach: its sources build only inside a configured kernel tree.
# Replicates the GENERIC config build (see sys/GENERIC/Makefile):
#
#   .c : cc -S -O -DKERNEL -I<GENERIC> -I<dir>          (config + device headers)
#   .s : cpp -P -DKERNEL [-DLOCORE] -I<GENERIC> -I<tree/usr/include> f.s > f.i
#        as -V -o f.o [assym.s] f.i                     (assym.s = config-generated
#                                                        struct offsets, prepended
#                                                        for the locore mch.s/l.s)
#
# then assembles the output with our as vs the native 2.9 as (apsim) and compares.
# GENERIC/ supplies whoami.h/localopts.h (config options) + the per-device count
# headers (ht.h/dz.h/...) + assym.s; the tree's /usr/include supplies the machine
# headers (sys/cpu.m, sys/iopage.m).  Device drivers GENERIC does not configure
# (rf/rx/... -- no count header) are skipped: native didn't build them here either.
#
# The kernel is built in OVERLAY mode (as -V, `ovas'): an undefined ref not
# .globl'd is overlay-resolved by name (n_type 0 + absolute reloc), which our as
# now handles (b889fbf), so all kernel .c match here.  Residual: mch.s (the
# machine locore -- a genuine remaining divergence, ~81B text/reloc + symtab) and
# GENERIC/l.s (3 bytes, config-specific: the same l.s matches under conf/).
#
# Needs sim/native/{as,as2} (extract-rootdump.py).  See docs/bin-bytematch.md.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
APSIM="$BIN-apsim"; NAT="$HERE/native"; SRC="${SRC:-$HOME/bsd/2.9/usr/src}"
G="$SRC/sys/GENERIC"; INC="$SRC/../include"
for f in as as2; do [ -f "$NAT/$f" ] || { echo "missing $NAT/$f -- run extract-rootdump.py"; exit 1; }; done
[ -f "$G/assym.s" ] || { echo "no $G/assym.s -- run 'make assym.s' in a configured kernel dir first"; exit 1; }
R="${CORPUS_WORK:-$HOME/.pdp11-corpus/ksweep.$$}"; rm -rf "$R"; mkdir -p "$R/bin" "$R/lib" "$R/tmp"
cp "$NAT/as" "$R/bin/as"; cp "$NAT/as2" "$R/lib/as2"; export TMPDIR="$R/tmp"
trap 'rm -rf "$R"' EXIT

m=0; d=0; cf=0; sk=0; big=0; dl=""
# assemble our.o vs native (-V, apsim) and tally; $2!="" => file tag for the diff list
cmpas(){ tag="$1"
	rm -f "$R/our.o" "$R/nat.o" "$R/tmp/t.o" "$R/tmp/x.s"	# fresh: never compare a stale native .o
	"$BIN-as" -V -o "$R/our.o" "$R/tmp/u.s" 2>/dev/null; cp "$R/tmp/u.s" "$R/tmp/x.s"
	timeout 60 env APSIM_ROOT="$R" "$APSIM" "$R/bin/as" -V -o /tmp/t.o /tmp/x.s 2>/dev/null; cp "$R/tmp/t.o" "$R/nat.o" 2>/dev/null
	if [ -s "$R/our.o" ] && [ -s "$R/nat.o" ]; then
		if cmp -s "$R/our.o" "$R/nat.o"; then m=$((m+1)); else d=$((d+1)); dl="$dl $tag"; fi
	elif [ -s "$R/our.o" ] && [ ! -s "$R/nat.o" ]; then big=$((big+1))	# native as over its 64KB (our as ok)
	else sk=$((sk+1)); fi
}

echo "== kernel .c (cc -S -O -DKERNEL -I GENERIC) =="
for c in $(find "$SRC/sys/sys" "$SRC/sys/dev" "$SRC/sys/autoconfig" "$SRC/sys/conf" -name '*.c' 2>/dev/null | grep -vE '/old/|/OLD_' | sort); do
	rm -f "$R/tmp/s.c" "$R/tmp/u.s"; cp "$c" "$R/tmp/s.c" 2>/dev/null || { cf=$((cf+1)); continue; }
	(cd "$R/tmp" && "$BIN-cc" -O -DKERNEL -I"$G" -I"$(dirname "$c")" -S s.c) 2>/dev/null
	[ -s "$R/tmp/s.s" ] || { cf=$((cf+1)); continue; }		# device GENERIC omits, etc.
	cp "$R/tmp/s.s" "$R/tmp/u.s"; cmpas "c:${c#$SRC/sys/}"
done
echo "== kernel .s (cpp -P -DKERNEL + assym.s + as -V) =="
# mch.s/l.s are locore -> prepend assym.s; boot.s + conf/*boot.s do not.
for spec in "$SRC/sys/sys/mch.s|1" "$G/l.s|1" "$SRC/sys/conf/l.s|1" "$G/boot.s|0" \
            $(for b in "$SRC"/sys/conf/*boot.s; do echo "$b|0"; done); do
	s=${spec%|*}; asym=${spec#*|}; [ -f "$s" ] || continue
	rm -f "$R/tmp/f.i" "$R/tmp/u.s"
	"$BIN-cpp" -P -DKERNEL -DLOCORE -I"$G" -I"$INC" "$s" > "$R/tmp/f.i" 2>/dev/null
	[ -s "$R/tmp/f.i" ] || { cf=$((cf+1)); continue; }
	if [ "$asym" = 1 ]; then cat "$G/assym.s" "$R/tmp/f.i" > "$R/tmp/u.s"; else cp "$R/tmp/f.i" "$R/tmp/u.s"; fi
	cmpas "s:${s#$SRC/sys/}"
done
echo "==== kernel-sweep ===="
echo "  MATCH native as: $m   DIFFER: $d   cc-fail/skip: $cf   as-skip: $sk   native-as-over-64KB (our as ok): $big"
[ -n "$dl" ] && { echo "  DIFFER:"; for x in $dl; do echo "    $x"; done; }
[ "$d" = 0 ]
