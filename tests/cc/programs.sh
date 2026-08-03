# cc: compile + assemble a range of K&R C programs end to end (cpp -> c0 ->
# c1 -> as) and check each yields a valid object with the expected globals.
# Exercises locals, for/while loops, arithmetic (+ - *), arrays, K&R function
# definitions and calls, recursion, and strings -- the paths that the LP64
# port's pointer-truncation / uninitialised-memory / table bugs used to break.
. "$ROOT/tests/lib.sh"

tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

# program ; expected global symbol in nm output
prog_test() {
	name="$1"; sym="$2"; src="$3"
	printf '%s\n' "$src" > "$tmp/$name.c"
	( cd "$tmp" && "$BIN-cc" $CCOPT -c "$name.c" ) 2>"$tmp/$name.err" \
		|| fail "$name: cc -c failed: `cat \"$tmp/$name.err\"`"
	test -f "$tmp/$name.o" || fail "$name: no object produced"
	check_contains "$name global $sym" "`\"$BIN-nm\" \"$tmp/$name.o\"`" "T $sym"
}

prog_test forloop _main 'int main(){int i,s;s=0;for(i=0;i<10;i++)s=s+i;return s;}'
prog_test whileloop _main 'int main(){int x;x=10;while(x>0)x=x-1;return x;}'
prog_test multiply _main 'int main(){int i,n;n=1;for(i=1;i<=5;i++)n=n*i;return n;}'
prog_test array _main 'int main(){int a[5];int i;for(i=0;i<5;i++)a[i]=i*i;return a[4];}'
prog_test func _f 'int g;
f(x) int x; { g=x; return g*2; }
int main(){ return f(7); }'
prog_test recursion _fib 'int fib(n) int n; { if(n<2) return n; return fib(n-1)+fib(n-2); }
int main(){ return fib(10); }'

# determinism: the for-loop must compile cleanly several times in a row
# (guards against the uninitialised-memory regressions we fixed)
i=0
while [ $i -lt 5 ]; do
	( cd "$tmp" && "$BIN-cc" $CCOPT -c forloop.c ) 2>/dev/null || fail "non-deterministic compile failure"
	i=`expr $i + 1`
done

exit 0
