#!/bin/sh
# Cross-universe test matrix: exercise every tool across every universe it
# can reach.  The two "full" universes (bsd28, bsd29) carry a libc, so the
# whole compiler pipeline + binutils run in each; the "sim" universes
# (v1, v5, v6, v7, bsd210, bsd211) have real vintage binaries, so apsim and
# das are tested against those.  Prints a pass/fail line per (tool, universe)
# and a summary; exits nonzero on any failure.
#
# Trees the sim tiers read (skipped cleanly if absent):
#   v1     ~/unix/v1/unix72/fs/root      (First Edition, 0405)
#   v5/6/7 ~/unix/v{5,6,7}
#   bsd210 ~/bsd/2.10/root   bsd211 ~/bsd/2.11/root
set -u
here=$(cd "$(dirname "$0")" && pwd); BIN="$here/../bin"
CC="$BIN/pdp11-cc"; AS="$BIN/pdp11-as"; LD="$BIN/pdp11-ld"
NM="$BIN/pdp11-nm"; SIZE="$BIN/pdp11-size"; STRIP="$BIN/pdp11-strip"
DAS="$BIN/pdp11-das"; AR="$BIN/pdp11-ar"; RANLIB="$BIN/pdp11-ranlib"
APSIM="$BIN/pdp11-apsim"; XSTR="$BIN/pdp11-xstr"; DCC="$BIN/pdp11-dcc"
pass=0; fail=0; failed=''
tmp=$(mktemp -d) || exit 1; trap 'rm -rf "$tmp"' EXIT
ok()  { pass=$((pass+1)); printf '  %-9s %-8s ok    %s\n' "$1" "$2" "$3"; }
bad() { fail=$((fail+1)); failed="$failed $1/$2"; printf '  %-9s %-8s FAIL  %s\n' "$1" "$2" "$3"; }
skip(){ printf '  %-9s %-8s skip  %s\n' "$1" "$2" "$3"; }

# ---- the C battery (each program prints a known line) -------------------
cat > "$tmp/hello.c" <<'EOF'
int main(){ printf("hello\n"); return 0; }
EOF
cat > "$tmp/arith.c" <<'EOF'
int fib(n) int n; { return n<2 ? n : fib(n-1)+fib(n-2); }
int main(){ printf("%d %d %d\n", fib(10), 7*6, 100/3); return 0; }
EOF
cat > "$tmp/str.c" <<'EOF'
int cmp(a,b) char *a,*b; { return *(int*)a - *(int*)b; }
int main(){ char b[32]; int v[5],i;
  strcpy(b,"ab"); strcat(b,"cd"); printf("%d ", strlen(b));
  v[0]=3;v[1]=1;v[2]=4;v[3]=1;v[4]=5; qsort(v,5,sizeof(int),cmp);
  for(i=0;i<5;i++) printf("%d",v[i]); printf("\n"); return 0; }
EOF
cat > "$tmp/float.c" <<'EOF'
int main(){ double x=1.0/3.0; printf("%.4f %.2e\n", x, x*300.0); return 0; }
EOF

echo "== A. compiler pipeline (cpp/c0/c1/c2/as/ld + libc) x full universes =="
# One universal libc.a (flat lib/) serves the whole V7-syscall-convention
# family; --universe only selects __univ, which ld stamps and crt0 records.
# We compile+run the battery under EACH to prove the single library serves all.
for u in v5 v6 v7 bsd28 bsd29; do
  [ -f "$here/../lib/libc.a" ] || { skip cc "$u" "no libc"; continue; }
  # plain and -O, for each battery program
  for prog in hello arith str float; do
    for opt in "" "-O"; do
      lbl="cc$opt"
      if "$CC" --universe=$u $opt "$tmp/$prog.c" -o "$tmp/$prog.$u" 2>"$tmp/err"; then
        out=$("$APSIM" -u $u "$tmp/$prog.$u" 2>&1)
        case "$prog:$out" in
        hello:hello)            ok "$lbl" "$u" "$prog -> $out" ;;
        arith:"55 42 33")       ok "$lbl" "$u" "$prog -> $out" ;;
        str:"4 11345")          ok "$lbl" "$u" "$prog -> $out" ;;
        float:"0.3333 1.00e+02")ok "$lbl" "$u" "$prog -> $out" ;;
        *) bad "$lbl" "$u" "$prog: [$out]" ;;
        esac
      else
        bad "$lbl" "$u" "$prog: compile failed ($(head -1 "$tmp/err"))"
      fi
    done
  done
  # cc -S / cc -c write to the source basename (authentic cc refuses -o
  # onto a .s/.o target), so run them in the tmp dir and collect the result.
  ( cd "$tmp" && "$CC" --universe=$u -S hello.c ) 2>/dev/null
  [ -f "$tmp/hello.s" ] && grep -qE "sys|mov|jsr" "$tmp/hello.s" \
    && ok "cc -S" "$u" "emits assembly" || bad "cc -S" "$u" "no asm"
  rm -f "$tmp/hello.s"
  ( cd "$tmp" && "$CC" --universe=$u -c hello.c ) 2>/dev/null
  [ -f "$tmp/hello.o" ] && { ok "cc -c" "$u" "emits object"; cp "$tmp/hello.o" "$tmp/obj.$u.o"; } \
    || bad "cc -c" "$u" "no object"
  rm -f "$tmp/hello.o"
done

