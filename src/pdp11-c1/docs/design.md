# pdp11-c1 — design

This document explains what `pdp11-c1` is, how it turns c0's intermediate form
into PDP-11 assembly, the code table that drives it and how that table is built
on the host, the soft floating-point path, and the LP64 correctness fixes the
port carries. For how to *invoke* it (it is a compiler pass, not a command you
run by hand), see the [user guide](user-guide.md).

The sources are `c10.c` (the driver — `match`/`rcexpr`/`cexpr`, the code-table
interpreter), `c11.c` (naming, switches, comparisons, and the intermediate-file
reader `getree`), `c12.c` (`optim`/`unoptim`/`acommute` — the expression-tree
rewriter that runs before codegen), `c13.c` (the operator-dope and
instruction-mnemonic tables), and `c1.h` (the node superset and operator codes).
The code table is authored in `table.s` and turned into C by two build-time
programs, `cvopt.c` and `mktab.c` (§4); FP constants go through `softfp.c` (§5).

---

## 1. What it is and where it runs

`pdp11-c1` is the **second pass of Ritchie's PDP-11 C compiler** — the code
generator (`c0` is the first pass, the parser; the file header still reads "C
compiler, part 2"). `cc` runs it as the `c1` stage of

```
cpp → c0 → c1 [→ c2 with -O] → as → ld
```

reading the two temp files `c0` produced and writing assembler text. `c10.c`'s
`main` opens `argv[1]` (the intermediate tree stream) as stdin and `argv[3]` as
stdout, calls `getree()` to generate code, then — unless `argv[2]` is `"-"` —
reopens `argv[2]` (the string/initialised-data file) and runs `getree()` again,
tacking its output on in `.data`. It runs entirely on the LP64 host.

---

## 2. Reading the intermediate form

`getree()` (c11.c) reads a byte stream of 16-bit words via `geti()`, which packs
two bytes little-endian and **sign-extends to 16 bits** (a PDP-11 `int`). Each
operator word is tagged `0177000` in its high byte; the low byte is the operator
(the `#define`s in `c1.h`). Leaf operators (`NAME`, `CON`, `LCON`, `FCON`, …)
push a freshly built `tnode` on a small expression stack; a unary or binary
operator pops its operands and pushes `tnode(op, type, …)`. Whole statements
arrive as `EXPR`/`CBRANCH`/`SWIT`/`INIT`/… records that hand the finished tree to
`optim` and then to `rcexpr`.

Nodes are allocated from a per-function arena (`getblk`): `curbase` is reset to
`funcbase` at each statement, so one function's tree is all that need fit. On the
PDP-11 this arena grew by `sbrk`; on the host that would corrupt the libc heap
c1's own stdio uses, so `main` allocates one 16 MB `malloc` region up front and
`getblk` sub-allocates from it (§7).

---

## 3. Code generation by table

The heart of c1 is `match`/`rcexpr`/`cexpr` in c10.c, driven by four **code
tables** (c13.c-adjacent, built in §4):

- **`regtab`** — produce the value in a register (this table must succeed, or
  it is a "No code table for op" error);
- **`cctab`** — only the condition codes are wanted (a test before a branch);
- **`efftab`** — the value is discarded, evaluate for side effect;
- **`sptab`** — push the value on the stack (a function argument).

`match()` walks the table for the operator, then scans that operator's `optab`
rows for one whose operand *degrees* (register pressure, from `dcalc`) and
*types* (`notcompat`) fit; a row's `tabstring` is a template. `cexpr()`
interprets the template: control bytes select an operand address (`A`/`B` →
`pname`), a register (`I`/`J`), a subtree to recurse into with a named table
(`G`/`K`/`H` → `rcexpr`), or an instruction mnemonic (`M` → `prins`), and the
`0200` bit asks for a tab. `rcexpr()` wraps `cexpr` with the fallback logic
(cctab → regtab + `tst`, sptab → regtab + push, …) and the register bookkeeping
that keeps long values (two registers) and odd/even register pairing correct.

Two families of tree rewrites feed the tables: `reorder`/`sreorder` turn
`reg = x + y` into `reg = x; reg += y`, and `delay`/`sdelay` pull a postfix
`++`/`--` out to run after the expression. Both got NULL guards for the port (§7).

---

## 4. The code table: authored, expanded, converted, compiled

The tables are not hand-written C. They are **authored in `table.s`**, a compact
template language (`%a,n` / `jmp A1` etc.), the way the 1981 compiler wrote them.
On the PDP-11 `table.s` was expanded to assembler by `cvopt`, assembled, and
linked in as data the code generator interprets. The host cannot assemble
PDP-11, so a fourth stage turns the assembler into C:

```
table.s ──cvopt──▶ table.i ──mktab──▶ table.c ──cc──▶ table.o
        (templates → asm)   (asm → C)
```

- **`cvopt.c`** is the authentic 2BSD table compiler: it maps the template
  letters to the `cexpr` control bytes, emits each template as a labelled
  `<...>;.byte N;<...>` fragment string, and emits the degree/type match bytes
  (with `.byte %o`, i.e. **octal**) and the index/optab arrays. Its only source
  change was the `=op` → `op=` operator spelling so it compiles on the host.
- **`mktab.c`** is a new host helper that parses `cvopt`'s output and writes
  `table.c`: each template becomes `static char Ln[] = "…"` (a `.byte N` →
  `\NNN`), each optab label becomes `static struct optab L[] = {{d,t,d,t,str},…,
  {0,…}}`, and each `_NAME=.` index becomes `struct table NAME[] = {{op,optab},…}`.
  It parses those match bytes back **in octal** — reading them as decimal was the
  bug that made no arithmetic operator match any row ("No code table for op: +").
  Templates are emitted first, then aliases, then optabs, then index tables, so
  every name is defined before use.

`c13.c` supplies the fixed tables the templates lean on: `opdope[]` (per-operator
flag bits — LEAF, BINARY, RELAT, COMMUTE, …), `opntab[]` (names for diagnostics),
and the mnemonic tables `instab[]`/`branchtab[]` that `prins` indexes (`mov`,
`add`, `jeq`/`jne`, the 200+ pointer-test forms, …).

---

## 5. Floating point — the DEC path, not the host's

PDP-11 floating point is DEC F/D format, not IEEE-754, and the port computes it
that way. `softfp.c` is a soft D-format (56-bit) implementation whose add / mul /
div / normalise sequence mirrors 2.8 libc `atof.c` statement for statement, so
every intermediate rounding lands where the 1981 machine's did (FP11 rounds by
adding one at the first discarded bit — no sticky, no round-to-even). At read
time `getree` calls `softfp_atof` and stores the exact four D-format words on the
`FCON` node; `fcwords()` returns those. `decfloat()` — a direct host `double` →
D-format conversion — is only the defensive fallback when the stored words are
all zero (in practice, `0.0`). `softfp_dtof` narrows to F-format for `float`
initialisers, `softfp_fromlong` handles the `long`→float folds, and FP negation
flips the sign bit of the DEC word directly (c12.c), not a bit of the host double.

