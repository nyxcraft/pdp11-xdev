# size/nm/strip against a committed real 2.9BSD PDP-11 object (dsort.o, from
# the 2.9BSD GENERIC kernel build -- usr/src/sys/GENERIC/dsort.o).
. "$ROOT/tests/lib.sh"

obj="$FIX/dsort.o"

# --- size: exact segment sizes (self-verified: dsort.o is pure text)
line=`"$BIN-size" "$obj" | tail -1`
set -- $line
check_eq "size text" 158 "$1"
check_eq "size data" "+" "$2"      # layout: "158 +\t0 +\t0 =..."
check_eq "size data val" 0 "$3"
check_eq "size bss val" 0 "$5"

# --- nm: decodes the 2.9 symbol table (16-bit n_type) and emits 6-digit octal.
# 39 entries: text labels, commons (_disksor et al.), register + undef symbols.
n=`"$BIN-nm" "$obj" | wc -l | tr -d ' '`
check_eq "nm symbol count" 39 "$n"
check_contains "nm format" "`"$BIN-nm" "$obj" | head -1`" " "

# --- strip: drops the symbol table and sets the no-relocation flag
tmp=`mktemp`
cp "$obj" "$tmp"
"$BIN-strip" "$tmp" || fail "strip exited nonzero"
after=`"$BIN-nm" "$tmp" 2>&1`
check_contains "strip removed names" "$after" "no name list"
rm -f "$tmp"

exit 0
