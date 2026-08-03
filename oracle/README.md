# Native-tool oracle

Opt-in verification suites that cross-check the toolchain against the
**original PDP-11 binaries** run under `apsim`.  Not part of `make check`:
the native fixtures are copyrighted vintage binaries, never committed, and
are regenerated locally from the distribution trees:

    sh native/setup.sh ~/bsd/2.8              # 1981 c0/c1/c2 (2.8 oracle)
    python3 native/extract-rootdump.py        # 2.9 as/ld/ar/... from the rootdump
    sh native/bsd28/build-c1.sh               # 2.8-era rebuild variants

Scripts assume the merged layout: tools in `../bin/pdp11-*`, era libraries in
`../lib/<universe>/`.  Headline results below were established in the
pdp11-bsd29-toolchain tree at import time (see NOTES.md); re-run after any
as/ld/ar/das change.

- `cross-universe.sh` — the tool x universe matrix: the whole compiler
  pipeline + binutils built and run in both full universes (bsd28, bsd29),
  `das` disassembling native binaries from every era (First Edition
  through 2.11BSD), `apsim` running each era's real `/bin` binaries, and
  `as`'s historical `--isa`/`--aout` axes.  Wired into `make check`; the
  era-binary tiers skip cleanly when a distribution tree is absent.

Cross-checks our ported tools against the **original PDP-11 tool binaries** run
under `apsim`. The native binaries are ground truth: a mismatch means our port
diverged from the tool that actually built the system.

Four harnesses:

- `run.sh` — the small hand-curated compiler corpus (`corpus/*.c`, one tiny
  function each), comparing our `c1`/`c2` to the 1981 2.8BSD passes (below).
- `as-corpus.sh [tree]` — assembles **every `.s`** under a tree with our `as`
  and the native 2.9 `as`, comparing the `.o`. Options-aware (prepends `sys.s`
  for the syscall-stub dirs). Tree-wide generalisation of the fight.o/nm check.
- `cc-corpus.sh [tree]` (set `CFLAGS` to match the native build) — for each
  `.c`: **AS** = compile to `.s` with our cc, assemble with our vs native `as`;
  **CC** = feed the same `.i` to our vs native `c0/c1/c2`, assemble both,
  compare. The CC check is bounded by apsim's ~64KB flat memory (large -> skip).
- `tree-sweep.sh TREE [EXTRA_CFLAGS]` — the whole-subtree AS oracle that chases
  the *compile* tail. Per `.c`, it compiles with the **options the native build
  actually used**, harvested automatically: each directory's own `Makefile` `-D`
  flags (f77 `-DFAMILY=DMR`, ls `-DUCB_PWHASH`, …), cross-dir `-I` for headers
  that live in sibling dirs (`ndir.h`←LIBNDIR, `mfile1/2`←mip, `macdefs`←pcc),
  and **yacc token headers generated on the fly** (`y.tab.h`/`e.def`, absent from
  the source tree — regenerated with the host `yacc`; token *numbers* are
  irrelevant since both assemblers see the same `.s`). Then it assembles the
  output with our `as` vs native `as`. Vestigial trees (`OLD_*`, `*/old/`,
  `lpr.old`, `oldcsh` — none have a Makefile) and the VAX-only uucp `syskludge`
  are skipped.
- `lib-sweep.sh [TREE]` — the **ar/ranlib** byte-match oracle over every `*.a` in
  the tree. For each archive it extracts the object members and rebuilds with our
  ar vs the native `/bin/ar` under apsim (member mtimes zeroed first — apsim's
  `stat` reports mtime 0 and passes uid/gid/mode/size through, so that is the only
  field to normalize). For ranlib'd libraries it runs **native ranlib end-to-end
  under apsim** (it `system("ar rlb")`s via `/bin/sh` to insert `__.SYMDEF`) and
  byte-compares the `__.SYMDEF` member content against our ranlib's — only that
  member's `time()+5` header date is nondeterministic and `ar x` drops it. This
  needs the apsim **lseek fix** (lseek returns its long result in the r0:r1 pair;
  without it `ftell()` is garbage and ranlib/nm truncate a multi-member archive
  after the first member). Result over `~/bsd/2.9`: ar **39/39**, ranlib **26/26**,
  0 diffs (2 `*.a` are not archives — `csh.a` is text, `libfp.a` a raw a.out).

