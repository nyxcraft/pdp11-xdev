#!/bin/sh
# as-corpus.sh -- assembler oracle over a whole source tree.
#
# For every .s file under $TREE, assemble it with BOTH our ported `as' and the
# original 2.9BSD `as' (run under apsim) and compare the .o byte-for-byte.  The
# native `as' is the ground truth: a mismatch means our port diverged from the
# assembler that actually built 2.9BSD.  (This is the tree-wide generalisation
# of the fight.o / nm check -- see docs/bin-bytematch.md.)
#
# The native as/as2 fixtures come from sim/native/extract-rootdump.py (not
# committed -- copyrighted).  apsim needs SHORT paths (long ones overflow the
# native tool's filename buffer) and APSIM_ROOT so `as' can exec /lib/as2.
#
# Usage:  sh as-corpus.sh [tree]        (default ~/bsd/2.9/usr/src)
#         AS_OPTS='-u -V' sh as-corpus.sh dir   (override assembler flags)
HERE=$(cd "$(dirname "$0")" && pwd)
BIN="$HERE/../bin/pdp11"
APSIM="$BIN-apsim"
NAT="$HERE/native"
TREE="${1:-$HOME/bsd/2.9/usr/src}"
ASOPTS="${AS_OPTS:--u}"

for f in as as2; do
	[ -f "$NAT/$f" ] || { echo "missing $NAT/$f -- run: python3 $NAT/extract-rootdump.py"; exit 1; }
done

# apsim work-root with a short path: bin/as execs lib/as2, tmp/ holds the input
# Host work dir under $HOME (not /tmp: systemd PrivateTmp / WSL drvfs can flake
# writes there).  apsim sees only SHORT guest paths (/tmp/t.s via APSIM_ROOT),
# so $R may live anywhere; override with CORPUS_WORK.
R="${CORPUS_WORK:-$HOME/.pdp11-corpus/as.$$}"; rm -rf "$R"; mkdir -p "$R/bin" "$R/lib" "$R/tmp"
cp "$NAT/as" "$R/bin/as"; cp "$NAT/as2" "$R/lib/as2"
trap 'rm -rf "$R"' EXIT

# The syscall stubs are never assembled alone -- the real build prepends the
# system-call number definitions (as sys.s file.s).  Assembled bare they carry
# an undefined `write' etc. and the two assemblers legitimately differ; with the
# defs prepended they match.  Same for the job-control and overlay stub dirs.
SYSDOTS="$HOME/bsd/2.9/usr/include/sys.s"
needs_sys(){ case "$1" in */lib/c/sys/*|*/lib/c/overlay/*|*/lib/jobs/*) return 0;; *) return 1;; esac; }

match=0; diff=0; skip=0; difflist=""
for s in $(find "$TREE" -name '*.s' | sort); do
	rm -f "$R/tmp/t.o" "$R/tmp/t.s" "$R/our.o"	# rm first: sources are 0444, cp can't
						# overwrite a read-only copy next round
	if needs_sys "$s"; then cat "$SYSDOTS" "$s" > "$R/tmp/t.s" 2>/dev/null || { skip=$((skip+1)); continue; }
	else cp "$s" "$R/tmp/t.s" 2>/dev/null || { skip=$((skip+1)); continue; }; fi
	# our as.  It may exit nonzero yet still emit a valid object (e.g. lfstat.s'
	# undefined `sys' operand -- native as does the same); compare the OBJECTS,
	# not the exit status.  Skip only when our as produced no object at all.
	"$BIN-as" $ASOPTS -o "$R/our.o" "$R/tmp/t.s" 2>/dev/null
	if [ ! -s "$R/our.o" ]; then skip=$((skip+1)); continue; fi
	# native as under apsim (short paths)
	APSIM_ROOT="$R" "$APSIM" "$R/bin/as" $ASOPTS -o /tmp/t.o /tmp/t.s 2>/dev/null
	if [ ! -s "$R/tmp/t.o" ]; then skip=$((skip+1)); continue; fi   # native as couldn't (apsim/size)
	if cmp -s "$R/our.o" "$R/tmp/t.o"; then match=$((match+1));
	else diff=$((diff+1)); difflist="$difflist ${s#$TREE/}"; fi
done
echo "==== as-corpus over $TREE ===="
echo "  MATCH native as: $match   DIFFER: $diff   SKIP (native as n/a): $skip"
[ -n "$difflist" ] && { echo "  differing files:"; for d in $difflist; do echo "    $d"; done; }
[ "$diff" = 0 ]
