#!/bin/sh
# tree-sweep.sh TREE [EXTRA_CFLAGS] -- assembler oracle over a whole subtree,
# harvesting each directory's own Makefile -D flags (so files compile with the
# options the native build used: f77's -DFAMILY=DMR, ls's -DUCB_PWHASH, ...).
#
# For every .c under TREE: compile to .s with our cc using (-O + the dir's -D
# flags + EXTRA_CFLAGS + -I<dir> + cross-dir -I for the shared headers that live
# in sibling dirs), then assemble that .s with our as AND the native 2.9 as
# (apsim) and compare.  Vestigial trees (OLD_*, */old/) and the VAX-only uucp
# syskludge are skipped.  Reports MATCH / DIFFER / cc-fail / as-skip.
#
# Needs sim/native/{as,as2} (extract-rootdump.py).  See docs/bin-bytematch.md.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
APSIM="$BIN-apsim"; NAT="$HERE/native"; SRC="${SRC:-$HOME/bsd/2.9/usr/src}"
TREE="$1"; EXTRA="$2"
for f in as as2; do [ -f "$NAT/$f" ] || { echo "missing $NAT/$f -- run extract-rootdump.py"; exit 1; }; done

R="${CORPUS_WORK:-$HOME/.pdp11-corpus/sweep.$$}"; rm -rf "$R"; mkdir -p "$R/bin" "$R/lib" "$R/usr/lib" "$R/tmp"
cp "$NAT/as" "$R/bin/as"; cp "$NAT/as2" "$R/lib/as2"; export TMPDIR="$R/tmp"
# native 2.9 yacc (+ its /usr/lib/yaccpar skeleton), if the fixtures are present,
# so yflags can generate authentic 2.9-format grammar output under apsim
[ -f "$NAT/yacc" ] && cp "$NAT/yacc" "$R/bin/yacc"
[ -f "$NAT/yaccpar" ] && { cp "$NAT/yaccpar" "$R/usr/lib/yaccpar"; cp "$NAT/yaccpar" "$R/lib/yaccpar"; }
trap 'rm -rf "$R"' EXIT
# cross-dir headers that sibling programs include (ndir.h, mfile1/2, macdefs)
XI="-I$SRC/cmd/uucp/LIBNDIR -I$SRC/cmd/mip -I$SRC/cmd/pcc -I$SRC/cmd/lint"
# resolve a dir's makefile -- 23 dirs (mip, lpr, plot, yacc/lib, delivermail, ...)
# spell it lowercase `makefile' with no capital `Makefile', so a Makefile-only
# grep silently dropped their build -D/-I flags.  Prefer Makefile, else makefile.
mkf(){ [ -f "$1/Makefile" ] && echo "$1/Makefile" || echo "$1/makefile"; }
# macro names may contain digits (Mail's -DV7); [A-Za-z_]+ stopped at the digit
# and captured `-DV', so V7 was never defined -> Mail's local.h omitted v7.local.h.
# Strip `#'-to-EOL first: delivermail's makefile DOCUMENTS its optional flags in a
# comment block (`-DLOG -- include log information...'), and harvesting -DLOG from
# there dragged in the absent <log.h>, breaking err.c/main.c that build fine on the
# real CFLAGS (-O -DDBM -DVFORK -DDEBUG).  Only active make lines should count.
dflags(){ sed 's/#.*//' "$(mkf "$1")" 2>/dev/null | grep -hoE '\-D[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?' | sort -u | tr '\n' ' '; }

# site-group macros the native build got from the machine's <whoami.h>, not the
# Makefile.  berknet's mach.h keys every per-machine block off an OUTER `#ifdef
# BERKELEY' (the site) with the specific machine (VIRUS -> LOCAL 'k') nested
# inside; the shipped whoami.h defines only VIRUS (what the kernel needs), so
# without BERKELEY the machine block never fires and netdaemon/sub/v6mail fail
# with `LOCAL undefined'.  Supply the site macro per-dir, as the VIRUS host did.
sitedefs(){ case "$(basename "$1")" in berknet) echo "-DBERKELEY";; esac; }

