#!/bin/sh
# das-wide.sh [TREE] -- the WIDE das round-trip oracle: every archive member and
# loose object under a 2.9BSD tree (default ~/bsd/2.9), disassembled with
# `das -a', reassembled, and byte-compared FULL-FILE.  Complements das-sweep.sh
# (the 175-object libc corpus built from source): this corpus is the SHIPPED
# binaries -- native-built kernel objects, FORTRAN runtime, curses, the overlay
# (libov*) libraries, contrib MH, the pascal FP interpreters -- ~1130 objects
# of wildly varied idiom ("the kernel alone will lie to you by being too
# uniform", das/vax.md section 1).
#
# Replay tiers, per object (the build pipeline is metadata the .o doesn't
# record, so the harness replays the known 2.9 recipes -- vax.md section 6):
#   1. plain `as'
#   2. `as -V'      -- when das's banner says [ovas...]: an REXT reloc citing a
#                      DEFINED symbol marks overlay assembly (libov*, kernel)
#   3. + `ld -x -r' -- members the Makefiles post-process (libI77, contrib MH):
#                      locals stripped, so das's synthetic/local names vanish
#
# Result: OK(as) + OK(ld -x -r) vs DIFF/FAIL.  Run from a SHORT path (the K&R
# as has a filename-length buffer bug -- vax.md section 8).
HERE=$(cd "$(dirname "$0")" && pwd); BIN="$HERE/../bin/pdp11"
TREE="${1:-$HOME/bsd/2.9}"
R="${CORPUS_WORK:-/tmp/daswide.$$}"; rm -rf "$R"; mkdir -p "$R/obj"
[ -n "$KEEP" ] || trap 'rm -rf "$R"' EXIT

# --- corpus: every archive member + every loose .o (magic 0407..0411) --------
python3 - "$TREE" "$R/obj" <<'PY'
import os,struct,sys
tree,out=sys.argv[1],sys.argv[2]; n=0
for dp,_,fns in os.walk(tree):
    for fn in fns:
        p=os.path.join(dp,fn)
        tag=os.path.relpath(p,tree).replace('/','_')
        try: head=open(p,'rb').read(2)
        except OSError: continue
        if len(head)<2: continue
        m=struct.unpack('<H',head)[0]
        if m in (0o407,0o410,0o411) and not fn.endswith('.a'):
            d=open(p,'rb').read()
            if len(d)<16: continue
            t,da,bs,sy,en,un,fl=struct.unpack('<7H',d[2:16])
            if not fn.endswith('.o'):
                # a non-.o name qualifies only as a RELOCATABLE image
                # (ld -r output under a program name: ertst, V1's bos,
                # the 1972 kernels); linked executables belong to the
                # das-strip/das-sepid/das-kernels tiers
                if fl or len(d)!=16+2*(t+da)+sy: continue
            # an EXACT length is what a valid PDP-11 .o has: shorter files are
            # TRUNCATED (~/bsd/2.8's usr/doc/curses/*.o are damaged), longer
            # ones are foreign formats whose first word merely coincides with
            # a PDP-11 magic (~/bsd/2.79's usenet getdate.o is a VAX a.out)
            base=16+(t+da)*(1 if fl else 2)+sy
            if len(d) != base:
                # 2.11 string-table format: a strtab (its first PDP-11 long =
                # its total length) follows the symtab
                if len(d)<base+4 or sy%8: continue
                tl=(struct.unpack('<H',d[base:base+2])[0]<<16)|struct.unpack('<H',d[base+2:base+4])[0]
                if tl<4 or base+tl!=len(d): continue
            open(f'{out}/{n:04d}_{tag}','wb').write(d); n+=1
        elif fn.endswith('.a') and head==b'!<':
            # 2.11BSD: the PORTABLE ar format ("!<arch>\n", 60-byte text
            # headers: name[16] date[12] uid[6] gid[6] mode[8] size[10] "`\n")
            d=open(p,'rb').read()
            if d[:8]!=b'!<arch>\n': continue
            off=8
            while off+60<=len(d):
                if d[off+58:off+60]!=b'`\n': break
                name=d[off:off+16].rstrip(b' /\0').decode('latin1','replace')
                try: size=int(d[off+48:off+58].split()[0])
                except (ValueError,IndexError): break
                mb=off+60
                if mb+2<=len(d) and name and name!='__.SYMDEF':
                    mm=struct.unpack('<H',d[mb:mb+2])[0]
                    if mm in (0o407,0o410,0o411):
                        md=d[mb:mb+size]
                        # older 2.11 builds counted the ar EVEN-pad byte in
                        # ar_size; the object's own strtab length exposes it
                        # -- normalize to the true object length
                        if len(md)>=20:
                            t2,d2,b2,s2,e2,u2,f2=struct.unpack('<7H',md[2:16])
                            b3=16+(t2+d2)*(1 if f2 else 2)+s2
                            if s2%8==0 and b3+4<=len(md):
                                tl=(struct.unpack('<H',md[b3:b3+2])[0]<<16)|struct.unpack('<H',md[b3+2:b3+4])[0]
                                if tl>=4 and b3+tl==len(md)-1 and md[-1]==0:
                                    md=md[:-1]
                        open(f'{out}/{n:04d}_{tag}_{name.replace("/","_")}','wb').write(md); n+=1
                off=mb+size+(size&1)
        elif fn.endswith('.a') and m in (0o177545,0o177555):
            # 0177545: V7+/2.x ar -- 26-byte header, name[14], size a PDP-11
            #          long (HIGH word first)
            # 0177555: the V4..V6 ar (archive.5: "chosen to be unlikely to
            #          occur anywhere else") -- 16-byte header: name[8],
            #          mtime[4], uid[1], mode[1], size[2]; name field holds
            #          junk past its NUL
            d=open(p,'rb').read(); off=2
            hl = 26 if m==0o177545 else 16
            while off+hl<=len(d):
                if m==0o177545:
                    name=d[off:off+14].rstrip(b' \0').decode('latin1','replace')
                    size=(struct.unpack('<H',d[off+22:off+24])[0]<<16)|struct.unpack('<H',d[off+24:off+26])[0]
                else:
                    name=d[off:off+8].split(b'\0')[0].decode('latin1','replace')
                    size=struct.unpack('<H',d[off+14:off+16])[0]
                mb=off+hl
                if mb+2<=len(d) and name and name!='__.SYMDEF':
                    mm=struct.unpack('<H',d[mb:mb+2])[0]
                    if mm in (0o407,0o410,0o411):
                        open(f'{out}/{n:04d}_{tag}_{name.replace("/","_")}','wb').write(d[mb:mb+size]); n+=1
                off=mb+size+(size&1)