echo "== B. binutils (nm/size/strip/das/ar/ranlib) x full universes =="
for u in bsd28 bsd29; do
  exe="$tmp/arith.$u"; obj="$tmp/obj.$u.o"
  [ -f "$exe" ] || { skip nm "$u" "no exe"; continue; }
  "$NM" "$exe" 2>/dev/null | grep -q . && ok nm "$u" "lists symbols" || bad nm "$u" "no symbols"
  "$SIZE" "$exe" 2>/dev/null | grep -qE "[0-9]" && ok size "$u" "reports sizes" || bad size "$u" "no sizes"
  cp "$exe" "$tmp/strip.$u"; "$STRIP" "$tmp/strip.$u" 2>/dev/null \
    && [ "$("$NM" "$tmp/strip.$u" 2>&1 | grep -c .)" -lt "$("$NM" "$exe" 2>/dev/null | grep -c .)" ] \
    && ok strip "$u" "removes symbols" || bad strip "$u" "no change"
  # das: the stripped exe disassembles linearly to stdout (a symboled a.out
  # is instead split into per-object .dis files -- also correct, just not
  # what this stdout check wants)
  ( cd "$tmp" && "$DAS" "strip.$u" 2>/dev/null ) | grep -qE '[0-9]{6}:' \
    && ok das "$u" "disassembles" || bad das "$u" "no decode"
  # ar + ranlib round trip on the cc -c object
  if [ -f "$obj" ] && rm -f "$tmp/lib.$u.a" \
     && "$AR" cr "$tmp/lib.$u.a" "$obj" 2>/dev/null && "$RANLIB" "$tmp/lib.$u.a" 2>/dev/null \
     && "$NM" "$tmp/lib.$u.a" 2>/dev/null | grep -qE "SYMDEF|main"; then
    ok ar+ranlib "$u" "archive + __.SYMDEF"
  else bad ar+ranlib "$u" "archive failed"; fi
done

echo "== C. das across every era's native binary =="
das_one(){ # name  path
  [ -f "$2" ] || { skip das "$1" "no binary"; return; }
  n=$("$DAS" "$2" 2>/dev/null | grep -cE ':\s+[0-9]{6}')
  [ "${n:-0}" -gt 20 ] && ok das "$1" "$n instructions decoded" || bad das "$1" "only ${n:-0} decoded"
}
das_one v1     "$HOME/unix/v1/unix72/fs/root/bin/ar"
das_one v5     "$HOME/unix/v5/bin/ls"
das_one v6     "$HOME/unix/v6/bin/ls"
das_one v7     "$HOME/unix/v7/bin/ls"
das_one bsd29  "$HOME/bsd/2.9/usr/bin/ls"
das_one bsd210 "$HOME/bsd/2.10/root/bin/ls"
das_one bsd211 "$HOME/bsd/2.11/root/bin/ls"

echo "== D. apsim runs each era's native binaries =="
run_era(){ # universe  root  echo-arg-test
  u=$1; R=$2
  [ -x "$R/bin/echo" ] || { skip apsim "$u" "no $R/bin/echo"; return; }
  out=$(APSIM_ROOT="$R" timeout 12 "$APSIM" -u $u "$R/bin/echo" xyzzy 2>&1)
  [ "$out" = "xyzzy" ] && ok apsim "$u" "echo" || { bad apsim "$u" "echo: [$out]"; return; }
  if [ -x "$R/bin/cat" ] && [ -f "$R/etc/passwd" ]; then
    out=$(APSIM_ROOT="$R" timeout 12 "$APSIM" -u $u "$R/bin/cat" /etc/passwd 2>&1 | head -1)
    case "$out" in root*) ok apsim "$u" "cat /etc/passwd" ;; *) bad apsim "$u" "cat: [$out]" ;; esac
  fi
}
run_era v5     "$HOME/unix/v5"
run_era v6     "$HOME/unix/v6"
run_era v7     "$HOME/unix/v7"
run_era bsd210 "$HOME/bsd/2.10/root"
run_era bsd211 "$HOME/bsd/2.11/root"
# First Edition: run a surviving 0405 binary (ar prints usage on no args)
if [ -f "$HOME/unix/v1/unix72/fs/root/bin/mv" ]; then
  out=$(timeout 12 "$APSIM" -u v1 "$HOME/unix/v1/unix72/fs/root/bin/mv" 2>&1 | head -1)
  [ -n "$out" ] && ok apsim v1 "First Edition mv runs (0405)" || bad apsim v1 "no output"
else skip apsim v1 "no First Edition binaries"; fi

echo "== E. as historical axes (--isa / --aout) =="
printf 'mov $1,r0\nsys 1\n' > "$tmp/v4.s"
"$AS" --isa=v4 -o "$tmp/v4.o" "$tmp/v4.s" 2>/dev/null && ok as v4 "--isa=v4 assembles" || bad as v4 "failed"
printf 'mfpt\nspl 7\n' > "$tmp/ext.s"
"$AS" --isa=extended -o "$tmp/ext.o" "$tmp/ext.s" 2>/dev/null && ok as extended "J-11/CIS mnemonics" || bad as extended "failed"
printf 'sys 1\n' > "$tmp/v1.s"
"$AS" --aout=v1 -o "$tmp/v1.o" "$tmp/v1.s" 2>/dev/null && ok as v1-aout "--aout=v1 (First Edition obj)" || bad as v1-aout "failed"

echo "------------------------------------------------------------"
echo "cross-universe: passed $pass   failed $fail"
[ "$fail" -eq 0 ] || { echo "failing:$failed"; exit 1; }
exit 0
