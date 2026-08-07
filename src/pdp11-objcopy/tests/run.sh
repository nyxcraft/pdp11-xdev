#!/bin/sh
# pdp11-objcopy: identity copy + loadable-image/segment extraction checked
# against slices computed independently, plus the hostile-header guards.
# Hermetic -- a synthetic a.out is built here; the committed kernel object is
# only an identity-copy smoke.
HERE=$(cd "$(dirname "$0")" && pwd)
OC="$HERE/../../../bin/pdp11-objcopy"
FIX="$HERE/../../../tests/fixtures/dkleave.o"
W=$(mktemp -d) || exit 1
trap 'rm -rf "$W"' EXIT
pass=0
fail=0
ck() { # ck <got> <want> <name>
	if [ "$1" = "$2" ]; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
		echo "  FAIL $3: got [$1] want [$2]"
	fi
}

# a 0407 a.out: 16-byte header (a_text=4, a_data=6, a_syms=12), then 4 text +
# 6 data + 12 symtab bytes with recognizable contents.
python3 - "$W" <<'PY'
import sys, struct
W = sys.argv[1]
text = bytes([0xC0, 0xDE, 0x11, 0x22])
data = bytes([0xDA, 0x7A, 0x33, 0x44, 0x55, 0x66])
syms = bytes(range(12))
hdr = struct.pack("<hHHHHHHH", 0o407, len(text), len(data), 0, len(syms), 0, 0, 0)
open(W + "/s.o", "wb").write(hdr + text + data + syms)
open(W + "/text.ref", "wb").write(text)
open(W + "/data.ref", "wb").write(data)
open(W + "/img.ref", "wb").write(text + data)
PY

"$OC" "$W/s.o" "$W/id.out"
ck "$(cmp -s "$W/s.o" "$W/id.out" && echo Y || echo N)" Y "identity copy byte-identical"

"$OC" -O binary "$W/s.o" "$W/img.out"
ck "$(cmp -s "$W/img.out" "$W/img.ref" && echo Y || echo N)" Y "-O binary == text+data image"

"$OC" -j text "$W/s.o" "$W/text.out"
ck "$(cmp -s "$W/text.out" "$W/text.ref" && echo Y || echo N)" Y "-j text == text segment"

"$OC" -j data "$W/s.o" "$W/data.out"
ck "$(cmp -s "$W/data.out" "$W/data.ref" && echo Y || echo N)" Y "-j data == data segment"

# hostile header: a_text = 0xffff far exceeds the file -> must refuse
python3 -c "d=bytearray(open('$W/s.o','rb').read()); d[2]=0xff; d[3]=0xff; open('$W/bad.o','wb').write(d)"
"$OC" -O binary "$W/bad.o" "$W/bad.out" 2>/dev/null
ck "$?" 1 "refuses oversized a_text"

# not an a.out -> must refuse
printf 'not an a.out at all........' >"$W/junk"
"$OC" "$W/junk" "$W/junk.out" 2>/dev/null
ck "$?" 1 "refuses bad magic"

# real committed 0407 kernel object: identity copy round-trips
"$OC" "$FIX" "$W/k.out"
ck "$(cmp -s "$FIX" "$W/k.out" && echo Y || echo N)" Y "identity copy of a real .o"

echo "pdp11-objcopy: $pass passed, $fail failed"
[ "$fail" = 0 ]
