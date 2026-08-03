#!/bin/sh
# cc-corpus.sh -- compiler+assembler oracle over a source tree's .c files.
#
# For each .c under $TREE (compiled with $CFLAGS -- MUST match how the native
# build invoked cc, or you exercise the wrong #ifdef paths), two checks vs the
# original 2.9BSD tools run under apsim:
#
#   AS   : compile .c -> .s with OUR cc, then assemble that .s with our as AND
#          native as; compare .o.  Works for any file size -- this is the broad
#          assembler check on realistic compiler output.
#   CC   : feed the same preprocessed .i to our c0|c1|c2 and native c0|c1|c2,
#          assemble both, compare .o.  Bounded by apsim's ~64KB flat memory, so
#          large files show CC=skip (native c0/c1 can't run them) -- not a fail.
#
# Native fixtures from sim/native/extract-rootdump.py.  apsim needs SHORT paths
# + APSIM_ROOT (so as execs /lib/as2).  See docs/bin-bytematch.md.
#
# Usage:  CFLAGS='-O' sh cc-corpus.sh [tree]      (default ~/bsd/2.9/usr/src/lib/c)
HERE=$(cd "$(dirname "$0")" && pwd)
BIN="$HERE/../bin/pdp11"
APSIM="$BIN-apsim"; NAT="$HERE/native"
TREE="${1:-$HOME/bsd/2.9/usr/src/lib/c}"
CFLAGS="${CFLAGS:--O}"
for f in as as2 c0 c1 c2; do [ -f "$NAT/$f" ] || { echo "missing $NAT/$f -- run extract-rootdump.py"; exit 1; }; done

# Host work dir.  apsim only ever sees SHORT guest paths (/tmp/t.s via
# APSIM_ROOT), so $R itself may live anywhere -- default under $HOME (not /tmp,
# which can flake under heavy concurrent I/O); override with CORPUS_WORK.
R="${CORPUS_WORK:-$HOME/.pdp11-corpus/cc.$$}"; rm -rf "$R"; mkdir -p "$R/bin" "$R/lib" "$R/tmp"
export TMPDIR="$R/tmp"		# our cc scratches its pass files here (not flaky /tmp)
cp "$NAT/as" "$R/bin/as"; cp "$NAT/as2" "$R/lib/as2"
trap 'rm -rf "$R"' EXIT
nas(){ rm -f "$R/tmp/t.o" "$R/tmp/t.s"; cp "$1" "$R/tmp/t.s"; APSIM_ROOT="$R" "$APSIM" "$R/bin/as" -u -o /tmp/t.o /tmp/t.s 2>/dev/null; }