- `ld-sweep.sh TREE...` — the **ld** byte-match oracle. Builds the full 2.9
  `libc.a` from source (the distribution's own `compall`+`mklib`, run through our
  cc/as/ar/ranlib — 175 members) plus the shipped curses/termcap/m (re-ranlib'd),
  then for each program links its objects (compiled with our cc) with **our ld and
  the native `/bin/ld` under apsim** on identical inputs — `ld -X crt0.o -o out
  OBJ... -lcurses -ltermlib -lm -lc`, mirroring cc(1) — and byte-compares. Covers
  single-source programs (top-level `.c`) and multi-source program dirs (all `.c`
  in the dir). Result over `cmd ucb games local`: **213 clean + 25 partial links,
  0 DIFFER**. (Partial = undefined externals remained but both linkers still
  agreed byte-for-byte; skip = the whole-dir object heuristic didn't match the
  program — multi-program dirs, grammar variants — not an ld result.)

- `kernel-link.sh` — the heaviest ld test: links the whole GENERIC kernel `unix`
  with our ld and native `/bin/ld` under apsim and byte-compares. Exercises the
  **overlay linker** (0430 a.out): `ld -X -n -o unix CONFOBJ -Z seg1..-Z seg7
  -L base vers.o param.o` — 57 objects across 7 overlay segments + resident base,
  built with our cc/as (kernel recipe; param.c `-DMAXUSERS=4`; locore l.s/mch.s
  get `-DLOCORE` + the config-generated assym.s). Result: **unix byte-identical,
  116842 B, magic 0430** — our ld reproduces a bootable overlay kernel to the byte.

- `selfhost.sh` — rebuild the 2.9 **toolchain's own binaries** from source with our
  toolchain and byte-compare to the on-disk native ones. `as`/`as2` are pure
  assembly (`as1?.s`/`as2?.s` + sys.s, `ld -n -s a.out`, no crt0/libc), so they
  exercise only our as+ld — both **byte-identical** to native `/bin/as` and
  `/lib/as2`. `strip` (a C tool) is byte-identical too, given the era libc + the
  sccsid our source checkout lacks (recovered from the binary). Result **3/3**.
  (nm/ar can't match — the archived .c predates the 1986 build-time revision;
  apsim separately proves the cpp→c0→c1→c2 chain is byte-exact, so it's source
  drift, not a tool defect.)

- `das-sweep.sh` — the **das round-trip** oracle: disassemble each object with
  `das -a` (reassemblable source), reassemble with our `as`, and byte-compare to
  the original `.o`. A full round-trip (das → as → identical object) is the
  strongest disassembler-fidelity test — it proves das recovered every byte of
  text, data, relocation **and the symbol table, order included**, losslessly.
  Corpus = the 175 `libc.a` objects (built from source via `compall`, our cc/as).
  Result: **175/175 FULL-FILE byte-identical, 0 reassemble-failures, idempotent.**
  The symbol table is the crux: `as` writes symbols in first-mention order and
  external relocations cite them by index, so `-a` output must reproduce the
  original mention order exactly.  das does this with an **index-order walk over
  in-memory segment bodies** (the generalization of the VAX das's
  `freezesymtab` — see `das/vax.md`, the VAX campaign's field guide): each
  segment's body is generated to memory, every symbol's first-mention position
  is scanned, and the walk visits the symtab in index order, inserting each
  declaration at the cursor or streaming one body through the symbol's first
  mention — gated by a viability guard (stream only if that interns no
  later-index symbol early; when text-streaming isn't viable the walk streams
  the `.data` body instead, which is how a `.data` switch table interns its
  handler labels in table order).  Getting here drove real das fixes:
  numbered-local synthetics, `.comm` commons, `.globl` pinning, operand
  symbolization (indexed/index-deferred/immediate/internal data pointers),
  nearest-label±offset anchors, `~name=` for absolute locals and for
  later-index label aliases (the compiler's goto-label idiom `~retoolon=L4`),
  the `b<inv> .+6; jmp *$Y` spelling of `as`'s own far-conditional expansion,
  and a corrected `sysinline[]` table (row 3 / Berkeley calls was wrong,
  mis-decoding every `sys local` indirect stub).

- `das-wide.sh [TREE]` — the **wide** das round-trip oracle: every archive
  member and loose `.o` under a 2.9 tree (default `~/bsd/2.9`) — **1130
  shipped, native-built objects**: kernel, FORTRAN runtime, curses, the overlay
  `libov*` libraries, contrib MH, the pascal FP interpreters.  Replay tiers per
  object: plain `as`; `as -V` when das's banner flags ovas (an REXT reloc citing
  a DEFINED symbol marks overlay assembly — all libov* members AND the kernel
  objects); plus the `ld -x -r` member post-processing the libI77/MH Makefiles
  do.  Result: **1130/1130 full-file byte-identical, 0 reassemble-failures.**
  This corpus surfaced: the `do_archive` ar_size endianness bug, ovas detection
  + local-anchor rules (TEXT globals only re-relocate external under `-V`),
  `sys n|0200` stack-args and pcrel-guarded inline-arg consumption (`sys
  fetchi` vs chroot), code assembled into `.data` (pcrel-reloc theorem, the FP
  trap stubs) with branch/sob targets resolved in the instruction's own
  segment, `.comm`/`.globl` interleave pinning, `name = op^tst` opcode symbols,
  external absolutes, f77's bare-`~` end-of-bss marker (even with bss==0), and
  neighbor+offset respelling of order-violating anchors (fpsim's `aexp+2`).

- `das-linked.sh` — das over the **linked binaries** in `~/bsd/2.9` (no
  relocation, so the tiers differ): the GENERIC **kernel** — all 57 shipped
  `.o`s das-round-trip byte-identical and the relinked `unix` (the Makefile's
  own `ld -X -n -Z ×7 -L` recipe) is **full-file byte-identical**, 30KB symtab
  included; **usr/70/rogue** (flat 0411, stripped) — whole-file `das -a | as`,
  text+data content byte-identical (in stripped files das emits all-numeric
  operands: exact by construction, immune to separate-I&D address overlap);
  **usr/games/rogue** (overlay 0430) — sliced per the `ovlhdr` into base +
  7 overlay + data windows (overlays share one load window), 9/9 windows
  content byte-identical.

Native 2.9 fixtures (`as as2 cpp c0 c1 c2` + `crt0.o`, nm/strip/ar targets, and
the runnable `ar`/`ranlib`/`sh`/`ld` for lib-sweep, ld-sweep, kernel-link and selfhost) come from
`../native/extract-rootdump.py ~/bsd/2.9/rootdump` (NOT committed --
copyrighted). apsim needs SHORT paths and `APSIM_ROOT` (so `as` execs
`/lib/as2`).

### The compiler's 64KB limit is authentic and essentially never bites

2.9's `cmd/c/Makefile` links c0/c1 with `cc -O -n -s` -- `-n` = **0410,
non-separate I&D, one 64KB space** (deliberately: the compiler had to run on
PDP-11/34,/40,/23 with no split-I&D hardware). c2 uses `-i` = 0411 (separate).
Our fixtures are the *shipped* binaries (0410 c0/c1) -- authentic, not our build.
apsim gives 0410 one 64KB space and 0411 two, matching the hardware.

A full **native-compiler census over cmd** (run native c0+c1 on every file our cc
compiles) found: **784 OK, 0 source-level failures, 0 c1 failures, 0 genuine
64KB-space failures.** The passes stream per-function (c1 reads c0's intermediate
from a temp file one function at a time -- gram.c's 44KB intermediate compiles in
a 64KB c1 fine), so *file size* is never the limit; only a single pathologically
huge function would be, and 2.9 has none. Every real 2.9 source file compiles on
the native compiler -- unsurprising, since it was built by that compiler.

The only apparent failures (16 eqn/neqn files) were the **long-path artifact**,
NOT space: the generated-`e.def` staging dir name was ~95 chars, overflowing
native c0's ~100-char filename buffer -> bogus "Symbol table overflow" (the error
even printed a corrupted path). Staging `e.def` at a short path compiles all 16
on native c0+c1. `yflags` now uses a short hashed staging dir so this can't recur.
Our own ported c0/c1/c2 run on the LP64 host and have no such limit at all.

## Coverage — our `as` vs native 2.9 `as`, **0 differences everywhere**

| tree | files byte-identical | harness |
|------|---------------------|---------|
| `cmd` | 766 | tree-sweep |
| `ucb` | 429 | tree-sweep |
| kernel (`sys`, `-DKERNEL -I<GENERIC>`) | 62 | cc-corpus |
| `lib/c` hand-written `.s` | 113 | as-corpus |
| `lib/c/gen` `.c` | 44 (+CC 29) | cc-corpus |
| `games` | 8 | cc-corpus |

**~1400 files across the tree assemble byte-for-byte identically to the native
2.9 assembler, with zero differences.** (`lib/c` `.s` has one non-match,
`sys/lfstat.s`, and that is a sys.s version skew — `lfstat`→`qfstat` rename — not
an assembler difference.)

### The residual compile tail is *parity*, not a tool defect

The files tree-sweep still can't compile (cmd ~40, ucb ~20) all need **build
artifacts we don't have standalone** — grammar-generated `.c` (not just the token
header), string-valued `-D` macros (Mail `MASTER`, berknet `LOCAL`), generated
width/opcode tables (troff, pascal), or config structs — or are malformed without
them. Where a file *is* malformed standalone, **our compiler fails identically to
the native one**: on `refer5.c` (undefined `KEYLET`) our `c0` and native `c0`
emit the same diagnostic, produce a **byte-identical `t1` intermediate**, and
return the **same exit code (1)**, so both cc drivers skip `c1`. No divergence
survives anywhere in the tail — our `cpp/c0/c1/c2/as` track the native tools
exactly, including their error behavior.

## Setup (2.8BSD compiler-pass oracle, run.sh)

The native binaries are not committed (copyrighted). Regenerate them from a
2.8BSD source tree, then run:

```sh
sh ../native/setup.sh [path-to-2.8BSD]   # default ~/bsd/2.8 -> sim/native/{c0,c1,c2}
cc -O2 -o ../apsim ../apsim.c -lm        # build the simulator
sh run.sh                                # run the corpus
```

## What it checks

For each `corpus/*.c` (kept to one tiny function each — native c1 truncates
larger inputs under apsim's flat 64 KB):

- **c2**: feed the same c1 output to our c2 and the native c2; compare byte-exact.
- **c1**: assemble our c0+c1 output and native c0+c1 output with `as`, compare the
  `.o`. (The c1 text streams differ only cosmetically — NULs, spaces,
  signed-vs-unsigned `%o` on immediates — so we compare assembled objects.)

A `c1=skip` line means native c1 couldn't run that input under apsim; it is not a
failure. Found and fixed a real c1 long-constant codegen bug (`c1/c11.c`).
