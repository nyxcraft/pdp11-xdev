# pdp11-xdev — design

How the merged toolchain is put together: the universe mechanism, the
pipeline and object model, the fidelity contract, and the verification
tiers.  For how to *use* it, see the [user guide](user-guide.md).

---

## 1. Two kinds of parameter: universe and object format

The toolchain answers "which UNIX" twice, deliberately separately.

**The universe** (`--universe=NAME`, `-u NAME`, or the exported
`PDP11_UNIVERSE`; default `bsd29`) is the *target runtime era*.  One
**universal** C library serves every era: a single `libc.a` + `crt0.o`
(and the `-p`/`-f` profiling and no-FPU startup variants), curses, and
termlib live FLAT in `lib/`, and one header set lives FLAT in `include/`.
The era is chosen at **link time**, not build time:

- `cc` validates the name against the generated table, normalizes aliases
  (`2.9`→`bsd29`, `1bsd`→`bsd1`, …) and exports the canonical
  `PDP11_UNIVERSE` to every pass;
- `cpp` resolves its default system include directory to `include/` next
  to the `bin/` it runs from;
- `ld` maps the universe name to its numeric era id (`universe.h`) and
  stamps it as the absolute symbol **`__univ`** in the executable;
- `crt0` copies `__univ` into a data cell, and the handful of libc
  routines whose ABI moved between releases branch on it — a run-time
  `if` on a link-time constant.  (On the PDP-11 the shipped pair 2.8/2.9
  share every syscall number, so the machinery mostly matters for the
  4BSD stack-arg convention of 2.10/2.11, whose syscall stubs are
  dual-headed.)
- `apsim` maps the name to a kernel *personality* (`enum pdp11_kern`,
  ordered by era) and the argument convention.

The names live in one file, `src/common/universes.tsv`, from which
`mkuniverse.py` generates the committed `universe.h` (an X-macro table;
`make -C src/common check` fails if it goes stale).  A universe is either
`full` — a compile target: the universal libc + headers serve it and the
suites pass — or `sim` — apsim runs that era's real vintage binaries but
it is not a compile target.  Every registered universe is one or the
other; there is no unfinished tier.  The `full` set spans First Edition
(1971) through 2.11BSD plus System III, SVR2, and Ultrix-11 1.0–3.1, and
the one libc serves all three of their syscall-convention families
(V1 inline-arg traps, the V7 inline/indirect family, and the 4BSD
stack-arg convention).

**The object format** is a property of the *assembler's output*,
orthogonal to the universe: `as --isa/--sys/--aout/--std` select
instruction set, syscall keyword table, and a.out flavor from First
Edition 12-byte headers with bit-stream relocation through 2.11BSD
string-table symbols.  The compiler pipeline uses the common 2BSD format
throughout; 2.8 and 2.9 share `struct exec` and all six magics, so no
conversion step exists — "era-native executable" falls out of linking
against the universal library and stamping `__univ`.

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
porting deltas, and for the **tools** the proof standard is *byte identity
with what the original tools produced*, not plausibility — established by
the oracle suites (§4.4):

- `as`/`ld` produce byte-identical objects and executables: the oracle
  links the whole 2.9 GENERIC kernel to a byte-identical 0430 overlay
  `unix`, and rebuilds native `as`/`as2`/`strip` byte-for-byte
  (self-hosting);
- `ar`/`ranlib` reproduce the 2BSD archive layout to the byte (39/39,
  26/26 vs the native tools run under apsim);
- `das -a` is a verified inverse of `as` across large object corpora.

The **C library** is one universal `libc.a` built from the 2.9 sources (a
functional superset of 2.8 — identical syscall numbers, superset
errno/signal, one `struct stat`).  Byte-for-byte reproduction of each
era's shipped `libc.a` was a goal of the *earlier* per-universe build; the
merged tree deliberately trades it for a single library, so archive member
order there is free.  Era differences that remain observable are handled
at run time (the `__univ` dispatch) rather than by shipping a different
library per era.

Known era bugs are preserved, not fixed, when the era is the target (the
recorded 2.8 cpp `defined()` quirk lives in `tests/cpp-bsd28/` as a
golden, deliberately unwired).  Where host reality forces a change (LP64
widths, register-ABI varargs, read-only string literals, `$TMPDIR`), the
deltas are localized and each tool's `docs/porting.md` records them.

## 4. Verification tiers

1. **Per-tool suites** (`src/pdp11-<tool>/tests/run.sh`).  The largest is
   apsim's: the CIS/late-hardware golden probe, errno fidelity, the
   universe numbering gate (positive *and* negative), FP11 `$literal`/FT
   probes, real era binaries (V5/V6 `ls`, 2.11 `echo`/`ls`/`date`/csh
   job control, sockets), and the cooperative ptrace channel driving the
   genuine 2.11 `adb`.
