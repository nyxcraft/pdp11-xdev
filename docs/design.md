# pdp11-xdev — design

How the merged toolchain is put together: the universe mechanism, the
pipeline and object model, the fidelity contract, and the verification
tiers.  For how to *use* it, see the [user guide](user-guide.md); for what
was taken from where, see [../NOTES.md](../NOTES.md).

---

## 1. Two kinds of parameter: universe and object format

The toolchain answers "which UNIX" twice, deliberately separately.

**The universe** (`--universe=NAME`, `-u NAME`, or the exported
`PDP11_UNIVERSE`; default `bsd29`) is the *target runtime era*.  It decides
what the compiled program is built against and how it runs:

- `cc` validates the name against the generated table and exports it to
  every pass, then links `lib/<universe>/crt0.o` (or the `-p`/`-f` profiling
  and floating-point variants of that era's csu);
- `cpp` resolves its default system include directory to
  `include/<universe>/` next to the `bin/` it runs from;
- `ld` resolves `-lc`, `-lcurses`, … in `lib/<universe>/`;
- `apsim` supplies the matching kernel personality (today: the 2.8BSD
  syscall layer at V7-trap parity, plus the auto-selected First Edition
  personality for 0405 binaries).

The names live in one file, `src/common/universes.tsv`, from which
`mkuniverse.py` generates the committed `universe.h` (an X-macro table;
`make -C src/common check` fails if it goes stale).  A universe is `full`
(headers + libraries + tests), `sim` (apsim personality only), or `planned`.

**The object format** is a property of the *assembler's output*, orthogonal
to the universe: `as --isa/--sys/--aout/--std` select instruction set,
syscall keyword table, and a.out flavor from First Edition 12-byte headers
with bit-stream relocation through 2.11BSD string-table symbols.  The
compiler pipeline uses the common 2BSD format throughout; 2.8 and 2.9
share `struct exec` and all six magics, so no conversion step exists —
"era-native executable" falls out of linking against the era's libraries.

## 2. The pipeline

    source.c ──cpp──▶ ──c0──▶ ──c1──▶ [c2] ──as──▶ file.o ──ld──▶ a.out
                      └────────── Ritchie cc ─────────┘

`cc` is the driver.  It resolves its sibling passes from its own location —
everything up to the last `-` in its basename is the family prefix, so
`bin/pdp11-cc` runs `bin/pdp11-cpp`, `bin/pdp11-c0`, … — which makes the
installed tree relocatable and allows renamed copies to drive matching
pass sets.  `-O` inserts the c2 peephole optimizer, exactly as the 2BSD
`compall` builds did; `-V` switches cc, as (`ovas` mode), and ld into the
MENLO overlay scheme.

c1's code generator table is authored in the original `table.s` template
language; the build runs the authentic `cvopt` expander and then `mktab`
(a host helper) to turn the result into compilable C instead of PDP-11
assembly — the one structural deviation the host port needs.

## 3. Fidelity is the contract

The sources are the authentic 2.8/2.9BSD programs with the minimal LP64
porting deltas, and the proof standard is *byte identity with what the
original tools produced*, not plausibility:

- archive member order in the libc recipes is the 2BSD `mklib` order —
  `ld` lays out pulled members in archive order, so the order is
  load-bearing for reproducing native binaries;
- curses/termlib for bsd29 are archived in the *old* 2BSD ar format
  (0xff65 magic, middle-endian headers) by dedicated writers, because
  that is what the shipped libraries used;
- bsd29 additionally builds `libc-era.a`/`crt0-era.o` against the
  reconstructed Feb–Mar 1983 `sys-era.s`, reproducing the *shipped*
  binaries' library rather than the final source tree's;
- known era bugs are preserved, not fixed, when the era is the target
  (the recorded 2.8 cpp `defined()` quirk lives in `tests/cpp-bsd28/` as
  a golden, deliberately unwired).

Where host reality forces a change (LP64 widths, register-ABI varargs,
read-only string literals, `$TMPDIR`), the deltas are localized and each
tool's `docs/porting.md` records them.

## 4. Verification tiers

1. **Per-tool suites** (`src/pdp11-<tool>/tests/run.sh`) — currently the
   apsim CIS/late-hardware golden probe; more land with each tool.
2. **The end-to-end suite** (`tests/run.sh`, run by `make check`) — cpp
   goldens, per-pass shell tests, and the cc correctness programs
   compiled, linked, and *executed under apsim*; then re-run with `-O`;
   then re-run in the second universe (`bsd28`).  "Run it, don't inspect
   it."
3. **The oracles** (`oracle/`, opt-in) — the original PDP-11 binaries run
   under apsim as ground truth: the 1981 compiler corpus, whole-tree `as`
   sweeps, `ar`/`ranlib` archive byte-matching, `ld` sweeps up to the
   byte-identical 0430 GENERIC kernel, disassembler round-trips, and
   self-hosting (our toolchain rebuilding native `as`/`as2`/`strip` to
   the byte).  Fixtures are copyrighted vintage binaries, regenerated
   locally from `~/bsd` trees, never committed.
4. **GNU binutils** (`~/pdp11-tools`, pdp11-aout target) is kept *outside*
   the tree as an independent reader for a.out structure — an oracle on
   the object side only (its `as` speaks DEC syntax, not 2BSD).

## 5. The simulator

`apsim` is a user-mode runtime, not a machine emulator: it loads 2BSD
a.out (0407/0410/0411, 0430 auto-overlays) and First Edition (0405)
images, executes the user-mode instruction set (base + EIS + FIS + full
CIS + the J-11 group), and turns `sys` traps into host syscalls inside an
`APSIM_ROOT` path sandbox.  Floating point is a bit-faithful 56-bit
FP11 D-format engine over 128-bit integers — no host doubles on the FP11
path — because printf's `%f` last-digit behavior is part of fidelity.
fork/wait/pipe/exec are real host primitives (the emulator's flat 64 KB
spaces are duplicated by host `fork` for free).  Determinism knobs
(`APSIM_PID`, `APSIM_TIME`, fixed uid/gid) exist so two builds can be
byte-compared.  Known gaps are tracked in NOTES.md §Known gaps.

## 6. Growing the universe set

Promoting a `planned` universe to `full` = era headers and libc recipe
under `src/pdp11-libc/<u>/` + an apsim personality audit + a universe row
in the end-to-end suite.  Per the project's porting policy, **2.11BSD is
the source base for any newly ported tool** (`~/bsd/2.11`, pl 431): its
binutils/ld/libc line is portable C, and the assembler's `--aout=v2+`
axis already writes its string-table object format.  The research eras
(V5/V6/V7, sys3) stage from `~/unix`.
