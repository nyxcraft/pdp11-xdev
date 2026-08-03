#!/bin/sh
# s-sweep.sh [tree] -- assembler oracle over EVERY hand-written .s in a tree,
# assembled the way its Makefile actually invokes `as' (our as vs native as
# under apsim, compare .o).  Assembling a fragment alone leaves undefined
# cross-references, so this replicates the real build:
#
#   * multi-file UNITS (cmd/as, cmd/roff, lib/fpsim, ucb/pascal/px, ...) are
#     assembled as one `as f1.s f2.s ...' just like the Makefile;
#   * syscall stubs (lib/c/sys, lib/c/overlay, lib/jobs, and any .s that uses a
#     symbolic `sys NAME') get /usr/include/sys.s prepended;
#   * everything else is assembled individually (the implicit .s.o rule).
#
# Files needing a build-time GENERATED input (cmd/c/table.s via cvopt; ranm
# temp.s) or a special load origin (sys/mdec, sys/stand boot blocks: `..'
# relocation) are reported as KNOWN-EXCLUDED with a reason, not silent failures.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
APSIM="$BIN-apsim"; NAT="$HERE/native"; SRC="${SRC:-$HOME/bsd/2.9/usr/src}"
TREE="${1:-$SRC}"; SYS="$SRC/../include/sys.s"
for f in as as2; do [ -f "$NAT/$f" ] || { echo "missing $NAT/$f -- run extract-rootdump.py"; exit 1; }; done
R="${CORPUS_WORK:-$HOME/.pdp11-corpus/ssweep.$$}"; rm -rf "$R"; mkdir -p "$R/bin" "$R/lib" "$R/tmp"
cp "$NAT/as" "$R/bin/as"; cp "$NAT/as2" "$R/lib/as2"; trap 'rm -rf "$R"' EXIT

m=0; d=0; sk=0; ex=0; dl=""; done_list=" "
# assemble one unit (list of .s paths, sys.s prepended if $1=sys) and compare
cmp_unit(){ tag="$1"; pre="$2"; shift 2
	rm -f "$R/tmp/u.s" "$R/our.o" "$R/tmp/t.o" "$R/tmp/t.s"
	[ "$pre" = sys ] && cat "$SYS" > "$R/tmp/u.s"
	for x in "$@"; do cat "$x" >> "$R/tmp/u.s" 2>/dev/null || { sk=$((sk+1)); echo "  SKIP $tag (missing $x)"; return; }; done
	"$BIN-as" -u -o "$R/our.o" "$R/tmp/u.s" 2>/dev/null; cp "$R/tmp/u.s" "$R/tmp/t.s"
	timeout 40 env APSIM_ROOT="$R" "$APSIM" "$R/bin/as" -u -o /tmp/t.o /tmp/t.s 2>/dev/null
	if [ -s "$R/our.o" ] && [ -s "$R/tmp/t.o" ]; then
		if cmp -s "$R/our.o" "$R/tmp/t.o"; then m=$((m+1)); else d=$((d+1)); dl="$dl $tag"; fi
	else sk=$((sk+1)); fi
}
mark(){ for x in "$@"; do done_list="$done_list$x "; done; }	# record files consumed by a unit
seen(){ case "$done_list" in *" $1 "*) return 0;; *) return 1;; esac; }
# a boot block: assembled standalone with a `..' run-time origin, then STRIPPED
# and header-stripped by the Makefile.  Compare the shipped image = everything
# before the symbol table (header+text+data+relocs); the symtab is discarded, so
# its ordering never reaches the boot (rkuboot/rluboot differ only there).
cmp_boot(){ tag="$1"; s="$2"
	rm -f "$R/tmp/t.s" "$R/our.o" "$R/tmp/t.o" "$R/tmp/x.s"; cp "$s" "$R/tmp/t.s" 2>/dev/null || { sk=$((sk+1)); return; }
	"$BIN-as" -o "$R/our.o" "$R/tmp/t.s" 2>/dev/null; cp "$R/tmp/t.s" "$R/tmp/x.s"
	timeout 30 env APSIM_ROOT="$R" "$APSIM" "$R/bin/as" -o /tmp/t.o /tmp/x.s 2>/dev/null; cp "$R/tmp/t.o" "$R/nat.o" 2>/dev/null
	[ -s "$R/our.o" ] && [ -s "$R/nat.o" ] || { sk=$((sk+1)); return; }
	tw=$(od -An -tu2 -j2 -N2 "$R/our.o"|tr -d ' '); dw=$(od -An -tu2 -j4 -N2 "$R/our.o"|tr -d ' ')
	n=$((16 + 2*tw + 2*dw))				# offset of the (stripped-away) symbol table
	if cmp -s -n "$n" "$R/our.o" "$R/nat.o"; then m=$((m+1)); else d=$((d+1)); dl="$dl $tag"; fi
}

