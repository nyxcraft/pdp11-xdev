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
# One universal libc.a (flat lib/) serves THREE syscall-convention families,
# the stubs dispatching on __univ (which ld stamps and crt0 records):
#   v1, v2          First Edition inline-arg traps (ld emits the 0405 format)
#   v3..bsd29       the V7 family (inline/indirect; v3 is a best-guess on V5/V6)
#   bsd210/bsd211   the 4BSD family (stack args + 4.x numbers)
# Compile+run the battery under EACH to prove the one library serves all.
for u in v1 v2 v3 v4 v5 v6 v7 bsd1 bsd2 bsd279 bsd28 bsd29 bsd210 bsd211 sys3 sys5v2 ultrix1 ultrix2 ultrix3 ultrix31; do
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
das_one sys3   "$HOME/unix/sys3/bin/du"    # System III 0411 (split I&D)

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
run_era v4     "$HOME/unix/v4"
run_era v7     "$HOME/unix/v7"
run_era bsd210 "$HOME/bsd/2.10/root"
run_era bsd211 "$HOME/bsd/2.11/root"
run_era sys3   "$HOME/unix/sys3"
run_era ultrix1 "$HOME/unix/ultrix11/1.0"   # native V7M-11 1.0 /bin (echo+cat)
# The Ultrix line (1.0/2.0/3.0/3.1) shares one apsim personality, so a
# deterministic native V7M-11 binary must give the SAME answer under all four --
# proving 2.0/3.0/3.1 run real Ultrix binaries too, not just our compiler's
# output.  The native /bin was carved from the agn453/V7M-11 disk image into
# ~/unix/ultrix11/1.0 (V7 s5fs at image block 0); skips cleanly if absent.
USUM="$HOME/unix/ultrix11/1.0/bin/sum"
if [ -f "$USUM" ]; then
  ref=$(timeout 8 "$APSIM" -u ultrix1 "$USUM" /etc/passwd 2>/dev/null | head -1)
  for u in ultrix1 ultrix2 ultrix3 ultrix31; do
    o=$(timeout 8 "$APSIM" -u $u "$USUM" /etc/passwd 2>/dev/null | head -1)
    { [ -n "$o" ] && [ "$o" = "$ref" ]; } \
      && ok apsim "$u" "native V7M sum == across Ultrix ($o)" \
      || bad apsim "$u" "native sum [$o] != [$ref]"
  done
else skip apsim ultrix1 "no native V7M /bin"; fi
# 2.0's OWN /bin, carved from its install tape (a UNIX dump), runs under -u
# ultrix2: these use Ultrix's stack-argument syscall variant (sys N|0200) that
# apsim decodes.  A native `wc' must byte-match the host's count of /etc/passwd.
U2="$HOME/unix/ultrix11/2.0/bin/wc"
if [ -f "$U2" ]; then
  o=$(timeout 8 "$APSIM" -u ultrix2 "$U2" /etc/passwd 2>/dev/null | awk '{print $1,$2,$3}')
  h=$(wc /etc/passwd | awk '{print $1,$2,$3}')
  [ -n "$o" ] && [ "$o" = "$h" ] && ok apsim ultrix2 "native 2.0 tape wc == host ($o)" \
    || bad apsim ultrix2 "2.0 wc [$o] != host [$h]"
  # factor leans hard on FP11 ($literal FP operands + FT chop in its Newton
  # sqrt); ls needs sys time to keep its hands off the text after the trap.
  U2F="$HOME/unix/ultrix11/2.0/bin/factor"
  if [ -f "$U2F" ]; then
    o=$(echo 3600 | timeout 8 "$APSIM" -u ultrix2 "$U2F" 2>/dev/null | tr -s ' \n' ' ')
    [ "$o" = " 2 2 2 2 3 3 5 5 " ] && ok apsim ultrix2 "native 2.0 factor 3600 (FP11 FT/\$imm)" \
      || bad apsim ultrix2 "2.0 factor [$o]"
  fi
  U2LS="$HOME/unix/ultrix11/2.0/bin/ls"
  if [ -f "$U2LS" ]; then
    o=$(APSIM_ROOT="$HOME/unix/ultrix11/2.0" timeout 8 "$APSIM" -u ultrix2 "$U2LS" /bin 2>/dev/null | grep -c .)
    [ "$o" -ge 100 ] && ok apsim ultrix2 "native 2.0 ls lists /bin ($o entries)" \
      || bad apsim ultrix2 "2.0 ls listed only [$o] entries"
  fi
