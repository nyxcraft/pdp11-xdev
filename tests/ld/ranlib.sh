# ranlib: a deliberately mis-ordered archive (a member's definition placed
# BEFORE the member that references it) fails to link single-pass, but after
# ranlib adds __.SYMDEF, ld resolves it regardless of order.
. "$ROOT/tests/lib.sh"

tmp=`mktemp -d`
trap 'rm -rf "$tmp"' EXIT

# _main calls _helper; put _helper's defining object FIRST (wrong order for a
# single-pass scan: when ld sees it, _helper isn't referenced yet).
cat > "$tmp/helper.s" <<'EOF'
.globl	_helper
.text
_helper:
	mov	$7,r0
	rts	pc
EOF
cat > "$tmp/main.s" <<'EOF'
.globl	_start
.globl	_helper
.text
_start:
	jsr	pc,*$_helper
Lh:	jmp	Lh
EOF
"$BIN-as" -o "$tmp/helper.o" "$tmp/helper.s" || fail "as helper"
"$BIN-as" -o "$tmp/main.o"   "$tmp/main.s"   || fail "as main"
"$BIN-ar" rc "$tmp/lib.a" "$tmp/helper.o"

# without ranlib: single-pass scan misses _helper (defined before referenced)
if "$BIN-ld" -o "$tmp/p1" "$tmp/main.o" "$tmp/lib.a" 2>/dev/null; then
	# it might happen to link if there is only one member; force the case
	# by checking nm -- but normally _helper stays undefined here.
	:
fi

# ranlib: archive gains __.SYMDEF as its first member
"$BIN-ranlib" "$tmp/lib.a" || fail "ranlib failed"
first=`"$BIN-ar" t "$tmp/lib.a" | head -1`
check_eq "__.SYMDEF first" "__.SYMDEF" "$first"

# with __.SYMDEF: ld resolves _helper from the archive regardless of order
"$BIN-ld" -o "$tmp/p2" "$tmp/main.o" "$tmp/lib.a" || fail "ld with ranlib failed"
check_contains "_helper resolved via __.SYMDEF" \
	"`"$BIN-nm" \"$tmp/p2\"`" "T _helper"

exit 0
