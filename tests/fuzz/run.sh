#!/bin/sh
# Fuzz the object parsers -- ld, ar, nm, size, strip, objcopy -- on a malformed
# a.out + archive corpus, built under AddressSanitizer + UBSan.  A case passes
# unless the tool dies of a sanitizer abort or its own SIGSEGV; a clean reject,
# a diagnostic, a normal exit, and a timeout are all legitimate answers to
# hostile input.  Plain (non-sanitized) builds are restored on exit.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN="$ROOT/bin"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
TOOLS="pdp11-ld pdp11-ar pdp11-nm pdp11-size pdp11-strip pdp11-objcopy"
W=$(mktemp -d) || exit 1
trap 'rm -rf "$W"; for t in $TOOLS; do make -s -C "$ROOT/src/$t" clean all >/dev/null 2>&1; done' EXIT

echo "building parsers under ASan+UBSan..."
for t in $TOOLS; do
	make -s -C "$ROOT/src/$t" clean >/dev/null 2>&1
	if ! make -s -C "$ROOT/src/$t" OPT="$SAN" >/dev/null 2>&1; then
		echo "sanitized build failed: $t"
		exit 1
	fi
done

python3 "$HERE/mkcorpus.py" "$W/corpus" >/dev/null || exit 1

export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ran=0
fail=0

crashed() { # crashed <rc> <errfile>
	case "$1" in 134 | 135 | 139) return 0 ;; esac
	grep -qE "AddressSanitizer|runtime error|UndefinedBehaviorSanitizer" "$2"
}

run() { # run <label> <cmd...>
	label="$1"
	shift
	ran=$((ran + 1))
	timeout 10 "$@" >/dev/null 2>"$W/err" </dev/null
	rc=$?
	if crashed "$rc" "$W/err"; then
		echo "FUZZ FAIL rc=$rc: $label on $(basename "$IN")"
		sed -n '1,4p' "$W/err" | sed 's/^/    /'
		fail=$((fail + 1))
	fi
}

for f in "$W/corpus"/*; do
	IN="$f"
	cp "$f" "$W/copy" # strip/objcopy rewrite -- give them a scratch copy
	run nm "$BIN/pdp11-nm" "$f"
	run size "$BIN/pdp11-size" "$f"
	run ar-t "$BIN/pdp11-ar" t "$f"
	run strip "$BIN/pdp11-strip" "$W/copy"
	run objcopy "$BIN/pdp11-objcopy" "$f" "$W/o1"
	run objcopy-bin "$BIN/pdp11-objcopy" -O binary "$f" "$W/o2"
	run ld "$BIN/pdp11-ld" -o "$W/lout" "$f"
done

echo "fuzz-tools: $ran runs, $fail failures"
[ "$fail" -eq 0 ]