else skip apsim ultrix2 "no carved 2.0 /bin"; fi
# ultrix3/ultrix31: native 3.x /bin carved from the TUHS boot tapes (root
# dumps, oracle/native/v7dump.py).  3.x adds the "Berkeley 2.9 compatable"
# syscall block at 80+ (the 2.9 local sub-calls flattened: 82=lstat,
# 93=setpgrp, 99=gethostname...), which its ls needs -- and which must NOT
# answer under the pre-3.x universes (the negative half of the gate).
for uv in ultrix3:3.0 ultrix31:3.1; do
  u=${uv%:*}; d="$HOME/unix/ultrix11/${uv#*:}"
  if [ -f "$d/bin/wc" ]; then
    o=$(timeout 8 "$APSIM" -u $u "$d/bin/wc" /etc/passwd 2>/dev/null | awk '{print $1,$2,$3}')
    h=$(wc /etc/passwd | awk '{print $1,$2,$3}')
    [ -n "$o" ] && [ "$o" = "$h" ] && ok apsim $u "native ${uv#*:} wc == host ($o)" \
      || bad apsim $u "${uv#*:} wc [$o] != host [$h]"
    o=$(APSIM_ROOT="$d" timeout 8 "$APSIM" -u $u "$d/bin/ls" /bin 2>/dev/null | grep -c .)
    [ "$o" -ge 100 ] && ok apsim $u "native ${uv#*:} ls via the 80+ block ($o entries)" \
      || bad apsim $u "${uv#*:} ls listed only [$o] entries"
    o=$(echo 3600 | timeout 8 "$APSIM" -u $u "$d/bin/factor" 2>/dev/null | tr -s ' \n' ' ')
    [ "$o" = " 2 2 2 2 3 3 5 5 " ] && ok apsim $u "native ${uv#*:} factor 3600" \
      || bad apsim $u "${uv#*:} factor [$o]"
  else skip apsim $u "no carved ${uv#*:} /bin"; fi
done
# negative gate: a 3.x binary's sys 82 (lstat) must stay ENOSYS under the
# 2.0 universe -- era numbering must not leak backward down the Ultrix line.
U31LS="$HOME/unix/ultrix11/3.1/bin/ls"
if [ -f "$U31LS" ]; then
  o=$(APSIM_ROOT="$HOME/unix/ultrix11/3.1" timeout 8 "$APSIM" -u ultrix2 "$U31LS" /bin 2>/dev/null | grep -c .)
  # lstat(82) must be ENOSYS there, so ls reports "not found" (1 line)
  # rather than producing a listing -- a real listing is the era leak.
  [ "$o" -lt 100 ] && ok apsim ultrix2 "3.x lstat(82) correctly dead under ultrix2" \
    || bad apsim ultrix2 "3.x ls WORKED under ultrix2 (era leak)"
