#!/bin/sh
# fuzz-load.sh -- run the deterministic malformed-a.out corpus through the
# REAL loader (no separate parser model to drift out of sync).  A case
# passes unless apsim dies of a sanitizer abort or its own SIGSEGV; a
# rejected load, a guest fault, a clean exit, and a timeout are all
# legitimate answers to hostile input (a "valid" garbage program looping
# until the timeout is emulator behavior, not a loader bug).
here=$(cd "$(dirname "$0")" && pwd); BIN="$here/../../../bin"
APSIM="$BIN/pdp11-apsim"
work=$(mktemp -d) || exit 1; trap 'rm -rf "$work"' EXIT

python3 "$here/mkfuzz.py" "$work/corpus" >/dev/null || exit 1

fail=0; ran=0
for f in "$work/corpus"/*; do
	timeout 5 "$APSIM" "$f" arg1 arg2 </dev/null >/dev/null 2>"$work/err"
	rc=$?
	ran=$((ran+1))
	# 134/135 = sanitizer/assert abort, 139 = apsim's own SIGSEGV
	if [ $rc -eq 134 ] || [ $rc -eq 135 ] || [ $rc -eq 139 ] || \
	   grep -qE "AddressSanitizer|runtime error" "$work/err"; then
		echo "FUZZ FAIL: $(basename "$f") rc=$rc"
		sed -n '1,6p' "$work/err" | sed 's/^/    /'
		fail=$((fail+1))
	fi
done
echo "fuzz-load: $ran cases, $fail failures"
[ $fail -eq 0 ]
