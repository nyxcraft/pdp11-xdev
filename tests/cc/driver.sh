# cc driver: cc -S drives cpp -> c0 -> c1 and writes PDP-11 assembly,
# resolving the passes relative to its own install location.
. "$ROOT/tests/lib.sh"

tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

echo 'int main() { return 42; }' > "$tmp/t.c"
( cd "$tmp" && "$BIN-cc" -S t.c ) || fail "cc -S exited nonzero"
[ -f "$tmp/t.s" ] || fail "cc -S did not produce t.s"

asm=`cat "$tmp/t.s"`
check_contains "fn label" "$asm" "_main:"
check_contains "ret 42"   "$asm" "mov	\$52,r0"
check_contains "return"   "$asm" "jmp	cret"

# the -Dpdp11 the driver passes to cpp must reach the source
printf '#ifdef pdp11\nint here = 1;\n#endif\nint main(){return here;}\n' > "$tmp/d.c"
( cd "$tmp" && "$BIN-cc" -S d.c ) || fail "cc -S d.c failed"
check_contains "pdp11 defined" "`cat \"$tmp/d.s\"`" "_here:"

exit 0
