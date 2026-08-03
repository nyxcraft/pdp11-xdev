# das: the PDP-11 disassembler.  Verify it (1) decodes a bare object with
# symbol labels, (2) splits a linked a.out back into per-object listings via
# the N_FN file symbols, and (3) disassembles each archive member to its own
# file.  das writes <stem>.<object>.dis files into the current directory, so
# the test runs inside its scratch dir.
. "$ROOT/tests/lib.sh"

tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

# --- 1. object disassembly + symbol labelling -----------------------------
cat > m.c <<'EOF'
int sq(n) int n; { return n * n; }
EOF
"$BIN-cc" -c m.c || fail "cc -c m.c failed"
dis=`"$BIN-das" -p m.o`
check_contains "object: function label" "$dis" "_sq:"
check_contains "object: mul decoded"    "$dis" "mul"
check_contains "object: csv prologue"   "$dis" "jsr"

# --- 2. a.out split into per-object listings ------------------------------
cat > p.c <<'EOF'
int helper() { return 7; }
int main() { return helper(); }
EOF
"$BIN-cc" p.c -o p || fail "cc p.c failed"
"$BIN-das" p >/dev/null 2>&1
test -f p.crt0.o.dis || fail "a.out: no per-object crt0.o.dis (split failed)"
grep -l '_main:'   p.*.dis >/dev/null 2>&1 || fail "a.out: _main not in any split file"
grep -l '_helper:' p.*.dis >/dev/null 2>&1 || fail "a.out: _helper not in any split file"

# --- 3. archive: one listing per member -----------------------------------
"$BIN-ar" cr lib.a m.o || fail "ar failed"
"$BIN-das" lib.a >/dev/null 2>&1
test -f lib.a.m.o.dis || fail "archive: member m.o not disassembled"
check_contains "archive member" "`cat lib.a.m.o.dis`" "_sq:"

# --- 4. assembleable round-trip: das -a | as -> byte-identical text+data ---
cat > rt.c <<'EOF'
int g;
int helper(n) int n; { if (n < 2) return 1; return n * helper(n - 1); }
int main() { g = helper(5); return g; }
EOF
"$BIN-cc" -c rt.c || fail "cc -c rt.c failed"
"$BIN-das" -a -p rt.o > rt.s 2>/dev/null
"$BIN-as" -o rt2.o rt.s 2>/dev/null || fail "das -a output did not assemble"
# compare text+data segments of the original and the reassembled object
seg() { od -An -tx1 -j16 -N`expr \`od -An -tu2 -j2 -N4 "$1" | awk '{print $1+$2}'\`` "$1"; }
[ "`seg rt.o`" = "`seg rt2.o`" ] || fail "round-trip text+data not byte-identical"

exit 0
