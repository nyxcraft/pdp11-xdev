#!/bin/sh
# selfhost.sh -- rebuild the 2.9 TOOLCHAIN's own binaries from source with our
# toolchain and byte-compare to the on-disk native ones (extracted from the
# rootdump).  The strongest end-to-end check: our as/ld reproducing the native
# assembler and linker exactly.
#
#   as, as2  -- PURE ASSEMBLY (as1?.s / as2?.s + sys.s), linked `ld -n -s a.out'
#               with NO crt0 and NO libc, so nothing but our as + ld is exercised.
#               Byte-identical to native /bin/as and /lib/as2.
#   strip    -- a C tool; needs the era libc (strip pulls sys_errlist via perror;
#               the default UCB_NET errlst is +870 data bytes) AND the sccsid the
#               shipped 2.4 carried but our source checkout lacks (recovered from
#               the binary -- see docs/bin-bytematch.md).  Then byte-identical.
#
# NOT byte-matchable (documented, NOT a toolchain defect): nm/ar -- /bin/nm is
# inode-dated 1986 but the archived nm.c is 1982, i.e. built from a later
# unarchived revision (source-revision skew).  apsim proves our whole compiler
# chain cpp->c0->c1->c2 is byte-exact to native 2.9, so the residual is source,
# not tool.  See [[bin-utilities-byte-match]].
#
# Needs sim/native/{as,as2,strip.target} + libc-era.a/crt0-era.o (`make libc-era').
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
NAT="$HERE/native"; SRC="${SRC:-$HOME/bsd/2.9/usr/src}"; SYSS="$HOME/bsd/2.9/usr/include/sys.s"
LIB="$HERE/../lib/bsd29"
for f in as as2 strip.target; do [ -f "$NAT/$f" ] || { echo "missing $NAT/$f -- run extract-rootdump.py"; exit 1; }; done

R="${CORPUS_WORK:-$HOME/.pdp11-corpus/selfhost.$$}"; rm -rf "$R"; mkdir -p "$R"; trap 'rm -rf "$R"' EXIT
cp "$SRC/cmd/as"/*.s "$R/"; ok=0; tot=0
chk(){ tot=$((tot+1)); if cmp -s "$1" "$2"; then ok=$((ok+1)); echo "  $3: BYTE-IDENTICAL ($(stat -c%s "$1") B)"; else echo "  $3: DIFFER (our $(stat -c%s "$1" 2>/dev/null) vs native $(stat -c%s "$2") B)"; fi; }

# --- as / as2 : pure assembly, no crt0/libc ---
( cd "$R" && "$BIN-as" "$SYSS" as1?.s && "$BIN-ld" -n -s a.out -o as ) >/dev/null 2>&1
( cd "$R" && "$BIN-as" "$SYSS" as2?.s && "$BIN-ld" -n -s a.out -o as2 ) >/dev/null 2>&1
chk "$R/as"  "$NAT/as"  "as "
chk "$R/as2" "$NAT/as2" "as2"

# --- strip : era libc + the recovered sccsid ---
if [ -f "$LIB/libc-era.a" ] && [ -f "$LIB/crt0-era.o" ]; then
	cp "$LIB/libc-era.a" "$LIB/crt0-era.o" "$R/"; ( cd "$R" && "$BIN-ranlib" libc-era.a ) 2>/dev/null
	printf 'char *sccsid = "@(#)strip.c\\t2.4";\n' > "$R/strip.c"; cat "$SRC/cmd/strip.c" >> "$R/strip.c"
	( cd "$R" && "$BIN-cc" -c -O strip.c && "$BIN-ld" -X -n -s crt0-era.o strip.o libc-era.a -o strip ) >/dev/null 2>&1
	chk "$R/strip" "$NAT/strip.target" "strip"
else echo "  strip: SKIP (run 'make libc-era' for libc-era.a/crt0-era.o)"; fi

echo "==== selfhost: $ok/$tot toolchain binaries byte-identical to native ===="
[ "$ok" = "$tot" ]
