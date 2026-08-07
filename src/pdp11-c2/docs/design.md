# pdp11-c2 — design

This document explains what `pdp11-c2` is, how it reads c1's assembly into an
instruction list, the peephole transformations it iterates over that list, the
register tracking that keeps them honest, and the LP64/host porting fixes it
carries. For how it is *invoked* (it is the `cc -O` pass, not a command you run
by hand), see the [user guide](user-guide.md).

The sources are `c20.c` (the driver: read the assembly in, iterate, write it
out — plus the opcode table and hashing), `c21.c` (the improver proper — the
transformations and the register tracking), and `c2.h` (the node and opcode-table
declarations). It is newer code than `c0`/`c1`, so the source changes are small.

---

## 1. What it is

`pdp11-c2` is Ritchie's **`c2`**, the C object-code improver — the compiler's
optional peephole optimiser. `cc` runs it as the `-O` pass, between the code
generator and the assembler:

```
cpp → c0 → c1 → c2 → as → ld        (only when -O is given)
```

It reads the assembly `c1` produced and writes tighter assembly. It is **not** a
code generator: it only rewrites what is already there — shortening branches,
threading jumps, deleting redundant instructions, tracking registers — so the
one thing that must hold is that the code still **computes the same answer**. The
2.8 and 2.9 trees shipped the same c2; this is that one program.

---

## 2. Reading assembly into a list

`input()` (c20.c) reads the assembler text line by line (`getlin`) and builds a
doubly-linked list of `struct node`, each carrying an opcode (`op`), a sub-opcode
(`subop` — the condition-branch flavour, or the `BYTE` marker for a `.b`
instruction), a numeric label (`labno`), and the operand text (`code`).
`oplook()` classifies each mnemonic through `optab[]`, a table hashed into
`ophash[OPHS]` by the first three characters (`opsetup` builds the open-addressed
hash once at start-up); a `Lnnn`-form line is a numeric-label switch entry
(`JSW`). `output()` walks the finished list and prints it back, reattaching the
`b` byte suffix and turning label nodes back into `L%d:`.

---

## 3. The iteration driver

`main` runs a fixpoint. Per input segment:

```
input → movedat →
  { refcount →
      { iterate → clearreg } while nchange →
    comjump → rmove }
  while (nchange || jumpsw)
  → addsob → output
```

`refcount()` rebuilds label reference counts (through a `labhash`) so a label
that falls to zero references can be dropped (`decref`); `iterate()` runs the
jump/branch transforms; `rmove()` runs the register-and-redundant-instruction
transforms; `comjump()` factors common code before shared jumps; `jumpsw()`
reverses branches to shorten spans; `addsob()` forms loop instructions. Each pass
counts its rewrites in `nchange`, and the loops re-run until nothing changes.

---

## 4. Jump and branch transforms

`iterate()` (c20.c) and its helpers do the control-flow tidying:

- **jump-to-jump threading** — a branch whose target is itself a `jbr` is
  repointed at the final target;
- **skip-over-jump** — a conditional branch over an unconditional one is inverted
  (`revbr`) so the fall-through is taken;
- **dead code after a jump** — instructions between an unconditional jump and the
  next label are unlinked, and a jump to the very next label is deleted;
- **cross-jumping** (`xjump`) — identical instructions ahead of two paths that
  merge are hoisted behind a shared label (`insertl`, `equop`);
- **code motion and loop inversion** (`codemove`) — a jump into a loop is turned
  into the fall-into-body / test-at-bottom shape when it is shorter;
- **common tails before a shared jump** (`comjump`/`backjmp`).

`redunbr()` and the `CBR` handling in `rmove` also delete a `tst`/`cmp` against a
constant plus a conditional branch that can be decided at compile time — with the
7th-edition addendum correction that keeps the test when a following instruction
still needs the condition codes.

---

## 5. Register and redundant-instruction tracking

The transformations in `c21.c`'s `rmove()` are only safe because c2 tracks what
each register and location holds. `regs[12][20]`, `conloc`/`conval`, and `ccloc`
model the register contents, the last stored constant, and which location last
set the condition codes; `savereg`/`setcon`/`setcc` record, `dest`/`source`
invalidate on write and on auto-increment/decrement side effects, and
`clearreg()` wipes the model at any label or unknown control transfer. With that
model, `rmove` deletes a `mov` that reloads a value already in place
(`findrand`), rewrites a memory operand to a register that already holds it
(`repladdr`, `nsaddr`), turns `mov $0` into `clr` and `bic $-1` / `bit $0` into
their simpler forms, and drops a `tst` whose operand already set the codes
(`nrtst`). `check()` is the list-integrity assertion used while debugging.