# ---- explicit multi-file units (from each Makefile's `as' line) ----
AS=$SRC/cmd/as;   [ -d "$AS" ]   && { cmp_unit "cmd/as:pass1" sys $AS/as1[0-9].s; mark $AS/as1[0-9].s;
                                      cmp_unit "cmd/as:pass2" sys $AS/as2[0-9].s; mark $AS/as2[0-9].s; }
RO=$SRC/cmd/roff; [ -d "$RO" ]   && { cmp_unit "cmd/roff" sys $RO/*.s; mark $RO/*.s; }
FP=$SRC/lib/fpsim;[ -d "$FP" ]   && { cmp_unit "lib/fpsim" none $FP/fp1.s $FP/fp2.s $FP/fp3.s $FP/fpx.s; mark $FP/fp1.s $FP/fp2.s $FP/fp3.s $FP/fpx.s;
                                      cmp_unit "lib/fpsim_sep" none $FP/fp1_sep.s $FP/fp2.s $FP/fp3.s $FP/fpx_sep.s; mark $FP/fp1_sep.s $FP/fpx_sep.s; }
PX=$SRC/ucb/pascal/px; [ -d "$PX" ] && {
	AL="00int.s 02rel.s 02relset.s 03bool.s 04as.s 05lv.s 06add.s 07sub.s 10mul.s 12div.s 13mod.s 14neg.s 16dvd.s 17ind.s 17rv.s 20con.s 21rang.s 24case.s 24pxp.s 25set.s 26for.s 27conv.s 30atof.s 30getname.s 30io.s 30iosubs.s 30read.s 30write.s 34fun.s E.s opcode.s wait.s"
	set -- ; for f in $AL; do set -- "$@" "$PX/$f"; done
	cmp_unit "ucb/px:as.o" sys "$PX/00head.s" "$@"; mark "$PX/00head.s"; for f in $AL; do mark "$PX/$f"; done; }

# known-excluded (need a generated input or special origin) -- counted, with reason
for x in "$SRC/cmd/c/table.s|cvopt-preprocessed" "$SRC/ucb/ranm/temp.s|generated"; do
	p=${x%|*}; [ -f "$p" ] && { ex=$((ex+1)); mark "$p"; echo "  EXCLUDE ${p#$SRC/}  (${x#*|})"; }; done
# boot blocks: `..'-origin, assembled standalone, shipped stripped (compare the
# pre-symtab image).  srt0.s is a normal standalone startup -> individual loop.
for p in $(find "$SRC/sys/mdec" -name '*.s' 2>/dev/null) "$SRC/sys/stand/mtboot.s" "$SRC/sys/stand/tsboot.s"; do
	[ -f "$p" ] && { cmp_boot "boot:${p#$SRC/}" "$p"; mark "$p"; }; done

# ---- everything else: assemble individually (implicit .s.o), sys.s if it uses a symbolic sys ----
# /bugs/ = f77 bug-repro artifacts (no Makefile, .f sources + .out expected): not
# built, and they hold f77-compiling-Fortran output (out of the .c/.s scope).
# bug4.s does `~~MAIN__ = _MAIN__' -- an alias to a forward-ref: native interns
# the LHS name before evaluating the RHS, we do it after, so the two symbols land
# in the .o symtab in the opposite order (4 bytes).  Only manifests on Fortran
# output, never in the buildable tree; excluded like OLD_/old.
for s in $(find "$TREE" -name '*.s' | grep -vE '/old/|/OLD_|/bugs/' | sort); do
	seen "$s" && continue
	rm -f "$R/tmp/t.s" "$R/our.o" "$R/tmp/t.o"
	if grep -qE '	sys	[a-z]|	sys [a-z]' "$s" 2>/dev/null; then cat "$SYS" "$s" > "$R/tmp/t.s" 2>/dev/null
	else cp "$s" "$R/tmp/t.s" 2>/dev/null; fi
	[ -s "$R/tmp/t.s" ] || { sk=$((sk+1)); continue; }
	"$BIN-as" -u -o "$R/our.o" "$R/tmp/t.s" 2>/dev/null
	timeout 30 env APSIM_ROOT="$R" "$APSIM" "$R/bin/as" -u -o /tmp/t.o /tmp/t.s 2>/dev/null
	if [ -s "$R/our.o" ] && [ -s "$R/tmp/t.o" ]; then
		if cmp -s "$R/our.o" "$R/tmp/t.o"; then m=$((m+1)); else d=$((d+1)); dl="$dl ${s#$SRC/}"; fi
	else sk=$((sk+1)); fi
done
echo "==== s-sweep $TREE ===="
echo "  MATCH native as: $m   DIFFER: $d   both-fail/skip: $sk   known-excluded: $ex"
[ -n "$dl" ] && { echo "  DIFFER:"; for x in $dl; do echo "    $x"; done; }
[ "$d" = 0 ]