2. **The end-to-end suite** (`tests/run.sh`, run by `make check`) — cpp
   goldens, per-pass shell tests, and the cc correctness programs
   compiled, linked, and *executed under apsim*; then re-run with `-O`;
   then re-run in the `bsd28` universe.  "Run it, don't inspect it."
3. **The cross-universe matrix** (`oracle/cross-universe.sh`, also in
   `make check`) — every tool exercised across every universe it can
   reach: the compiler pipeline + binutils in the compile targets, and
   `apsim`/`das` against each era's native binaries (285 checks).
4. **The oracles** (`oracle/`, opt-in) — the original PDP-11 binaries run
   under apsim as ground truth: the 1981 compiler corpus, whole-tree `as`
   sweeps, `ar`/`ranlib` byte-matching, `ld` up to the byte-identical
   GENERIC kernel, disassembler round-trips, and self-hosting.  Fixtures
   are copyrighted vintage binaries, regenerated locally, never committed.
5. **Memory safety** (`make check-san`) — apsim rebuilt under
   AddressSanitizer + UBSan, running the suite and an 800-case
   deterministic loader-fuzz corpus over the real a.out loader, then
   rebuilt clean.  Leak detection is off by design (the guest space is
   never freed); the value is corruption detection on hostile input.
6. **Syscall coverage** (`docs/syscalls/coverage.md`) — `sweep.py` runs
   every command binary in each distribution tree under apsim and counts
   the ones with no unimplemented call; the number is a floor and the
   misses are classified.  GNU binutils (`~/pdp11-tools`) is kept outside
   the tree as an independent a.out-structure reader.

## 5. The simulator

`apsim` is a user-mode runtime, not a machine emulator.  It loads 2BSD
a.out (0407/0410/0411, 0430/0431 auto-overlays) and First Edition (0405)
images, executes the user-mode instruction set (base + EIS + FIS + full
CIS + the J-11 group), and turns `sys` traps into host syscalls inside an
`APSIM_ROOT` path sandbox.  What makes it multi-universe:

- **Per-era personalities.**  One canonical syscall switch (V7 numbering)
  plus a per-era *remap table* (`Bsd210`/`Bsd211`/`Sys3`/`Ultrix`) that
  moves an era's guest numbers onto the canonical handlers; private
  synthetic ids cover calls with no V7 ancestor.  The personality also
  selects the argument convention (inline/indirect vs 4BSD stack args)
  and the per-era `struct stat` and directory-record shapes (V1 34-byte,
  V7 32-byte, 4.x 52-byte; 16-byte vs variable-length directories).
- **A real host→classic errno map**, so a failing call delivers the era's
  errno (carry set, number in r0), never a raw host number.
- **Real process model:** fork/wait/pipe/exec are host primitives (the
  flat 64 KB spaces are duplicated by host `fork` for free), plus 4.3
  **job control** (setpgrp/tcsetpgrp, `wait3(WUNTRACED)`, real `SIGSTOP`)
  and **Berkeley sockets** on host sockets — enough to run the genuine
  2.10/2.11 csh and the Ultrix network clients.
- **A cooperative ptrace channel** (`$APSIM_PTRACE`): a traced guest parks
  inside apsim serving an AF_UNIX socket, so one apsim can read/write and
  single-step another across their separate spaces — this runs real 2.11
  `adb`.
- **Bit-faithful FP11** — a 56-bit D-format engine over 128-bit integers,
  no host doubles, honoring the FT (chop) mode — because printf's `%f`
  last digit is part of fidelity.
- **Robustness:** all diagnostics go to a private dup of stderr (a guest
  that closes fd 2 cannot mute the simulator), and the a.out loader masks
  its 16-bit header fields and clamps every segment read into the address
  space (hardened by the fuzzer).
- **Determinism knobs** (`APSIM_PID`, `APSIM_TIME`, fixed uid/gid) so two
  builds can be byte-compared.

Known gaps are tracked in NOTES.md §Known gaps; they reduce to a handful
of privileged/kernel-internal calls apsim refuses on purpose.

## 6. Growing the universe set

Every registered universe is already `full` or `sim`.  Adding a new one is
a universe row in `universes.tsv` plus, if it renumbers the syscalls, a
remap table and the per-era struct shapes in apsim, then a row in the
cross-universe matrix (and a coverage sweep if native binaries exist).
Per the project's porting policy, **2.11BSD is the source base for any
newly ported tool** (`~/bsd/2.11`, pl 431): its binutils/ld/libc line is
portable C, and the assembler's `--aout=v2+` axis already writes its
string-table object format.
