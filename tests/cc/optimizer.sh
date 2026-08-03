# c2 peephole optimizer: `cc -O' runs the authentic 2.8BSD object-code
# improver.  For each program, the -O build must (a) produce the SAME runtime
# result as the unoptimized build, and (b) actually be no larger (usually
# smaller).  Exercises loops, recursion, calls, and the buffered-stdio path
# -- the cases that flushed out the c2 porting bugs (varargs copy(), the
# sbrk/malloc arena, and host printf("%c",0) emitting a NUL c2 must skip).
. "$ROOT/tests/lib.sh"

SIM="$BIN-apsim"
tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

# (name, source, expected exit code) -- run unoptimised and -O, compare both.
run_one() {
	name="$1"; src="$2"; want="$3"
	echo "$src" > "$tmp/o.c"
	( cd "$tmp" && "$BIN-cc"    o.c -o n ) || fail "$name: plain cc failed"
	( cd "$tmp" && "$BIN-cc" -O o.c -o o ) || fail "$name: cc -O failed"
	"$SIM" "$tmp/n" >/dev/null 2>&1; rn=$?
	"$SIM" "$tmp/o" >/dev/null 2>&1; ro=$?
	check_eq "$name: plain == expected" "$want" "$rn"
	check_eq "$name: -O == plain"       "$rn"   "$ro"
	# -O should not grow the text segment
	tn=`"$BIN-size" "$tmp/n" | tail -1 | awk '{print $1}'`
	to=`"$BIN-size" "$tmp/o" | tail -1 | awk '{print $1}'`
	[ "$to" -le "$tn" ] || fail "$name: -O text $to > plain $tn"
}

run_one "loop"       'int main(){int i,s;s=0;for(i=0;i<10;i++)s=s+i;return s;}' 45
run_one "recursion"  'int fib(n)int n;{return n<2?n:fib(n-1)+fib(n-2);}int main(){return fib(10);}' 55
run_one "nested"     'int f(x)int x;{return x+1;}int main(){return f(f(f(f(10))));}' 14
run_one "arrays"     'int main(){int a[5],i,s;for(i=0;i<5;i++)a[i]=i*i;s=0;for(i=0;i<5;i++)s=s+a[i];return s;}' 30

# buffered printf, optimised: the output (and exit) must be identical
cat > "$tmp/p.c" <<'EOF'
int main(){ printf("opt %d %s %x %o %d\n", 42, "ok", 255, 64, -7); return 0; }
EOF
( cd "$tmp" && "$BIN-cc" -O p.c -o p ) || fail "cc -O p.c failed"
check_eq "printf -O output" "opt 42 ok ff 100 -7" "`"$SIM" "$tmp/p" 2>/dev/null`"

# c2 must run cleanly and emit assemblable output.  cc silently falls back to
# the unoptimised c1 output when c2 fails, so a c2 crash would be invisible
# through `cc -O' alone -- invoke c2 directly here.  Recursion (fib) is the
# case whose call-return `mov%c' NUL once crashed c2.
cat > "$tmp/r.c" <<'EOF'
int fib(n)int n;{return n<2?n:fib(n-1)+fib(n-2);}int main(){return fib(10);}
EOF
( cd "$tmp" && "$BIN-cc" -S r.c )             || fail "cc -S r.c failed (c1 output)"
"$BIN-c2" "$tmp/r.s" "$tmp/r.opt.s"           || fail "c2 crashed/failed on r.s"
( cd "$tmp" && "$BIN-as" -o r.opt.o r.opt.s ) || fail "c2 output does not assemble"

exit 0
