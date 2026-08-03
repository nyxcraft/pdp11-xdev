# c0 pass-1: preprocess and parse `int main(){return 42;}`, then check the
# emitted intermediate stream for the expected opcodes and the constant 42.
. "$ROOT/tests/lib.sh"

tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

echo 'int main() { return 42; }' > "$tmp/t.c"
"$BIN-cpp" -Dpdp11=1 "$tmp/t.c" > "$tmp/t.i" 2>/dev/null
"$BIN-c0" "$tmp/t.i" "$tmp/t.1" "$tmp/t.2" || fail "c0 exited nonzero"

# Intermediate byte stream (od hex) for inspection.
hex=`od -An -tx1 "$tmp/t.1" | tr -s ' \n' ' '`

# SYMDEF(0xcf)+marker then the symbol name "_main".
check_contains "symdef _main" "$hex" "cf fe 5f 6d 61 69 6e 00"
# RLABEL(0x72) _main : the function entry label.
check_contains "rlabel _main" "$hex" "72 fe 5f 6d 61 69 6e 00"
# CON(0x15) type INT(00 00) value 42(2a 00) : the returned constant.
check_contains "con 42" "$hex" "15 fe 00 00 2a 00"
# RETRN(0xd1): the return.
check_contains "retrn" "$hex" "d1 fe"

exit 0