print(f'corpus: {n} objects')
PY

# a LINKED PROGRAM stored inside a file-bundle archive (1bsd's cont.a members
# named `a.out', `glob2', ...): no relocation, so the meaningful tier is
# text+data CONTENT, exactly as das-exec.sh treats loose executables
is_linked_content(){ python3 -c "
import struct,sys
a=open('$1','rb').read(); b=open('$2','rb').read()
t,da,bs,sy,en,un,fl=struct.unpack('<7H',a[2:16])
linked = fl==1 or len(a)==16+(t+da)+sy
tb,db=struct.unpack('<2H',b[2:6])
sys.exit(0 if linked and t==tb and da==db and a[16:16+t+da]==b[16:16+tb+db] else 1)"; }

ok=0; okld=0; okex=0; diff=0; fail=0; dl=""; flist=""
for o in "$R"/obj/*; do b=$(basename "$o")
  if ! "$BIN-das" -a -p "$o" > "$R/t.s" 2>/dev/null; then fail=$((fail+1)); flist="$flist $b(das)"; continue; fi
  if head -1 "$R/t.s" | grep -q ovas; then AV=-V;
  elif head -1 "$R/t.s" | grep -q v7as; then AV=-7; else AV=; fi
  head -1 "$R/t.s" | grep -q tab211 && AV="$AV --isa=bsd211"
  head -1 "$R/t.s" | grep -q nsym && AV="$AV -n"	# 2.11 string-table format
  "$BIN-as" $AV -o "$R/t.o" "$R/t.s" 2>/dev/null
  if [ ! -s "$R/t.o" ]; then fail=$((fail+1)); flist="$flist $b(as)"; continue; fi
  if cmp -s "$o" "$R/t.o"; then ok=$((ok+1)); rm -f "$R/t.o"; continue; fi
  rm -f "$R/t2.o"; "$BIN-ld" -x -r -o "$R/t2.o" "$R/t.o" 2>/dev/null
  if [ -s "$R/t2.o" ] && cmp -s "$o" "$R/t2.o"; then okld=$((okld+1)); rm -f "$R/t.o" "$R/t2.o"; continue; fi
  # `ld -X -r <member>.o' -- the 2.10 profiled-library recipe: -X keeps the
  # filename symbol (an N_FN whose NAME is the input path as given), so the
  # replay must present the object under its real member name
  case "$b" in *.a_*) mn="${b##*.a_}";; *) mn="${b##*_}";; esac
  if [ -n "$mn" ] && [ "$mn" != "$b" ]; then
    cp "$R/t.o" "$R/$mn" 2>/dev/null
    rm -f "$R/t3.o"; (cd "$R" && "$BIN-ld" -X -r -o t3.o "$mn" 2>/dev/null)
    if [ -s "$R/t3.o" ] && cmp -s "$o" "$R/t3.o"; then okld=$((okld+1)); rm -f "$R/t.o" "$R/t3.o" "$R/$mn"; continue; fi
    rm -f "$R/t3.o" "$R/$mn"
  fi
  # 2.11 new-format members post-processed `ld -X -r member.o' (or -x):
  # our 2.9-era ld does not speak the string-table format, so EMULATE the
  # single-object transform (N_FN insert, locals/globals resort, REXT remap)
  if head -1 "$R/t.s" | grep -q nsym; then
    case "$b" in *.a_*) mn="${b##*.a_}";; *) mn="${b##*_}";; esac
    ok211=0
    for XF in -X -x; do
      rm -f "$R/t4.o"
      python3 "$HERE/ldxr211.py" "$R/t.o" "$mn" "$R/t4.o" $XF 2>/dev/null
      if [ -s "$R/t4.o" ] && cmp -s "$o" "$R/t4.o"; then okld=$((okld+1)); ok211=1; break; fi
    done
    rm -f "$R/t4.o"
    if [ $ok211 = 1 ]; then rm -f "$R/t.o"; continue; fi
  fi
  if is_linked_content "$o" "$R/t.o"; then okex=$((okex+1)); rm -f "$R/t.o" "$R/t2.o"; continue; fi
  # V5/V6 sysent personality (`das -6'): trap 19. is seek with TWO inline
  # words, not V7's three-word lseek; `das -2' adds the 1972 sysent (trap
  # 33. octal = signal, ONE inline word) -- retry the whole replay under each
  # early multi-file `ld -r' (V4-era, old 12-byte symbols): the original's
  # N_FN entries record the link recipe -- inject them into our object at
  # their local-stream positions and remap the REXT indices
  if [ -s "$R/t.o" ]; then
    for LM in -k -X -x; do
      rm -f "$R/t5.o"
      python3 "$HERE/ldr72.py" "$R/t.o" "$o" "$R/t5.o" $LM 2>/dev/null
      if [ -s "$R/t5.o" ] && cmp -s "$o" "$R/t5.o"; then okld=$((okld+1)); rm -f "$R/t.o" "$R/t5.o"; break; fi
      rm -f "$R/t5.o"
    done
    [ ! -f "$R/t.o" ] && continue
  fi
  SYSOK=0
  for SYSF in -6 -2; do
    "$BIN-das" -a -p $SYSF "$o" > "$R/t.s" 2>/dev/null || continue
    if head -1 "$R/t.s" | grep -q ovas; then AV=-V;
    elif head -1 "$R/t.s" | grep -q v7as; then AV=-7; else AV=; fi
    head -1 "$R/t.s" | grep -q tab211 && AV="$AV --isa=bsd211"
    head -1 "$R/t.s" | grep -q nsym && AV="$AV -n"
    "$BIN-as" $AV -o "$R/t.o" "$R/t.s" 2>/dev/null
    if [ -s "$R/t.o" ] && cmp -s "$o" "$R/t.o"; then ok=$((ok+1)); rm -f "$R/t.o"; SYSOK=1; break; fi
    # garbage-name symtab entries reinserted from the original (unix.out)
    if [ -s "$R/t.o" ] && python3 "$HERE/ldgn.py" "$R/t.o" "$o" "$R/tg.o" 2>/dev/null \
       && cmp -s "$o" "$R/tg.o"; then ok=$((ok+1)); rm -f "$R/t.o" "$R/tg.o"; SYSOK=1; break; fi
    rm -f "$R/t2.o"; "$BIN-ld" -x -r -o "$R/t2.o" "$R/t.o" 2>/dev/null
    if [ -s "$R/t2.o" ] && cmp -s "$o" "$R/t2.o"; then okld=$((okld+1)); rm -f "$R/t.o" "$R/t2.o"; SYSOK=1; break; fi
  done
  [ "$SYSOK" = 1 ] && continue
  diff=$((diff+1)); dl="$dl $b"; rm -f "$R/t.o" "$R/t2.o"
done

echo "==== das-wide round-trip over $TREE ===="
echo "  OK: $ok   OK(ld -x -r): $okld   OK(linked-content): $okex   DIFF: $diff   FAIL: $fail"
[ -n "$dl" ] && { echo "  DIFF:"; for x in $dl; do echo "    $x"; done; }
[ -n "$flist" ] && { echo "  FAIL:"; for x in $flist; do echo "    $x"; done; }
[ "$fail" = 0 ]