---

## 6. Branch reversal and the `sob` loop instruction

`jumpsw()` (c21.c) reverses a conditional branch and its following `jbr` when
that brings both targets nearer (via `revbr`, the reversal table in c20.c),
shrinking span-dependent branches. `addsob()` turns a `dec reg` + `jne` into the
PDP-11's single `sob` (subtract-one-and-branch) — **guarded by `toofar()`**,
which sums `ilen()` over the branch span and declines the transform when the
target is 128 bytes or more away, because `sob` carries only a short backward
displacement. This guard is unconditional here (c2 has no command-line knob for
it); `as` then resolves the remaining span-dependent branches.

---

## 7. Data motion

`movedat()` (c21.c) gathers the `.data` fragments c1 scattered between `.text`
runs into one place and collapses redundant segment-directive switches, so the
assembler sees fewer `.text`/`.data` flips. It must initialise its local
`struct node data` (`data.forw = 0`) before the no-DATA-segment path reads it —
a real bug the port fixed (§8).

---

## 8. LP64 and host porting fixes

c2 is newer than c0/c1, so the changes are few but each was load-bearing:

- **`copy()` varargs.** The 2BSD `copy()` read its optional second string with
  `(&ap)[1]`, walking the stack past the first parameter — valid on the PDP-11's
  contiguous-arg stack, garbage on the x86-64 register ABI. Rewritten with
  `<stdarg.h>`.
- **The node arena.** `alloc()` grew its arena with raw `sbrk`, which corrupts
  the host libc heap c2's own stdio uses; it now grabs `malloc` chunks (nodes
  reference each other by absolute address, so chunks need not be contiguous).
- **Embedded NULs.** `c1` runs on the host, where glibc's `printf("%c",0)` writes
  a NUL byte the PDP-11's `_doprnt` emitted as nothing; `getlin()` drops embedded
  NULs so the opcode/operand split is not truncated, exactly as `as` already did.
- **`dualop` NULL guard.** A jump to a numeric label leaves `code == 0`; the
  `sob` length walk dereferenced it. The PDP-11 read address 0 as an empty
  operand; `*0` faults on the host, so `dualop` guards the deref, matching the
  output path's own `if (t->code)`.
- **`movedat` init** (§7), and a local **`iabs()`** renamed from `abs()` —
  defining the reserved `<stdlib.h>` name `abs` ourselves is undefined behaviour.

---

## Testing

`tests/cc/optimizer.sh` compiles each program twice — plain `cc` and `cc -O` —
runs both under `pdp11-apsim`, and requires the `-O` build to give the **same
answer** (an optimiser that deleted the program would fail where a "did it get
smaller" check would pass) while its text segment is no larger. It exercises
loops, recursion, nested calls, arrays, and the buffered-`printf` path — the
cases that flushed out the varargs `copy()`, the arena, and the NUL bugs — and
invokes c2 directly on a recursive function (whose call-return `mov%c` NUL once
crashed it), because `cc` silently falls back to the unoptimised c1 output when
c2 fails.

The strong check is the oracle. Building libc with `-O` reproduces native 2.x
libc members **byte-for-byte** (the `oracle/cc-corpus.sh` CC check assembles our
`c0/c1/c2` output and the native passes' output and byte-compares the `.o`), so
c2's optimisation decisions match the authentic tool's; the whole
`cpp→c0→c1→c2` chain is byte-identical to native 2.9 across every file the
oracle can run (91/91 CC).

---

## For a maintainer

- **It only rewrites; correctness is "same answer", proven by running it.** The
  optimiser test executes the `-O` build and compares its result, not its size.
- **The `sob` distance guard (`toofar`) stays.** `sob` has a short backward
  reach; forming one to a far target would be a "Branch too far" from `as`. It is
  unconditional here — there is no relax flag.
- **Register tracking is what makes the deletions safe.** A new transform that
  moves or deletes an instruction must respect (and update) `regs`/`conloc`/
  `ccloc` through `savereg`/`setcon`/`setcc`/`dest`/`source`; skipping that is how
  you clobber a live value.
- **c2 reads no universe and has no era knobs.** Its output is PDP-11 assembly in,
  PDP-11 assembly out, using only base instructions; the target era is `as`'s and
  `ld`'s downstream. The only positional/prefix arguments are the two files and
  the `+`/`-` debug/stats prefixes.
