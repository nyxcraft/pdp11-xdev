# Floating point: c1 emits FP11 codegen (movf/addf/mulf/cmpf/...), as encodes
# it, and apsim emulates the FP11 accumulators in DEC F/D format.  Each program
# computes a float result, converts to int, and returns it; we check the exit
# code.  Covers arithmetic, comparison, int<->float conversion, negation, and
# negative constants -- the cases that exercised the DEC-format constant
# emission and the FP11 instruction decode.
. "$ROOT/tests/lib.sh"

SIM="$BIN-apsim"
tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

# (name, source, expected exit code)
t() {
	echo "$2" > "$tmp/z.c"
	( cd "$tmp" && "$BIN-cc" z.c -o z ) || fail "$1: cc/link failed"
	"$SIM" "$tmp/z" >/dev/null 2>&1
	check_eq "$1" "$3" "$?"
}

t "add"        'int main(){ float a,b; a=3.5; b=2.0; return (int)(a+b); }' 5
t "sub"        'int main(){ float a,b; a=5.0; b=1.5; return (int)(a-b); }' 3
t "mul"        'int main(){ float a,b; a=2.5; b=4.0; return (int)(a*b); }' 10
t "div"        'int main(){ float a,b; a=9.0; b=2.0; return (int)(a/b); }' 4
t "gt true"    'int main(){ float a; a=2.5; return a>2.0?7:1; }' 7
t "lt false"   'int main(){ float a; a=2.5; return a<2.0?7:1; }' 1
t "eq"         'int main(){ float a; a=2.0; return a==2.0?7:1; }' 7
t "int->float" 'int main(){ int i; float f; i=9; f=i; f=f/2.0; return (int)f; }' 4
t "negate var" 'int main(){ float a; a= -2.5; return (int)(-a); }' 2
t "neg const"  'int main(){ float a; a= -6.0; return a< -5.0 ? 8 : 1; }' 8
t "neg arith"  'int main(){ float a,b; a= -3.0; b= -4.0; return (int)(a*b); }' 12
t "abs path"   'int main(){ float a; a= -3.0; if(a<0.0) a= -a; return (int)a; }' 3
t "sum loop"   'int main(){ float s; int i; s=0.0; for(i=0;i<5;i++) s=s+1.5; return (int)s; }' 7
t "global"     'float g; int main(){ g=2.5; g=g*g; return (int)g; }' 6
t "ldexp"      'double ldexp(); int main(){ return (int)ldexp(1.0,4); }' 16
t "modf"       'double modf(); int main(){ double ip; modf(7.25,&ip); return (int)ip; }' 7

# --- float printing (%f/%e/%g via the authentic fltpr.s + ecvt/fcvt/gcvt) ---
cat > "$tmp/p.c" <<'EOF'
int main(){ printf("%f %e %g\n", 3.5, 31400.0, 42.5); return 0; }
EOF
( cd "$tmp" && "$BIN-cc" p.c -o p ) || fail "printf float: cc failed"
check_eq "printf %f/%e/%g" "3.500000 3.140000e+04 42.5" "`"$SIM" "$tmp/p" 2>/dev/null`"

# --- atof + scanf %f (the scanf family, re-enabled now float assembles) ----
cat > "$tmp/s.c" <<'EOF'
double atof();
int main(){
	double f; int a, b;
	printf("atof=%g\n", atof("2.5") * 2.0);
	sscanf("12 34", "%d %d", &a, &b);
	sscanf("3.25", "%lf", &f);
	printf("scan %d %d %g\n", a, b, f * 4.0);
	return 0;
}
EOF
( cd "$tmp" && "$BIN-cc" s.c -o s ) || fail "atof/scanf: cc failed"
check_eq "atof + scanf" "atof=5
scan 12 34 13" "`"$SIM" "$tmp/s" 2>/dev/null`"

exit 0