---

## 6. Switches, comparisons, and longs

`c11.c` holds the non-table codegen. `pswitch` chooses a **direct** jump table
(dense ranges), a **simple** compare chain (< 10 cases), or a **hash** table
(case values hashed with `% (unsigned short)`; `sort` orders and dup-checks
them). `cbranch`/`branch` emit conditional branches, mapping a relation to its
`jeq`/`jlt`/… and its inverse (the `200+` variants are pointer tests against
zero). `longrel`/`xlongrel` drive the two-word comparison off `lrtab`, emitting
only the tests a given relation needs.

---

## 7. LP64 correctness fixes

c1 is pre-1977 K&R C manipulating 16-bit quantities on a 64-bit host; the port's
fixes cluster in a few classes:

- **Pointer truncation.** Every node-returning function has an explicit
  `struct tnode *` return type and a visible prototype (`c1.h`), or its 64-bit
  result truncates to `int`; K&R-implicit pointer *parameters* were typed too.
- **16-bit integer semantics.** `geti()` sign-extends, so a negative `switch`
  case or `-1` constant reads correctly; `CON` sign-extends a signed `int`; the
  `LCON` int-fold masks both halves with `& 0177777` before testing (so `-1L`
  and a `long` whose low word is ≥ `0100000` fold right); `ANAME` prints a
  negative auto offset as 16-bit octal to match the native listing; the switch
  hash and modulo use `(unsigned short)`.
- **The `(signed char)` ITOC fold.** A `(char)` constant cast is folded with
  `(signed char)`; the original `value<<8>>8` sign-extended from bit 23 on the
  host, so bit-7-set constants (`(char)0300`) came out wrong.
- **Sequencing, NULL, and the arena.** The `FSEL` case sequences its stack pop
  against the input read (was unsequenced UB); `reorder`/`sreorder`/`xdcalc`/
  `sdelay` guard the NULL / reduced-to-leaf subtree the PDP-11 read harmlessly
  from low memory; `getblk` allocates at least `sizeof(struct tnode)` (the
  variant structs are smaller than the union superset) and zeroes each block,
  because the per-function arena is reused rather than served fresh from `sbrk`.
- **The table octal bug** (§4) and **DEC vs IEEE float** (§5).

---

## Testing

`tests/c1/codegen.sh` drives `cpp → c0 → c1` and checks the emitted assembly:
`return 42` → `mov $52,r0` (52 octal), `2 + 3*4` folds to `mov $16,r0`, and a
global `int x = 7` lands in `.data` as `_x:` with `mov _x,r0`, all with the
`jsr r5,csv` / `jmp cret` prologue and epilogue. `tests/cc/programs.sh`
disassembles c1's output for locals, loops, arithmetic, arrays, K&R calls and
recursion with GNU `objdump -m pdp11`.

The strong check is the oracle: `oracle/run.sh` compiles the small corpus and
runs **our c1 against the 1981 native c1** under `pdp11-apsim`, and
`oracle/cc-corpus.sh` feeds the same preprocessed `.i` to our and native
`c0/c1/c2`, assembles both, and byte-compares the `.o`. Across the trees it can
run under apsim's ~64 KB the whole `cpp→c0→c1→c2` chain is **byte-identical to
the native 2.9 compiler** (the CC oracle matches on every file it can run,
91/91).

---

## For a maintainer

- **The table is generated; edit `table.s`, never `table.c`.** `table.c` is
  rebuilt by `cvopt` + `mktab` on every `make` and is marked "do not edit". A
  codegen change is a `table.s` change; a pipeline change is `cvopt`/`mktab`.
- **Match bytes are octal end to end.** `cvopt` emits them `%o` and `mktab`
  reads them `%o`; a decimal slip there silently unmatches whole operators.
- **`softfp.c` is authoritative for FP constants; `decfloat` is the zero
  fallback.** Do not "simplify" FP conversion back to the host double — the last
  mantissa bits must match the FP11.
- **Pointer-returning functions need a prototype**, in `c1.h` beside the others,
  or the LP64 truncation crash returns; and **`getblk` returns full-`tnode`
  blocks** — do not allocate a variant struct at its own size and read it as a
  node.
