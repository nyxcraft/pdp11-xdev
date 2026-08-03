# as: assemble hand-written PDP-11 and check the encoding (the .o is the
# classic 2.8BSD format; we read the header/symbols with our own size/nm,
# and check a few known instruction encodings in the text by octal dump).
. "$ROOT/tests/lib.sh"

tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/t.s" <<'EOF'
.globl	_start
.text
_start:
	mov	$52,r0
	mov	(r1)+,r2
	clr	-(sp)
	mov	4(r5),r0
	beq	Lx
	jsr	pc,_sub
Lx:	rts	pc
.data
_d:	7
	_start
.globl	_sub
EOF
"$BIN-as" -o "$tmp/t.o" "$tmp/t.s" || fail "as exited nonzero"

# header via size: text=18 bytes, data=4
sz=`"$BIN-size" "$tmp/t.o" | tail -1`
set -- $sz
check_eq "text size" 20 "$1"

# symbols: _start global text, _sub/_d present
nm=`"$BIN-nm" "$tmp/t.o"`
check_contains "global _start" "$nm" "T _start"
check_contains "extern _sub"   "$nm" "U _sub"

# first instruction word: mov $52,r0 == 012700 octal
tsz=`od -An -j2 -N2 -tu2 "$tmp/t.o" | tr -d ' '`
w0=`dd if="$tmp/t.o" bs=1 skip=16 count=2 2>/dev/null | od -An -to2 | tr -d ' '`
check_eq "mov \$52,r0 opcode" 012700 "$w0"
# immediate word == 052 (42)
w1=`dd if="$tmp/t.o" bs=1 skip=18 count=2 2>/dev/null | od -An -to2 | tr -d ' '`
check_eq "immediate 52" 000052 "$w1"

# end-to-end: cc -c produces an object whose main is a global text symbol
echo 'int main(){return 42;}' > "$tmp/h.c"
( cd "$tmp" && "$BIN-cc" -c h.c ) || fail "cc -c failed"
check_contains "cc -c _main" "`"$BIN-nm" \"$tmp/h.o\"`" "T _main"

exit 0
