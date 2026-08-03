# Double precision: prove `double' (DEC D, 64-bit, 56-bit mantissa) actually
# preserves precision that `float' (DEC F, 32-bit, 24-bit mantissa) loses, and
# that 8-byte double storage round-trips through memory and arrays.  The
# decisive case is 100000001.0: it needs more than 24 mantissa bits, so a
# `float' rounds it to 100000000 (the +1 vanishes) while a `double' keeps it.
. "$ROOT/tests/lib.sh"

SIM="$BIN-apsim"
tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/d.c" <<'EOF'
double recip(x) double x; { return 1.0 / x; }

int main() {
	float  f;
	double d, e, sum, third, a[4];
	int i;

	f = 100000001.0;            /* single rounds away the +1 */
	d = 100000001.0;            /* double keeps it           */
	printf("prec %d %d\n",
		(int)(f - 100000000.0),
		(int)(d - 100000000.0));

	third = recip(3.0);
	e = third * 3.0;            /* 1/3 * 3 round-trips */
	printf("third %f %f\n", third, e);

	d = 123456789.0;            /* large integer held exactly */
	printf("bigint %f\n", d);

	sum = 0.0;
	for (i = 0; i < 100; i++) sum = sum + 0.1;
	printf("sum01 %f\n", sum);

	for (i = 0; i < 4; i++) a[i] = recip((double)(i + 1));
	printf("recips %f %f %f %f\n", a[0], a[1], a[2], a[3]);
	return 0;
}
EOF
( cd "$tmp" && "$BIN-cc" d.c -o d ) || fail "cc/link double test failed"

got=`"$SIM" "$tmp/d" 2>/dev/null`
want='prec 0 1
third 0.333333 1.000000
bigint 123456789.000000
sum01 10.000000
recips 1.000000 0.500000 0.333333 0.250000'

check_eq "double precision" "$want" "$got"

exit 0
