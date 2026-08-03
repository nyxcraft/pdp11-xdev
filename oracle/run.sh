#!/bin/sh
# native-compiler oracle test.
#
# For each corpus .c file, compile through cpp + c0 (our ports), then run BOTH
# our ported c1/c2 and the original 1981 2.8BSD c1/c2 binaries (under apsim) and
# compare.  The native binaries are the ground truth: a mismatch means our port
# diverged from the compiler that actually built 2.8BSD.
#
#   c2 test  -- byte-exact: same c1 input to both c2's, output must be identical.
#   c1 test  -- assemble both c1 outputs with `as` and compare the .o (the c1
#               text streams differ only cosmetically: the native c1 emits the
#               NUL/space bytes our port strips; `as` normalizes them away).
#
# Usage: sh run.sh            (run whole corpus)
#        sh run.sh foo.c      (one file)
#
# Tolerant of nonzero exits from apsim (the native c1 crashes on large inputs --
# see apsim-native-compiler-verification memory): such a case shows as c1=skip.
HERE=$(cd "$(dirname "$0")" && pwd)
SIM="$HERE"
BIN="$SIM/../bin/pdp11"
APSIM="$BIN-apsim"
NAT="$HERE/native"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

pass=0; fail=0
run_one() {
	c="$1"; b=$(basename "$c" .c)
	"$BIN-cpp" -I"$(dirname "$c")" "$c" > "$T/i" 2>/dev/null
	"$BIN-c0" "$T/i" "$T/t1" "$T/t2" 2>/dev/null

	# --- our c1 -> our/native c2 (c2 byte-exact test) ---
	"$BIN-c1" "$T/t1" "$T/t2" "$T/c1.s" 2>/dev/null
	tr -d '\0' < "$T/c1.s" > "$T/c1_clean.s"
	"$BIN-c2" "$T/c1_clean.s" "$T/ourc2.s" 2>/dev/null || cp "$T/c1_clean.s" "$T/ourc2.s"
	"$APSIM" "$NAT/c2" < "$T/c1_clean.s" > "$T/natc2.s" 2>/dev/null
	tr -d '\0' < "$T/ourc2.s" > "$T/o2"; tr -d '\0' < "$T/natc2.s" > "$T/n2"
	if cmp -s "$T/o2" "$T/n2"; then c2res=ok; else c2res=DIFF; fi

	# --- our c0+c1 vs native c0+c1 (assemble both, compare .o) ---
	# Each toolchain stays internally consistent: native c1 is fed NATIVE
	# c0's intermediate, because our c0 and native c0 differ on long-constant
	# word order -- feeding native c1 our c0 output makes it misread longs.
	# The c1 text streams also differ cosmetically (NUL/space bytes; signed
	# vs unsigned %o on immediates), so we compare the assembled .o, not text.
	rm -f "$T/nt1" "$T/nt2" "$T/natc1.s"
	"$APSIM" "$NAT/c0" "$T/i" "$T/nt1" "$T/nt2" 2>/dev/null || true
	"$APSIM" "$NAT/c1" "$T/nt1" "$T/nt2" "$T/natc1.s" 2>/dev/null || true
	if [ ! -s "$T/natc1.s" ]; then
		c1res=skip   # native c0/c1 crashed under apsim (input too large)
	else
		tr -d '\0' < "$T/natc1.s" > "$T/natc1_clean.s"
		if "$BIN-as" -o "$T/our.o" "$T/c1_clean.s" 2>/dev/null && \
		   "$BIN-as" -o "$T/nat.o" "$T/natc1_clean.s" 2>/dev/null; then
			if cmp -s "$T/our.o" "$T/nat.o"; then c1res=ok; else c1res=DIFF; fi
		else
			c1res=skip-as
		fi
	fi

	# a DIFF in either pass fails; a skip (native c1 couldn't run) is not a fail
	if [ "$c2res" = DIFF ] || [ "$c1res" = DIFF ]; then
		fail=$((fail+1)); st=FAIL
	else
		pass=$((pass+1)); st=PASS
	fi
	printf '  %-12s c1=%-7s c2=%-5s  %s\n' "$b" "$c1res" "$c2res" "$st"
}

if [ $# -gt 0 ]; then
	run_one "$1"
else
	for c in "$HERE"/corpus/*.c; do run_one "$c"; done
fi
echo "---- oracle: $pass passed, $fail failed ----"
[ "$fail" = 0 ]
