# Float stress fixture: compile tests/fixtures/float_stress.c with cc, run it
# in apsim, and compare its full stdout to a golden.  Unlike the small cases in
# float.sh, this is one realistic numerical program (Newton sqrt, factorial/e,
# Horner polynomial, array mean/variance, a 10000-term harmonic sum, a 5000-
# term Leibniz pi, and magnitude extremes) that exercises double args/returns,
# recursion, float arrays, deep iteration, and %f/%e/%g together.
. "$ROOT/tests/lib.sh"

SIM="$BIN-apsim"
tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

cp "$FIX/float_stress.c" "$tmp/fs.c"
( cd "$tmp" && "$BIN-cc" fs.c -o fs ) || fail "cc/link float_stress.c failed"

got=`"$SIM" "$tmp/fs" 2>/dev/null`
want='sqrt: 1.414214 4.000000 1.500000
fact5=120.000000 e=2.718282
poly: 7.000000 -17.000000
stats: 5.250000 11.812500 3.436932
harm=9.787606
pi=3.141393
mag: 1.000000e+30 2.000000e-30 2.000000e+00
fmt: .0001 1.e+05 3.14159 0'

check_eq "float_stress output" "$want" "$got"

exit 0
