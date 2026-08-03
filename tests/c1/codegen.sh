# c1 pass-2: drive cpp -> c0 -> c1 and check the generated PDP-11 assembly.
. "$ROOT/tests/lib.sh"

tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

gen() {	# gen <c-source>  -> assembly on stdout
	echo "$1" > "$tmp/t.c"
	"$BIN-cpp" -Dpdp11=1 "$tmp/t.c" > "$tmp/t.i" 2>/dev/null
	"$BIN-c0" "$tmp/t.i" "$tmp/t.1" "$tmp/t.2" || { echo "c0 failed"; return 1; }
	"$BIN-c1" "$tmp/t.1" "$tmp/t.2" "$tmp/t.s" || { echo "c1 failed"; return 1; }
	cat "$tmp/t.s"
}

# return a constant: 42 decimal == 52 octal, loaded into r0
asm=`gen 'int main() { return 42; }'`
check_contains "fn label"   "$asm" "_main:"
check_contains "prologue"   "$asm" "jsr	r5,csv"
check_contains "ret 42"     "$asm" "mov	\$52,r0"
check_contains "return"     "$asm" "jmp	cret"

# constant folding: 2 + 3*4 == 14 == 16 octal
asm=`gen 'int main() { return 2 + 3 * 4; }'`
check_contains "fold 2+3*4" "$asm" "mov	\$16,r0"

# global variable: definition in .data and a load of _x
asm=`gen 'int x = 7; int main() { return x; }'`
check_contains "data seg"   "$asm" ".data"
check_contains "global def" "$asm" "_x:"
check_contains "load global" "$asm" "mov	_x,r0"

exit 0
