# buffered stdio end-to-end: cc compiles a printf program, links it against
# crt0 + libc.a (authentic 2.8BSD printf/_doprnt/strout/flsbuf + buffering),
# and apsim runs it -- the output (flushed at exit via _cleanup) must match.
# Exercises K&R varargs, the format dispatch, and every common conversion.
. "$ROOT/tests/lib.sh"

SIM="$BIN-apsim"
tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

# 1. plain string + a decimal arg
cat > "$tmp/p.c" <<'EOF'
int main(){ printf("hello, pdp-11! year %d\n", 2025); return 0; }
EOF
( cd "$tmp" && "$BIN-cc" $CCOPT p.c -o p ) || fail "cc p.c failed"
check_eq "printf %d" "hello, pdp-11! year 2025" "`"$SIM" "$tmp/p" 2>/dev/null`"

# 2. all the common conversions: %d %s %c %x %o and a negative %d
cat > "$tmp/q.c" <<'EOF'
int main(){
	printf("%d %d %d\n", 2, 40, 42);
	printf("%s says %c%c\n", "cc", 'h', 'i');
	printf("hex %x oct %o neg %d\n", 255, 64, -7);
	return 0;
}
EOF
( cd "$tmp" && "$BIN-cc" $CCOPT q.c -o q ) || fail "cc q.c failed"
out=`"$SIM" "$tmp/q" 2>/dev/null`
check_eq "printf line 1" "2 40 42" "`echo "$out" | sed -n 1p`"
check_eq "printf line 2" "cc says hi" "`echo "$out" | sed -n 2p`"
check_eq "printf line 3" "hex ff oct 100 neg -7" "`echo "$out" | sed -n 3p`"

exit 0