fi
# 1BSD/2BSD are userland layered on V6/V7 (no kernel of their own), so their
# personalities ARE v56/v7.  Validate that identity directly: a native V6/V7
# binary is byte-identical under bsd1/bsd2 and under v6/v7 -- then run a real
# Berkeley binary (2BSD csh, 1BSD ex) under its universe.
equiv(){ # label  base-universe  bsd-universe  native-binary
  [ -f "$4" ] || { skip apsim "$3" "no $2 binary"; return; }
  a=$("$APSIM" -u "$2" "$4" /etc/passwd 2>/dev/null | md5sum)
  b=$("$APSIM" -u "$3" "$4" /etc/passwd 2>/dev/null | md5sum)
  [ "$a" = "$b" ] && ok apsim "$3" "$2 native == under $3 (personality identity)" \
    || bad apsim "$3" "output differs from $2"
}
equiv bsd1 v6 bsd1 "$HOME/unix/v6/bin/ls"
equiv bsd2 v7 bsd2 "$HOME/unix/v7/bin/ls"
CSH2="$HOME/bsd/2bsd/bin/csh"
if [ -f "$CSH2" ]; then
  out=$(echo 'echo csh-ok' | timeout 8 "$APSIM" -u bsd2 "$CSH2" 2>/dev/null | head -1)
  [ "$out" = "csh-ok" ] && ok apsim bsd2 "2BSD csh runs a command" || bad apsim bsd2 "csh: [$out]"
else skip apsim bsd2 "no 2BSD csh"; fi
EX1="$HOME/bsd/1bsd/ex-1.1/a.outNOID"
if [ -f "$EX1" ]; then
  printf 'q\n' | timeout 8 "$APSIM" -u bsd1 "$EX1" >/dev/null 2>&1
  rc=$?
  { [ "$rc" != 124 ] && [ "$rc" -lt 128 ]; } \
    && ok apsim bsd1 "1BSD ex loads+runs (V6 a.out, rc=$rc)" || bad apsim bsd1 "ex hung/crashed rc=$rc"
else skip apsim bsd1 "no 1BSD ex"; fi
# 2.79BSD: also V7-personality userland (its csh uses no vfork, so v7 vs bsd2x
# is moot).  Personality identity vs v7 + its own updated csh running a command.
equiv bsd279 v7 bsd279 "$HOME/unix/v7/bin/ls"
CSH279="$HOME/bsd/2.79/bin.v6/csh"
if [ -f "$CSH279" ]; then
  out=$(echo 'echo two79-ok' | timeout 8 "$APSIM" -u bsd279 "$CSH279" 2>/dev/null | head -1)
  [ "$out" = "two79-ok" ] && ok apsim bsd279 "2.79BSD csh runs a command" || bad apsim bsd279 "csh: [$out]"
else skip apsim bsd279 "no 2.79 csh"; fi
# First Edition: run a surviving 0405 binary (ar prints usage on no args)
if [ -f "$HOME/unix/v1/unix72/fs/root/bin/mv" ]; then
  out=$(timeout 12 "$APSIM" -u v1 "$HOME/unix/v1/unix72/fs/root/bin/mv" 2>&1 | head -1)
  [ -n "$out" ] && ok apsim v1 "First Edition mv runs (0405)" || bad apsim v1 "no output"
else skip apsim v1 "no First Edition binaries"; fi
# Second Edition: v2 is a 0405/0407 mix -- run one of each under -u v2.
if [ -x "$HOME/unix/v2/bin/echo" ]; then
  out=$(APSIM_ROOT="$HOME/unix/v2" timeout 12 "$APSIM" -u v2 "$HOME/unix/v2/bin/echo" v2xyz 2>&1)
  case "$out" in v2xyz*) ok apsim v2 "Second Edition echo (0405)";; *) bad apsim v2 "echo: [$out]";; esac
fi
if [ -x "$HOME/unix/v2/bin/size" ]; then
  # size (0407) on itself -- text/data/bss must match das's independent read
  out=$(APSIM_ROOT="$HOME/unix/v2" timeout 12 "$APSIM" -u v2 "$HOME/unix/v2/bin/size" /bin/size 2>&1)
  das=$("$DAS" "$HOME/unix/v2/bin/size" 2>/dev/null | sed -n 's/.*text \([0-9]*\).*/\1/p')
  case "$out" in *"=*"|*"+"*) ok apsim v2 "Second Edition size runs (0407): $out";; *) bad apsim v2 "size 0407: [$out]";; esac
