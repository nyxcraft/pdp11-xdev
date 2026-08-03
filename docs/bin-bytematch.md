# Byte-matching the shipped /bin utilities (nm, strip, ar)

Can the toolchain reproduce the standard 2.9BSD command binaries bit-for-bit,
the way it reproduces rogue?  This is the running record of that experiment.
It doubles as the log of era-source discrepancies we find along the way (the
user's rule: *fix our source for things like that, but keep a record*).

## Targets

The shipped binaries live in the root filesystem, inside the dump image, not on
the extracted tree:

    ~/bsd/2.9/rootdump  ->  /bin/nm  /bin/strip  /bin/ar   (+ /lib/libc.a, /lib/crt0.o)

Extract with the old-`dump(5)` reader (1024-byte records; the same one used for
/lib/libc.a — script kept in the session scratchpad).  All three are **0410
(pure text), fully stripped** (0-byte symbol table):

| bin    | bytes | text | data | bss  |
|--------|-------|------|------|------|
| nm     | 6272  | 5760 | 496  | 1124 |
| strip  | 5612  | 4288 | 1308 | 1146 |
| ar     | 9844  | 7872 | 1956 | 1764 |

Source: `~/bsd/2.9/usr/src/cmd/{nm,strip,ar}.c`.  Build recipe (from that dir's
`MAKE`): `cc -n -O -s <f>.c -o <f>`  (`-n`=0410, `-O`=c2, `-s`=strip; `L=` empty).

Because the targets are stripped, only text+data must match — the libc sys.s
symbol flood is irrelevant here (see [libc-era](../Makefile) / memory).  But the
libc *members'* text+data must match, so link against **libc-era.a + crt0-era.o**
(the shipped-era library), not the source libc.a.  crt0's text+data are
identical between the two anyway (only its stripped symbols differ).

Link (bypassing cc, which forces `-lc` + `-X`):

    ld -X -n -s -o strip crt0-era.o strip.o -lc-era

## Results

### strip — BYTE-IDENTICAL ✅

Two things were needed, both era-accurate:

1. **libc-era.a** with the era `errlst.o` (below) — strip pulls `sys_errlist`
   via perror; the default UCB_NET errlst is 870 data bytes larger.
2. **One missing source line.**  The shipped strip carries the SCCS what-string
   `@(#)strip.c\t2.4`, laid out as a `char *` (pointer word 0x2004 + string) at
   the head of strip.o's data.  Our `~/bsd/2.9` checkout of strip.c has **no
   sccsid line at all**.  The shipped 2.4 was built from a source whose `%W%`
   keyword expanded to it.  Reconstructed source fix — prepend to strip.c:

       char	*sccsid = "@(#)strip.c\t2.4";

   With both, the linked+stripped output equals /bin/strip byte-for-byte, and
   runs under apsim.

### nm, ar — a source-revision gap, NOT a toolchain defect ⚠️

nm is +64 text/+40 data, ar +128/+106.  We ran the diagnosis the whole way down
against the **native 2.9 compiler under apsim** (extract /lib/{cpp,c0,c1,c2,as,
as2} from the rootdump; apsim needs SHORT paths — long ones truncate the
compiler's filename buffer and it dies with a bogus "Symbol table overflow"; and
`APSIM_ROOT` roots the guest fs so `as` finds `/lib/as2` and `cpp` finds
`/usr/include`).  Feeding the SAME preprocessed input to both toolchains:

- **cpp + headers**: our nm.i code == native cpp's nm.i code (line-markers, which
  embed differing paths, stripped).  Our headers are byte-identical to the
  image's for every include.
- **c0**: our c0 intermediate (t1) == native c0's, byte-for-byte.
- **c1**: our c1 output assembles to the same .o as native c1's.
- **c2**: our c2 output assembles to the same .o as native c2's.

So **our whole compiler chain (cpp→c0→c1→c2) is byte-identical to the native 2.9
compiler.**  The residual is therefore NOT our codegen.  By arithmetic the
shipped nm.o carries ~1504 text bytes, but the archived nm.c compiles (via that
faithful chain) to ~1566–1568 — ~62 bytes more.  Since the chain is faithful,
the shipped /bin/nm (inode-dated **1986**, four years after the archived nm.c's
1982-08-31) was built from a **different nm.c revision** than the source on the
image.  Same story for ar (c0/c1/c2 all verified identical).  Unlike strip's
recoverable sccsid, this is a code-level source-provenance gap: the image's
`/bin` binaries post-date its `/usr/src` tree, and we don't have the exact
sources they were built from.  Not a toolchain bug — closed on that front.

### The assembler branch-relaxation edge -- CLOSED (one-character fix)

Chasing nm's +64 down to the byte turned up a real 2-byte `as` difference, fully
diagnosed via the native passes under apsim, and now fixed:

- Our `as` emitted nm.o text 1568; native `as` emits 1566.  The extra word was
  at text `002062` (octal): a `jbr L62` that native keeps as `br` (1 word) but
  ours promoted to `jmp` (2 words).  L62 is a forward branch whose corrected
  displacement lands exactly at `r0 == 256`.
- Native `betwen` (as22.s) is INCLUSIVE of both bounds: a `jbr`/`jxxx` is short
  iff `-254 <= r0 <= 256` (a `br` reaches `target-dot == 256` exactly: offset
  +127 words = dot+2+254).  Our as.c `spanrec` used `r0 > 255` -- an off-by-one
  that excluded 256 and over-promoted the branch.

**Fix:** `r0 > 255` -> `r0 > 256` in as.c `spanrec`.  That is the whole change;
our `brdelt` drift reproduction was already correct.

An earlier attempt at this same fix *looked* like it broke rogue, but that was a
confound: this session's library rebuilds had left the flat-rogue full binary
diverging (+64 text / +774 data) on their own -- the +774 is libc's UCB_NET
`errlst` (rogue links `-lc`), nothing to do with `as`.  Re-tested cleanly with
the native-`as`-under-apsim oracle, the boundary fix matches native on
EVERYTHING: all 28 rogue modules (fight.o included), nm, and ar assemble
byte-identically to native `as`.  Toolchain tests 25/25.

The bar is matching native `as` (the true assembler, itself verified to
reproduce rogue), not the `~/bsd/2.9` binary -- the full rogue binary is
expected to drift once we move to era libraries, and that is fine.

## Era-source discrepancies recorded

- **strip.c** (`usr/src/cmd/strip.c`): our checkout is missing
  `char *sccsid = "@(#)strip.c\t2.4";`.  nm.c/ar.c correctly have none.
- **errlst.c**: our `libc/gen/errlst.c` is Berkeley 4.4 (82/04/01), built
  `-DUCB_NET`.  The shipped 2.9 libc used an **earlier** errlst.c — 37 entries
  (0..EWOULDBLOCK), **no ELOOP** (symlinks didn't exist yet — the rev0
  fingerprint), and EQUOT worded **"Quota exceeded"** not "Disk quota exceeded".
  Reconstructed verbatim from the shipped errlst.o as `libc/gen/errlst-era.c`
  (compiles `-O` to a byte-identical errlst.o); wired into `make libc-era`.