# yacc-generated headers: a yacc program's hand-written .c files #include the
# grammar's token table (y.tab.h, often renamed e.def / *.h in the Makefile).
# It's a build artifact, absent from the source tree, so generate it here with
# the host yacc into a per-dir staging area and -I it.  The token *numbers* need
# not match 2.9's yacc -- both assemblers see the SAME .s our cc emits, so the
# as-oracle stays valid; the header only has to let the .c compile.  Cached per
# dir (empty marker file); returns the -I flag on stdout.
GEN="$R/gen"; mkdir -p "$GEN"
yflags(){ d="$1"; y=$(ls "$d"/*.y "$d"/*.g 2>/dev/null | head -1); [ -z "$y" ] && return	# ratfor's grammar is r.g
	# SHORT staging dir name (cksum hash, not the full path): a native tool later
	# fed a .i that embeds this -I path would overflow its ~100-char filename
	# buffer and die with a bogus "Symbol table overflow" (the apsim long-path
	# gotcha) -- keep the path short so that never triggers.
	h=$(echo "$d" | cksum | cut -d' ' -f1); g="$GEN/$h"
	if [ ! -e "$g/.done" ]; then mkdir -p "$g"
		# Generate the grammar's y.tab.h/y.tab.c.  Prefer the NATIVE 2.9 yacc
		# under apsim (sim/native/yacc + yaccpar): it emits authentic 2.9-format
		# output (a plain `#define TOKEN num' header and a parser .c the 2.9 c0
		# accepts).  The host yacc/bison emits a modern header (enum yytokentype,
		# `extern YYSTYPE yylval;', `int yyparse(void);') that the 2.9 c0 rejects,
		# so if we fall back to it, keep only its `#define' lines.
		if [ -f "$R/bin/yacc" ]; then
			rm -f "$R/tmp/g.y" "$R/tmp/y.tab.c" "$R/tmp/y.tab.h"; cp "$y" "$R/tmp/g.y"
			(cd "$R/tmp" && timeout 60 env APSIM_ROOT="$R" "$APSIM" "$R/bin/yacc" -d /tmp/g.y >/dev/null 2>&1)
			[ -f "$R/tmp/y.tab.h" ] && cp "$R/tmp/y.tab.h" "$g/y.tab.h"
			[ -f "$R/tmp/y.tab.c" ] && cp "$R/tmp/y.tab.c" "$g/y.tab.c"
		fi
		if [ ! -f "$g/y.tab.h" ]; then			# fallback: host yacc, filtered
			(cd "$g" && yacc -d "$y" >/dev/null 2>&1)
			[ -f "$g/y.tab.h" ] && { grep -E '^#[ 	]*define[ 	]' "$g/y.tab.h" > "$g/y.tab.h.f"; mv "$g/y.tab.h.f" "$g/y.tab.h"; }
		fi
		# stage the header (and the generated .c) under every name the Makefile
		# renames y.tab.h/y.tab.c to (e.def/e.c, x.h/x.c, ...) plus the eqn/neqn
		# `e.def' convention, so the hand-written .c can #include it.
		if [ -f "$g/y.tab.h" ]; then
			for nm in $(grep -hoE 'y\.tab\.h[ ]+[A-Za-z0-9_.]+' "$(mkf "$d")" 2>/dev/null | awk '{print $2}'); do
				cp "$g/y.tab.h" "$g/$nm" 2>/dev/null; done
			for nm in $(grep -hoE 'y\.tab\.c[ ]+[A-Za-z0-9_.]+' "$(mkf "$d")" 2>/dev/null | awk '{print $2}'); do
				[ -f "$g/y.tab.c" ] && cp "$g/y.tab.c" "$g/$nm" 2>/dev/null; done
			cp "$g/y.tab.h" "$g/e.def" 2>/dev/null   # neqn/eqn common rename
		fi
		: > "$g/.done"
	fi
	[ -f "$g/y.tab.h" ] && echo "-I$g"; }

m=0; d=0; cf=0; sk=0; dl=""
for c in $(find "$TREE" -name '*.c' | sort | grep -vE '/old/|/OLD_|\.old/|/oldcsh/|syskludge'); do
	dd=$(dflags "$(dirname "$c")"); yi=$(yflags "$(dirname "$c")"); sd=$(sitedefs "$(dirname "$c")")
	rm -f "$R/tmp/s.c" "$R/tmp/s.s"
	cp "$c" "$R/tmp/s.c" 2>/dev/null || { cf=$((cf+1)); continue; }	# rm-first above: 0444 sources
	(cd "$R/tmp" && "$BIN-cc" -O $dd $sd $EXTRA -I"$(dirname "$c")" $yi $XI -S s.c) 2>/dev/null
	[ -s "$R/tmp/s.s" ] || { cf=$((cf+1)); continue; }
	"$BIN-as" -u -o "$R/our.o" "$R/tmp/s.s" 2>/dev/null
	rm -f "$R/tmp/t.o" "$R/tmp/t.s"; cp "$R/tmp/s.s" "$R/tmp/t.s"
	timeout 15 env APSIM_ROOT="$R" "$APSIM" "$R/bin/as" -u -o /tmp/t.o /tmp/t.s 2>/dev/null
	[ -s "$R/tmp/t.o" ] || { sk=$((sk+1)); continue; }
	if cmp -s "$R/our.o" "$R/tmp/t.o"; then m=$((m+1)); else d=$((d+1)); dl="$dl ${c#$TREE/}"; fi
done
echo "==== tree-sweep $TREE ===="
echo "  MATCH native as: $m   DIFFER: $d   cc-fail: $cf   as-skip: $sk"
[ -n "$dl" ] && { echo "  DIFFER:"; for x in $dl; do echo "    $x"; done; }
[ "$d" = 0 ]
