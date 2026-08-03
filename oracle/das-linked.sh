#!/bin/sh
# das-linked.sh -- das round-trip over the LINKED binaries in ~/bsd/2.9:
# the GENERIC kernel and both rogue executables.  Linked files carry no
# relocation, so the tiers differ from the .o oracles:
#
#   unix (0430 overlay, 57 shipped GENERIC .o's alongside): the FULL replay --
#     das -a | as each shipped object (must be byte-identical), then relink
#     with the GENERIC Makefile's own recipe (ld -X -n ... -Z x7 -L) and
#     byte-compare the executable, 30KB symbol table included.
#
#   usr/70/rogue (flat 0411, STRIPPED): das -a -p | as of the whole file;
#     text+data content must be byte-identical.  (The 16-byte header holds
#     only link flags; there is no symtab or relocation to reproduce.  In a
#     stripped file das emits all-numeric operands -- exact by construction,
#     and immune to the separate-I&D text/data address overlap.)
#
#   usr/games/rogue (0430 overlay, unstripped): sliced into base text, each
#     overlay, and data windows (the ovlhdr gives the sizes; overlays share
#     one load window so a single linear .s cannot express them); each window
#     wrapped as a stripped object and content round-tripped.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
T29="${T29:-$HOME/bsd/2.9}"; G="$T29/usr/src/sys/GENERIC"
R="${CORPUS_WORK:-/tmp/daslinked.$$}"; rm -rf "$R"; mkdir -p "$R"
[ -n "$KEEP" ] || trap 'rm -rf "$R"' EXIT
pass=0; fail=0

# ---- kernel: object round-trip + full relink --------------------------------
if [ -f "$G/unix" ]; then
  n=0; bad=0
  for o in "$G"/*.o; do b=$(basename "$o")
    "$BIN-das" -a "$o" > "$R/t.s" 2>/dev/null
    if head -1 "$R/t.s" | grep -q ovas; then AV=-V; else AV=; fi
    "$BIN-as" $AV -o "$R/$b" "$R/t.s" 2>/dev/null
    cmp -s "$o" "$R/$b" && n=$((n+1)) || bad=$((bad+1))
  done
  CONF="l.o mch.o c.o ioconf.o boot.o"
  OV1="sys2.o sys4.o fio.o pipe.o alloc.o iget.o"; OV2="text.o ureg.o malloc.o sys1.o main.o mem.o"
  OV3="bio.o dkleave.o subr.o rm.o machdep.o sys3.o syslocal.o"; OV4="ttynew.o tty.o prim.o partab.o"
  OV5="acct.o prf.o ioctl.o kl.o sys.o ttyold.o rk.o dz.o tm.o lp.o"; OV6="ts.o xp.o dh.o hs.o"
  OV7="nami.o ht.o rp.o hk.o rl.o vp.o"; BASE="sig.o slp.o sysent.o clock.o trap.o rdwri.o dsort.o"
  (cd "$R" && "$BIN-ld" -X -n -o unix $CONF -Z $OV1 -Z $OV2 -Z $OV3 -Z $OV4 -Z $OV5 -Z $OV6 -Z $OV7 -L $BASE vers.o param.o) 2>/dev/null
  if [ "$bad" = 0 ] && cmp -s "$G/unix" "$R/unix"; then
    echo "  unix: 57/57 objects round-trip + relinked kernel FULL-FILE byte-identical ($(wc -c <"$R/unix") B)"; pass=$((pass+1))
  else echo "  unix: FAILED (objects bad=$bad, link $(cmp -s "$G/unix" "$R/unix" && echo ok || echo differs))"; fail=$((fail+1)); fi
else echo "  unix: skip (no $G/unix)"; fi

# ---- flat rogue: whole-file content ------------------------------------------
F="$T29/usr/70/rogue"
if [ -f "$F" ]; then
  "$BIN-das" -a -p "$F" > "$R/r.s" 2>/dev/null
  "$BIN-as" -o "$R/r.o" "$R/r.s" 2>/dev/null
  if [ -s "$R/r.o" ] && python3 - "$F" "$R/r.o" <<'PY'
import struct,sys
a=open(sys.argv[1],'rb').read(); b=open(sys.argv[2],'rb').read()
ta,da=struct.unpack('<2H',a[2:6]); tb,db=struct.unpack('<2H',b[2:6])
sys.exit(0 if ta==tb and da==db and a[16:16+ta+da]==b[16:16+tb+db] else 1)
PY
  then echo "  usr/70/rogue: text+data content byte-identical (82904 B)"; pass=$((pass+1))
  else echo "  usr/70/rogue: FAILED"; fail=$((fail+1)); fi
else echo "  usr/70/rogue: skip"; fi

# ---- overlay rogue: per-window content ---------------------------------------
F="$T29/usr/games/rogue"
if [ -f "$F" ]; then
  python3 - "$F" "$R" <<'PY'
import struct,sys
d=open(sys.argv[1],'rb').read(); R=sys.argv[2]
m,t,da,bs,sy,en,un,fl=struct.unpack('<8H',d[:16])
ov=struct.unpack('<7H',d[18:32]); off=32
def wrap(name,text,data):
    open(f'{R}/{name}','wb').write(struct.pack('<8H',0o407,len(text),len(data),0,0,0,0,1)+text+data)
wrap('w_base.o', d[off:off+t], b''); off+=t
for i,s in enumerate(ov):
    if s: wrap(f'w_ov{i+1}.o', d[off:off+s], b''); off+=s
wrap('w_data.o', b'', d[off:off+da])
PY
  ok=0; tot=0
  for w in "$R"/w_*.o; do tot=$((tot+1))
    "$BIN-das" -a -p "$w" > "$R/w.s" 2>/dev/null
    "$BIN-as" -o "$R/w.o" "$R/w.s" 2>/dev/null
    [ -s "$R/w.o" ] && python3 - "$w" "$R/w.o" <<'PY' && ok=$((ok+1))
import struct,sys
a=open(sys.argv[1],'rb').read(); b=open(sys.argv[2],'rb').read()
ta,da=struct.unpack('<2H',a[2:6]); tb,db=struct.unpack('<2H',b[2:6])
sys.exit(0 if ta==tb and da==db and a[16:16+ta+da]==b[16:16+tb+db] else 1)
PY
  done
  if [ "$ok" = "$tot" ] && [ "$tot" -gt 0 ]; then
    echo "  usr/games/rogue: $ok/$tot windows (base + overlays + data) content byte-identical"; pass=$((pass+1))
  else echo "  usr/games/rogue: FAILED ($ok/$tot)"; fail=$((fail+1)); fi
else echo "  usr/games/rogue: skip"; fi

echo "==== das-linked: $pass passed, $fail failed ===="
[ "$fail" = 0 ]