fi

echo "== E. as historical axes (--isa / --aout) =="
printf 'mov $1,r0\nsys 1\n' > "$tmp/v4.s"
"$AS" --isa=v4 -o "$tmp/v4.o" "$tmp/v4.s" 2>/dev/null && ok as v4 "--isa=v4 assembles" || bad as v4 "failed"
printf 'mfpt\nspl 7\n' > "$tmp/ext.s"
"$AS" --isa=extended -o "$tmp/ext.o" "$tmp/ext.s" 2>/dev/null && ok as extended "J-11/CIS mnemonics" || bad as extended "failed"
printf 'sys 1\n' > "$tmp/v1.s"
"$AS" --aout=v1 -o "$tmp/v1.o" "$tmp/v1.s" 2>/dev/null && ok as v1-aout "--aout=v1 (First Edition obj)" || bad as v1-aout "failed"

echo "== F. binutils read every a.out era (0405 First Edition .. 0411 split I&D) =="
# size's text must agree with das's independent read, across the header formats.
bin_size(){ # name  path
  [ -f "$2" ] || { skip size "$1" "no binary"; return; }
  st=$("$SIZE" "$2" 2>/dev/null | sed -n '2p' | awk '{print $1}')
  dt=$("$DAS" "$2" 2>/dev/null | grep -oE 'text [0-9]+' | head -1 | awk '{print $2}')
  [ -n "$st" ] && [ "$st" = "$dt" ] && ok size "$1" "text=$st (== das)" || bad size "$1" "size=$st das=$dt"
}
bin_size v1 "$HOME/unix/v1/unix72/fs/root/bin/mv"   # 0405 (6-word header)
bin_size v5 "$HOME/unix/v5/bin/ls"                  # 0407
bin_size v6 "$HOME/unix/v6/bin/ls"                  # 0410
bin_size v7 "$HOME/unix/v7/bin/ls"                  # 0410
# size on a 0431 overlay reports "N total text, overlays:(...)" (its own form)
CSH211="$HOME/bsd/2.11/root/bin/csh"
if [ -f "$CSH211" ]; then
  "$SIZE" "$CSH211" 2>/dev/null | grep -q "overlays:" && ok size b211 "0431 overlay breakdown" || bad size b211 "no overlay report"
fi
# nm reads the First Edition (0405) symbol table
V1MV="$HOME/unix/v1/unix72/fs/root/bin/mv"
if [ -f "$V1MV" ]; then
  n=$("$NM" "$V1MV" 2>/dev/null | grep -c .)
  [ "${n:-0}" -ge 5 ] && ok nm v1 "$n First Edition (0405) symbols" || bad nm v1 "only ${n:-0} symbols"
  # strip a copy: symbols gone, still runs under -u v1
  cp "$V1MV" "$tmp/mv405"
  "$STRIP" "$tmp/mv405" 2>/dev/null
  ns=$("$NM" "$tmp/mv405" 2>/dev/null | grep -c .)   # stderr "no name list" excluded
  out=$("$APSIM" -u v1 "$tmp/mv405" 2>&1 | head -1)
  [ "${ns:-1}" -eq 0 ] && [ -n "$out" ] && ok strip v1 "0405 stripped clean, still runs" \
    || bad strip v1 "$ns symbols left / out=[$out]"
else skip nm v1 "no First Edition binary"; fi

