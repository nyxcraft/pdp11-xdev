#!/bin/sh
# das-exec.sh [TREE] -- das content round-trip over every PROGRAM EXECUTABLE
# in a tree (default ~/bsd/2.9): files with a PDP-11 magic, exact header-implied
# length, not named *.o/*.a.  For each: `das -a -p | as', then byte-compare the
# TEXT+DATA content (the meaningful tier for linked binaries: symbol tables are
# not reproducible without the objects, and headers hold only link flags).
#
# das handles executables in CONTENT MODE: with no relocation it drops the
# symbol table and emits all-numeric operands -- exact by construction, immune
# both to separate-I&D (0411) D-space/text overlap and to labels whose address
# falls mid-instruction.  V6-linked binaries (2.79's bin.v6) leave a_flag CLEAR
# despite carrying no relocation; das believes the geometry instead.
#
# Overlay (0430) executables are covered per-window by das-linked.sh.
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
TREE="${1:-$HOME/bsd/2.9}"
R="${CORPUS_WORK:-/tmp/dasexec.$$}"; rm -rf "$R"; mkdir -p "$R"
[ -n "$KEEP" ] || trap 'rm -rf "$R"' EXIT

python3 - "$TREE" <<'PY' > "$R/list"
import os,struct,sys
root=sys.argv[1]
for dp,_,fns in os.walk(root):
    for fn in fns:
        p=os.path.join(dp,fn)
        if fn.endswith(('.o','.a')): continue
        try: d=open(p,'rb').read(16)
        except OSError: continue
        if len(d)<16: continue
        m,t,da,bs,sy,en,un,fl=struct.unpack('<8H',d)
        if m==0o405:
            # V1 12-byte-header executable: a_text INCLUDES the header;
            # words 2/3 are symbol and relocation sizes
            if t>=12 and os.path.getsize(p)==t+da+bs: print(p)
            continue
        if m not in (0o407,0o410,0o411): continue
        base=16+(t+da)*(1 if fl else 2)+sy
        sz=os.path.getsize(p)
        okf = sz==base
        if not okf and sy%8==0 and sz>base+4:
            # 2.11 string-table format: strtab (first long = its length) follows
            dd=open(p,'rb').read(base+4)
            tl=(struct.unpack('<H',dd[base:base+2])[0]<<16)|struct.unpack('<H',dd[base+2:base+4])[0]
            okf = tl>=4 and base+tl in (sz,sz-1)
        if okf: print(p)
PY

ok=0; fail=0; fl=""
cmp_exec(){ python3 - "$1" "$2" <<'PYEOF'
import struct,sys
a=open(sys.argv[1],'rb').read(); b=open(sys.argv[2],'rb').read()
tb,db=struct.unpack('<2H',b[2:6])
if struct.unpack('<H',a[0:2])[0]==0o405:
    ta=struct.unpack('<H',a[2:4])[0]-12
    sys.exit(0 if ta==tb+db and a[12:12+ta]==b[16:16+tb+db] else 1)
ta,da=struct.unpack('<2H',a[2:6])
sys.exit(0 if ta==tb and da==db and a[16:16+ta+da]==b[16:16+tb+db] else 1)
PYEOF
}
while read p; do
  OKF=0
  for SYSF in "" -6 -2; do
    "$BIN-das" -a -p $SYSF "$p" > "$R/e.s" 2>/dev/null || continue
    AV=; head -1 "$R/e.s" | grep -q nsym && AV="-n"
    "$BIN-as" $AV -o "$R/e.o" "$R/e.s" 2>/dev/null
    [ -s "$R/e.o" ] && cmp_exec "$p" "$R/e.o" && { OKF=1; break; }
  done
  if [ $OKF = 1 ]; then ok=$((ok+1)); else fail=$((fail+1)); fl="$fl ${p#$TREE/}"; fi
done < "$R/list"


echo "==== das-exec content round-trip over $TREE ===="
echo "  OK: $ok   FAIL: $fail"
[ -n "$fl" ] && { echo "  FAIL:"; for x in $fl; do echo "    $x"; done; }
[ "$fail" = 0 ]
