#!/bin/sh
# lib-sweep.sh [TREE] -- ar/ranlib byte-match oracle over every 2.9 library.
#
# Exercises our ar and ranlib the way the compiler oracle exercises cc/as:
#
#   ar     -- for each *.a: extract its object members, feed the SAME files to
#             our ar AND the native /bin/ar (under apsim), byte-compare the two
#             archives.  Member mtimes are zeroed first because apsim's stat(2)
#             reports mtime 0, while a host stat reports the real time -- that is
#             the ONLY field that would otherwise differ (apsim passes uid/gid/
#             mode/size through unchanged, verified).  So `touch -d @0' makes both
#             tools see identical metadata and the archives must match to the byte.
#
#   ranlib -- native ranlib inserts __.SYMDEF by shelling out (system("ar rlb")
#             via /bin/sh) and stamps that member's date with time()+5.  It runs
#             END TO END under apsim (fork/exec/wait + /bin/sh + /bin/ar), so our
#             ranlib and native ranlib each build their own archive's __.SYMDEF and
#             we byte-compare the member CONTENT (ar x drops the ar header, whose
#             time()+5 date is the only nondeterministic part).  The symbol table
#             -- names, archive offsets, layout -- must match to the byte.
#             (Requires the apsim lseek fix: lseek returns its long result in the
#             r0:r1 pair; without it ftell() is garbage and ranlib/nm truncate a
#             multi-member archive after the first member.)
#
# Needs sim/native/{ar,ranlib} (extract-rootdump.py).  ARMAG=0177545 archives
# only; a couple of *.a in the tree are not archives (csh.a is text, libfp.a is a
# raw a.out) and are reported as "non-archive" -- native ar rejects them too.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
APSIM="$BIN-apsim"; NAT="$HERE/native"; TREE="${1:-$HOME/bsd/2.9}"
for f in ar ranlib sh; do [ -f "$NAT/$f" ] || { echo "missing $NAT/$f -- run extract-rootdump.py"; exit 1; }; done

R="${CORPUS_WORK:-$HOME/.pdp11-corpus/lib.$$}"; rm -rf "$R"; mkdir -p "$R/bin"
# native archiver + ranlib + shell in the apsim root: ranlib system()s `ar rlb'
# via /bin/sh to insert __.SYMDEF, so all three must be present to run it end-to-end.
cp "$NAT/ar" "$R/bin/ar"; cp "$NAT/ranlib" "$R/bin/ranlib"; cp "$NAT/sh" "$R/bin/sh"
trap 'rm -rf "$R"' EXIT

# ARMAG 0177545 -> little-endian bytes 65 ff
is_archive(){ [ "$(od -An -tx1 -N2 "$1" 2>/dev/null | tr -d ' ')" = "65ff" ]; }

am=0; ad=0; adl=""; rm_=0; rd=0; rdl=""; na=0
for L in $(find "$TREE" -name '*.a' | sort); do
	is_archive "$L" || { na=$((na+1)); continue; }
	W="$R/w"; rm -rf "$W" "$R/tmp"; mkdir -p "$W" "$R/tmp"
	order=$("$BIN-ar" t "$L" 2>/dev/null | grep -v '^__\.SYMDEF$')
	[ -z "$order" ] && { na=$((na+1)); continue; }
	(cd "$W" && "$BIN-ar" x "$L" 2>/dev/null)
	for f in $order; do [ -f "$W/$f" ] && touch -d @0 "$W/$f"; done
	cp -a "$W/." "$R/tmp/"
	# --- ar: our vs native (apsim), identical inputs ---
	(cd "$R/tmp" && "$BIN-ar" cq "$R/tmp/our.a" $order 2>/dev/null)
	(cd "$R/tmp" && timeout 30 env APSIM_ROOT="$R" "$APSIM" "$R/bin/ar" cq /tmp/nat.a $order 2>/dev/null)
	if cmp -s "$R/tmp/our.a" "$R/tmp/nat.a"; then am=$((am+1))
	else ad=$((ad+1)); adl="$adl ${L#$TREE/}"; fi
	# --- ranlib: our ranlib vs native ranlib run END-TO-END under apsim ---
	# both ranlibs run on their own (byte-identical) archive; compare the
	# __.SYMDEF member CONTENT (ar x drops the header, whose date is time()+5 --
	# nondeterministic).  Native ranlib forks /bin/sh -c "ar rlb ...", so PATH=/bin.
	if "$BIN-ar" t "$L" 2>/dev/null | grep -q '^__\.SYMDEF$'; then
		(cd "$R/tmp" && "$BIN-ranlib" "$R/tmp/our.a" 2>/dev/null)
		(cd "$R/tmp" && timeout 60 env APSIM_ROOT="$R" PATH=/bin "$APSIM" "$R/bin/ranlib" /tmp/nat.a 2>/dev/null)
		(cd "$R/tmp" && "$BIN-ar" x our.a __.SYMDEF 2>/dev/null && mv __.SYMDEF our.symdef)
		(cd "$R/tmp" && "$BIN-ar" x nat.a __.SYMDEF 2>/dev/null && mv __.SYMDEF nat.symdef)
		if cmp -s "$R/tmp/our.symdef" "$R/tmp/nat.symdef"; then rm_=$((rm_+1))
		else rd=$((rd+1)); rdl="$rdl ${L#$TREE/}"; fi
	fi
done
echo "==== lib-sweep $TREE ===="
echo "  ar     MATCH: $am   DIFFER: $ad   (native ar under apsim)"
echo "  ranlib MATCH: $rm_   DIFFER: $rd   (__.SYMDEF vs native ranlib under apsim)"
echo "  non-archive .a skipped: $na"
[ -n "$adl" ] && { echo "  ar DIFFER:"; for x in $adl; do echo "    $x"; done; }
[ -n "$rdl" ] && { echo "  ranlib DIFFER:"; for x in $rdl; do echo "    $x"; done; }
[ "$ad" = 0 ] && [ "$rd" = 0 ]
