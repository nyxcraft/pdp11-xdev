# ld: assemble + link classic 2.8BSD a.out and check the linked result.
# Two cases:
#   1. a self-contained program (one object, no undefined refs) -- checks the
#      output header (FMAGIC, relflg set = fully relocated) and symbol values.
#   2. a two-object program where object A calls a symbol defined in object B
#      -- checks that the external (REXT) reference in A is relocated to B's
#      final address (proves cross-object symbol resolution + relocation).
# The linked text is read with od/dd so the test needs only this toolchain.
. "$ROOT/tests/lib.sh"

tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

# word LABEL FILE BYTEOFFSET EXPECTED-OCTAL : read one 16-bit LE word
word() {
	w=`dd if="$2" bs=1 skip="$3" count=2 2>/dev/null | od -An -to2 | tr -d ' '`
	check_eq "$1" "$4" "$w"
}

# ---- case 1: self-contained program -----------------------------------
cat > "$tmp/t.s" <<'EOF'
.globl	_start
.text
_start:
	mov	$1,r0
	mov	$2,r1
	add	r1,r0
Lx:	jmp	Lx
.data
	0
EOF
"$BIN-as" -o "$tmp/t.o" "$tmp/t.s" || fail "as t.s failed"
"$BIN-ld" -o "$tmp/t.out" "$tmp/t.o" || fail "ld t.o failed"

# header: magic 0407, relflg 1 (no relocation bits left = fully linked)
word "magic"  "$tmp/t.out" 0  000407
word "relflg" "$tmp/t.out" 14 000001
# size: text=14 bytes, data=2
set -- `"$BIN-size" "$tmp/t.out" | tail -1`
check_eq "text size" 14 "$1"
check_eq "data size" 2 "$3"
# symbol: _start global text at 0
check_contains "_start global text" "`"$BIN-nm" \"$tmp/t.out\"`" "T _start"

# ---- case 2: two objects, external reference --------------------------
# A references _foo (defined in B) via an absolute-deferred jsr.
cat > "$tmp/a.s" <<'EOF'
.globl	_start
.globl	_foo
.text
_start:
	jsr	pc,*$_foo
Lh:	jmp	Lh
EOF
cat > "$tmp/b.s" <<'EOF'
.globl	_foo
.text
_foo:
	mov	$10,r0
	rts	pc
EOF
"$BIN-as" -o "$tmp/a.o" "$tmp/a.s" || fail "as a.s failed"
"$BIN-as" -o "$tmp/b.o" "$tmp/b.s" || fail "as b.s failed"
"$BIN-ld" -o "$tmp/ab.out" "$tmp/a.o" "$tmp/b.o" || fail "ld a.o b.o failed"

# A's text is 8 bytes, so B's _foo lands at text offset 010 (8).
nm=`"$BIN-nm" "$tmp/ab.out"`
check_contains "_foo resolved"  "$nm" "000010 T _foo"
check_contains "_start at 0"    "$nm" "000000 T _start"

# the jsr opcode word, then its operand = _foo's final address (relocated).
word "jsr opcode"       "$tmp/ab.out" 16 004737
word "jsr operand=_foo" "$tmp/ab.out" 18 000010
# header relflg set: fully linked, no undefined symbols
word "ab relflg" "$tmp/ab.out" 14 000001

# ---- case 3: data and bss symbol relocation ---------------------------
# In a 0407 image data loads after text and bss after data, so data/bss
# symbol values must be biased by the preceding segments' sizes.  Here
# text=8, data=4, so _d (data) lands at 010 and _b (bss) at 014; _p is a
# data word holding _d's absolute address.
cat > "$tmp/m.s" <<'EOF'
.globl	_start
.globl	_d
.globl	_b
.text
_start:
	mov	_d,r0
Lh:	jmp	Lh
.data
_d:	0444
_p:	_d
.bss
_b:	.=.+2
EOF
"$BIN-as" -o "$tmp/m.o" "$tmp/m.s" || fail "as m.s failed"
"$BIN-ld" -o "$tmp/m.out" "$tmp/m.o" || fail "ld m.o failed"
nm=`"$BIN-nm" "$tmp/m.out"`
check_contains "_d at data base 010" "$nm" "000010 D _d"
check_contains "_b at bss base 014"  "$nm" "000014 B _b"
# data word _p (file offset 16+text(8)+2) must hold _d's absolute addr 010
word "_p -> _d abs addr" "$tmp/m.out" 26 000010

exit 0
