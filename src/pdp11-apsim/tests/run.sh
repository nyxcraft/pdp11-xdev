#!/bin/sh
# apsim regression suite.  The late-hardware golden probe (cistest.s)
# exercises MFPT, SPL, TSTSET, WRTLCK, FIS, and the decimal CIS group:
# each numbered case jumps to `fail' with the case number in r5, so a
# nonzero exit identifies the first failing case; exit 0 = all pass.
here=$(cd "$(dirname "$0")" && pwd); BIN="$here/../../../bin"
AS="$BIN/pdp11-as"; APSIM="$BIN/pdp11-apsim"
fail=0; tmp=$(mktemp -d) || exit 1; trap 'rm -rf "$tmp"' EXIT

# ---- 1: late-hardware/CIS instruction golden ---------------------------
if "$AS" -j -o "$tmp/cistest" "$here/cistest.s" && "$APSIM" "$tmp/cistest"; then
	printf '%-11s ok    %s\n' cistest 'MFPT/SPL/TSTSET/WRTLCK/FIS/CIS all pass'
else
	printf '%-11s FAIL  %s\n' cistest "first failing case: exit $?"
	fail=1
fi

[ "$fail" = 0 ] && echo "ALL PASS" || echo "FAILURES"; exit $fail