asm=0; asd=0; ccm=0; ccd=0; ccs=0; cfail=0; asdlist=""; ccdlist=""
# Resilient copy/compile: some hosts (WSL drvfs + Windows Defender, heavy
# concurrent I/O) throw transient EACCES on writes; retry so those don't get
# miscounted as compile failures.  rm -f the dest FIRST: the 2.9 sources are
# mode 0444, so cp gives the copy 0444 too, and cp cannot overwrite a read-only
# file (EACCES) on the next iteration -- removing it lets cp create it fresh.
cpr(){ rm -f "$2" 2>/dev/null; n=0; until cp "$1" "$2" 2>/dev/null; do n=$((n+1)); [ $n -gt 20 ] && return 1; sleep 0.5; done; }
for c in $(find "$TREE" -name '*.c' | sort); do
	# our cc -> .s.  We compile a copy in $R (not the read-only source tree),
	# so -I the .c's own directory to resolve its local `#include "foo.h"'.
	# cpr rides out transient EACCES on the copy; the compile itself is a single
	# attempt (a genuine compile failure -- e.g. vestigial OLD_*.c -- shouldn't
	# be retried).
	INCL="-I$(dirname "$c")"; ok=""
	for try in $(seq 1 ${CORPUS_TRIES:-1}); do
		cpr "$c" "$R/tmp/s.c" || continue
		rm -f "$R/tmp/s.s"
		(cd "$R/tmp" && "$BIN-cc" $CFLAGS $INCL -S s.c) 2>/dev/null
		[ -s "$R/tmp/s.s" ] && { ok=1; break; }
		sleep 1		# a transient-EACCES burst: wait it out (CORPUS_TRIES>1)
	done
	if [ -z "$ok" ]; then cfail=$((cfail+1)); rm -f "$R/tmp/s.s"; continue; fi
	# --- AS check: our as vs native as on the compiler output ---
	"$BIN-as" -u -o "$R/our.o" "$R/tmp/s.s" 2>/dev/null
	nas "$R/tmp/s.s"
	if [ -s "$R/tmp/t.o" ]; then
		if cmp -s "$R/our.o" "$R/tmp/t.o"; then asm=$((asm+1)); else asd=$((asd+1)); asdlist="$asdlist ${c#$TREE/}"; fi
	fi
	if [ -n "$AS_ONLY" ]; then rm -f "$R/tmp/s.s" "$R/tmp/s.c"; continue; fi
	# --- CC check: our c0/c1/c2 vs native (same .i), assembled with our as ---
	"$BIN-cpp" $CFLAGS -I"$(dirname "$c")" "$c" > "$R/tmp/i" 2>/dev/null
	"$BIN-c0" "$R/tmp/i" "$R/tmp/t1" "$R/tmp/t2" 2>/dev/null
	"$BIN-c1" "$R/tmp/t1" "$R/tmp/t2" "$R/tmp/oc1.s" 2>/dev/null
	tr -d '\0' < "$R/tmp/oc1.s" > "$R/tmp/oc1c.s"; "$BIN-c2" "$R/tmp/oc1c.s" "$R/tmp/oc2.s" 2>/dev/null || cp "$R/tmp/oc1c.s" "$R/tmp/oc2.s"
	cp "$R/tmp/i" "$R/tmp/i2"
	APSIM_ROOT="$R" "$APSIM" "$NAT/c0" /tmp/i2 /tmp/nt1 /tmp/nt2 2>/dev/null
	APSIM_ROOT="$R" "$APSIM" "$NAT/c1" /tmp/nt1 /tmp/nt2 /tmp/nc1.s 2>/dev/null
	if [ ! -s "$R/tmp/nc1.s" ]; then ccs=$((ccs+1));
	else
		tr -d '\0' < "$R/tmp/nc1.s" > "$R/tmp/nc1c.s"; APSIM_ROOT="$R" "$APSIM" "$NAT/c2" < "$R/tmp/nc1c.s" > "$R/tmp/nc2.s" 2>/dev/null
		"$BIN-as" -u -o "$R/oc.o" "$R/tmp/oc2.s" 2>/dev/null; "$BIN-as" -u -o "$R/nc.o" "$R/tmp/nc2.s" 2>/dev/null
		if cmp -s "$R/oc.o" "$R/nc.o"; then ccm=$((ccm+1)); else ccd=$((ccd+1)); ccdlist="$ccdlist ${c#$TREE/}"; fi
	fi
	rm -f "$R/tmp/s.s" "$R/tmp/s.c"
done
echo "==== cc-corpus over $TREE  (CFLAGS='$CFLAGS') ===="
echo "  AS (our as vs native as on cc output):  MATCH $asm   DIFFER $asd"
echo "  CC (our c0/c1/c2 vs native):            MATCH $ccm   DIFFER $ccd   SKIP(apsim size) $ccs"
echo "  cc could not compile: $cfail"
[ -n "$asdlist" ] && { echo "  AS differ:"; for d in $asdlist; do echo "    $d"; done; }
[ -n "$ccdlist" ] && { echo "  CC differ:"; for d in $ccdlist; do echo "    $d"; done; }
[ "$asd" = 0 ] && [ "$ccd" = 0 ]