# ar reads the First Edition (0177555) archive format: t lists members, x
# extracts a real 0407 object das can read, and modify ops decline (read-only).
V2AR="$HOME/unix/v2/usr/lib/libc.a"
if [ -f "$V2AR" ]; then
  cp "$V2AR" "$tmp/v2libc.a"
  nm2=$("$AR" t "$tmp/v2libc.a" 2>/dev/null | grep -c .)
  [ "${nm2:-0}" -ge 20 ] && ok ar v2 "$nm2 members from 0177555 archive" \
    || bad ar v2 "only ${nm2:-0} members"
  ( cd "$tmp" && "$AR" x "$tmp/v2libc.a" exit.o 2>/dev/null )
  [ "$("$DAS" "$tmp/exit.o" 2>/dev/null | grep -oE 'magic [0-7]+' | awk '{print $2}')" = 0407 ] \
    && ok ar v2 "extracted 0407 member (das-clean)" || bad ar v2 "extract/das failed"
  rm -f "$tmp/exit.o"
  "$AR" r "$tmp/v2libc.a" /dev/null 2>/dev/null && bad ar v2 "modify not declined" \
    || ok ar v2 "modify of old-format archive declined"
else skip ar v2 "no First Edition archive"; fi

echo "== G. 4BSD syscall convention under 2.10/2.11 (the closed DEFER set) =="
# stat/fstat move to the 4.x number + 52-byte struct (repacked to our V7-shape
# header); wait uses wait4 (status through the pointer, not r1); exit passes the
# status on the stack.  One program exercises stat+fstat+pipe+fork+wait+exit and
# must give the SAME answer under the V7 family and the 4BSD family.
cat > "$tmp/syscov.c" <<'EOF'
#include <sys/types.h>
#include <sys/stat.h>
main(){
	struct stat sb; int fd,pid,st,p[2]; char b[4];
	stat("/etc/passwd",&sb);
	fd=open("/etc/passwd",0); fstat(fd,&sb); close(fd);
	pipe(p); pid=fork();
	if(pid==0){ write(p[1],"Z",1); _exit(7); }
	read(p[0],b,1); b[1]=0; wait(&st);
	printf("nz=%d pipe=%s wstat=%d\n", sb.st_size>0, b, (st>>8)&0377);
	exit(9);
}
EOF
for u in v7 bsd29 bsd210 bsd211; do
	if "$CC" --universe=$u -o "$tmp/syscov.$u" "$tmp/syscov.c" 2>/dev/null; then
		out=$(timeout 12 "$APSIM" -u $u "$tmp/syscov.$u" 2>&1); ec=$?
		[ "$out" = "nz=1 pipe=Z wstat=7" ] \
			&& ok syscov "$u" "stat+fstat+pipe+fork+wait -> $out" \
			|| bad syscov "$u" "[$out]"
		# exit(9) must reach apsim's own status (r0 for V7, 2(sp) for 4BSD)
		[ "$ec" -eq 9 ] && ok exit "$u" "exit(9) status" || bad exit "$u" "exit rc=$ec"
	else bad syscov "$u" "compile failed"; fi
done
# lstat is a 4.x-only call: under 2.10/2.11 it traps the 52-byte struct and
# repacks; it must return the same size as stat.
cat > "$tmp/lst.c" <<'EOF'
#include <sys/types.h>
#include <sys/stat.h>
main(){ struct stat a,b; stat("/etc/passwd",&a);
	if(lstat("/etc/passwd",&b)<0){printf("FAIL\n");exit(1);}
	printf("%d\n", a.st_size==b.st_size && b.st_size>0); exit(0); }
EOF
for u in bsd210 bsd211; do
	"$CC" --universe=$u -o "$tmp/lst.$u" "$tmp/lst.c" 2>/dev/null
	out=$(timeout 12 "$APSIM" -u $u "$tmp/lst.$u" 2>&1 | head -1)
	[ "$out" = "1" ] && ok lstat "$u" "4.x lstat == stat (52-byte repacked)" \
		|| bad lstat "$u" "[$out]"
done

echo "------------------------------------------------------------"
echo "cross-universe: passed $pass   failed $fail"
[ "$fail" -eq 0 ] || { echo "failing:$failed"; exit 1; }
exit 0
