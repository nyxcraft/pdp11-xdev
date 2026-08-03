# Helpers for shell-based toolchain tests.  Source this from a *.sh test:
#
#     . "$ROOT/tests/lib.sh"
#     check_eq "size text" 128 "`field ...`"
#
# ROOT, BIN (= .../bin/pdp11), and FIX (= tests/fixtures) are
# exported by run.sh.  A test fails (exits nonzero) on the first failed
# check.

# Optimisation flags for cc compiles (run.sh re-runs correctness tests with -O).
: "${CCOPT:=}"

fail() { echo "  $*"; exit 1; }

# check_eq LABEL EXPECTED ACTUAL
check_eq() {
	if [ "$2" != "$3" ]; then
		fail "$1: expected [$2], got [$3]"
	fi
}

# check_contains LABEL HAYSTACK NEEDLE
check_contains() {
	case "$2" in
	*"$3"*) : ;;
	*) fail "$1: [$2] does not contain [$3]" ;;
	esac
}
