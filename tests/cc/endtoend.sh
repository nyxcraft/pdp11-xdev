# end-to-end: cc compiles+links a whole program against crt0+libc.a, and the
# resulting 2.8BSD PDP-11 a.out is executed in the apsim simulator -- the
# ultimate check that the entire pipeline (cpp->c0->c1->as->ld + runtime)
# produces correct, runnable code.
. "$ROOT/tests/lib.sh"

SIM="$BIN-apsim"
tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

# 1. exit status flows back: main's return value becomes the process exit code
echo 'int main(){ return 42; }' > "$tmp/r.c"
( cd "$tmp" && "$BIN-cc" $CCOPT r.c -o r ) || fail "cc r.c failed"
"$SIM" "$tmp/r" >/dev/null 2>&1
check_eq "return 42 -> exit 42" 42 "$?"

echo 'int main(){ int i,s; s=0; for(i=1;i<=8;i++) s=s+i; return s; }' > "$tmp/s.c"
( cd "$tmp" && "$BIN-cc" $CCOPT s.c -o s ) || fail "cc s.c failed"
"$SIM" "$tmp/s" >/dev/null 2>&1
check_eq "sum 1..8 -> exit 36" 36 "$?"

# 2. output flows out: write(2) through the libc stub reaches the host
cat > "$tmp/h.c" <<'EOF'
char msg[] "hello, pdp-11\n";
int main(){ write(1, msg, 14); return 0; }
EOF
( cd "$tmp" && "$BIN-cc" $CCOPT h.c -o h ) || fail "cc h.c failed"
out=`"$SIM" "$tmp/h" 2>/dev/null`
check_eq "write output" "hello, pdp-11" "$out"

# 3. recursion + arithmetic end to end (fib(10)=55)
cat > "$tmp/f.c" <<'EOF'
int fib(n) int n; { if(n<2) return n; return fib(n-1)+fib(n-2); }
int main(){ return fib(10); }
EOF
( cd "$tmp" && "$BIN-cc" $CCOPT f.c -o f ) || fail "cc f.c failed"
"$SIM" "$tmp/f" >/dev/null 2>&1
check_eq "fib(10) -> exit 55" 55 "$?"

# 4. the produced file really is a classic 2.8BSD a.out our own tools read
sz=`"$BIN-size" "$tmp/h" | tail -1`
check_contains "linked image has a text segment" "$sz" "+"
check_contains "_main present" "`"$BIN-nm" \"$tmp/h\"`" "T _main"

exit 0
