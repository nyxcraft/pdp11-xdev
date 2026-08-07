# pdp11-c0 — design

This explains what `pdp11-c0` is, where it sits in the compiler, how a
translation unit becomes the intermediate stream `c1` reads, and the two things
the port had to get right: keeping this pre-1977 C compilable on a 64-bit host,
and keeping its output byte-for-byte what the 1981 compiler produced. For how to
*run* it, see the [user guide](user-guide.md); for the line-by-line porting
account see [porting.md](porting.md).

The sources are `c00.c` (driver, lexer, expression parser), `c01.c` (tree
building and conversions), `c02.c` (statements and externals), `c03.c`
(declarations), `c04.c` (tree walk and emission), `c05.c` (the tables), and
`c0.h` (the shared structs and `#define`s). It is the 2.9BSD `c0`, SCCS 2.x,
near byte-identical upstream to 2.8's.

---

## 1. What it is, and where it sits

`pdp11-c0` is **pass 1** of Dennis Ritchie's two-pass PDP-11 C compiler. The
full compile is

```
cpp  ->  c0  ->  c1  [ ->  c2 ]  ->  as  ->  ld
```

— the preprocessor, then this pass, then `c1` the code generator, then the
optional `c2` peephole optimiser (`cc -O`), then the assembler and linker.
`c0` reads **already-preprocessed** C and does the front-end work: lexing,
parsing declarations / statements / expressions, building the symbol table and
the expression trees, and writing an **intermediate token/operator stream** that
`c1` turns into PDP-11 assembly. It **does not emit assembly** — that is `c1`'s
job. `cc` drives the chain and calls `c0` with three temp-file arguments (see the
[user guide](user-guide.md)).

The 16-bit PDP-11 target is baked into `c0.h`: `int`, `short` and pointer are all
2 bytes (`SZINT`/`SZPTR`), `NBPW` = 16. Those object-machine constants are
unchanged by the port (§5).

---

## 2. The six files

- **`c00.c`** — `main`, the lexer (`symbol`, backed by the `ctab` character
  classes; `getnum`, `getcc`, `mapch`, `putstr`), the keyword hash, and `tree()`
  — the classic bottom-up, priority-driven expression parser, which shifts
  operands onto a stack and calls `build()` in `c01.c` to reduce.
- **`c01.c`** — `build()`: takes the top operands off the stack, inserts the
  required type conversions (driven by `cvtab`/`cvntab`), folds constant
  sub-expressions (`fold`), and makes a new node. Also the node allocators
  (`block`/`cblock`/`fblock`/`nblock`/`gblock`) and `error()`.
- **`c02.c`** — `extdef()` (one top-level external at a time) and `statement()`
  (the keyword-dispatched grammar: `if`/`while`/`for`/`do`/`switch`/`goto`/
  `return`/`break`/`continue`, blocks, labels), plus `cfunc` (function bodies)
  and `cinit`/`strinit` (initialisers).
- **`c03.c`** — declarations: `getkeywords`, `declare`/`decl1`, `getype` (the
  declarator recursion → pointer/array/function types), `strdec` (struct/union/
  enum), `align` (member and bit-field layout), and the push-down (`pushdecl`).
- **`c04.c`** — `treeout`/`rcexpr`/`cbranch`/`branch`/`label` walk a finished
  tree and emit it, the type arithmetic (`incref`/`decref`, `length`/`plength`),
  and `outcode()` — the one function that writes the byte stream.
- **`c05.c`** — pure data: `opdope[]` (per-operator priority + flag bits),
  `cvtab`/`cvntab` (the conversion matrix and its operator mapping), and `ctab[]`
  (the 128-entry character-class table the lexer indexes).

---

## 3. From source to stream

`main()` loops `extdef()` until EOF. Each external is either a data definition —
whose initialiser is walked by `cinit`/`strinit` and emitted as `DATA`/`BSS`/
`SSPACE` items — or a function, handed to `cfunc()`, which frames the body
(`PROG`/`SAVE`/`SETREG`/`RETRN`/`SETSTK`) and calls `statement()` for it.

Names live in one open-addressed table, `hshtab[HSHSIZ]` (800 entries). `lookup()`
hashes the 8-char name (`NCPS`) and linear-probes; keyword slots are pre-marked so
a name that collides with a keyword is checked against `kwtab` first (`findkw`).
Block scope is handled by *push-down*: an inner declaration of an outer name saves
the outer entry through `hpdown` (`pushdecl`), and `blkend()` restores or discards
it and rehashes retained names.

Expressions are the heart of it. `tree()` reads tokens, using `opdope`
priorities to decide shift-vs-reduce, and calls `build()` to reduce. `build()` is
where the type rules live: it disarrays arrays and takes the address of functions
(`disarray`/`chkfun`), consults `cvtab[lintyp(t1)][lintyp(t2)]` for each operand's
conversion and applies it (`convert` → an `ITOF`/`ITOL`/… node), folds constant
operands (`fold`, so array bounds and `case` values reduce to a `CON`), and builds
the operator node. A finished tree is handed to `rcexpr()`/`treeout()` (c04),
which post-order-walks it into `outcode()`.

---

## 4. The intermediate stream — the c0 → c1 interface

`outcode(fmt, ...)` writes a flat, host-independent stream. The code stream goes
to `temp1`; strings and data go to `temp2` (selected by the global `strflg`),
which `c1` reads after `temp1`. The format codes:

| code | bytes | meaning |
|------|-------|---------|
| `B`  | value, `0376` | an operator/opcode byte + marker |
| `N`  | lo, hi | a 16-bit little-endian word |
| `L`  | 4 bytes | a 32-bit long (high word first, each word little-endian) |
| `S`  | `_name\0` | a symbol name (≤ `NCPS`, `_`-prefixed if non-empty) |
| `F`  | string`\0` | a float-constant string |
| `0`/`1` | word | the constant 0 / 1 |

The opcodes are the `#define`s in `c0.h` (`SYMDEF`, `PROG`, `RLABEL`, `SAVE`,
`CON`, `RFORCE`, `EXPR`, `RETRN`, `EOFC`, …). Because the stream is defined in
bytes and 16-bit words, it is **identical on the PDP-11 and on the host** — only
the programs at each end differ. This is the contract the whole port is measured
against (§7): the bytes must match what native `c0` emitted — which is why the
`L` code reproduces native's high-word-first long order, not the host's.

---

## 5. Authentic 1970s C on a 64-bit host

`c0`'s *own* source is pre-1977 C, and the hazard is the C it *is*, not the C it
compiles. Target semantics are untouched (still 2-byte `int`s and pointers); only
the compiler's own word assumptions widened. On the PDP-11 an `int` and a pointer
were both 16 bits and freely interchanged — on LP64 they are 8 and 4 bytes, so
every pointer-in-`int` had to be found.

- **The tree nodes are a union superset.** Old C reached any variant tail
  (`cnode`/`lnode`/`fnode`) through a bare `struct tnode *`, relying on the
  shared `op/type/subsp/strp` prefix. `tnode` is made a superset whose anonymous
  union holds `{tr1,tr2} | value | lvalue | cstr` (built with `-fms-extensions`);
  the prefix still aliases the variant structs exactly, so a `tnode *` and a
  `cnode *`/`lnode *`/`fnode *` alias the same fields.
- **The parameter list carries a real pointer.** It was chained through the
  `int hoffset` field; `hshtab` gained an honest `hpnext` pointer so the link is
  not truncated on LP64.
- **The pointer/int register-reuse idioms were made honest.** Where the original
  reused one variable as both an `int` and a node pointer, the port splits them
  (`build`'s `SIZEOF` uses the spare `p3`) or fixes the type: `statement()`'s
  `o1`, which holds a label number *or* a token, is now an honest `int`; the spots
  reusing a `hshtab *` slot as an `lnode`/tree node are explicit casts.
- **`outcode()` uses `<stdarg.h>`.** The original walked its arguments as PDP-11
  stack words (a `long` = two words), invalid on a register-argument ABI;
  arguments are now gathered by type, the `L` code replacing the old two-`N` long
  walk. The on-disk bytes are unchanged.
- **The node arena is `malloc`, not `sbrk`.** Raw `sbrk` collides with the host
  libc `malloc` that `c0`'s own stdio uses; `main` takes one 16 MB region,
  `gblock` bump-allocates through it (never less than a full `tnode`, zeroed),
  reset per function via `funcbase = curbase`.

One subtle bug was a *dangling* one: `cmst[]`, the operand stack `tree()` builds
via the global `cp`, was a `tree()`-local array — but callers keep writing through
`cp` **after** `tree()` returns. On the PDP-11's stack that survived; on the host
the reused frame's writes smash the stack. `cmst` is now file-scope (BSS).

---

## 6. The syntax-error NULL guards

On the PDP-11, reading address 0 returned a harmless 0, so following a NULL tree
pointer on an error path merely produced nonsense; a 64-bit host faults. The
paths where `tree()` returns 0 (a diagnosed syntax error) are now guarded, so a
bad input yields a diagnostic and a clean non-zero exit, not a crash:

- `conexp()` returns `t ? t->value : 0` instead of dereferencing;
- `dogoto()` returns early when `tree()` gave 0, rather than building on it;
- `doret()` skips the return-value conversion on a 0 tree (still branching to
  `retlab`);
- `chkfun()`/`disarray()` pass a NULL argument through untouched — the empty
  arglist of a no-argument call `g()` flows through them as 0.

Relatedly, a `SEQNC` (comma-expression) node now copies the right operand's
`subsp`/`strp`, so a struct- or array-typed sequence does not later hand
`length()`/`treeout()` a NULL `strp`.

---

## 7. Testing

The output is held to the strongest standard available: **byte-identity with the
compiler that actually built 2.xBSD.**

- **`tests/c0/return42.sh`** parses `int main(){return 42;}` through `cpp` + `c0`
  and checks the emitted stream by opcode — `SYMDEF _main`, `RLABEL _main`, the
  constant 42 as `CON`/`INT`/`0x2a`, `RETRN` — decoding the `od` hex directly.
- **The `cc` end-to-end suite** (`tests/cc/*`) compiles, assembles, links and
  runs real programs through the whole chain, so a change that still parses but
  emits a wrong tree is caught downstream.
- **The native-compiler oracle** (`oracle/run.sh`, `oracle/cc-corpus.sh`) is the
  decisive check: it feeds the same input to our `c0|c1|c2` and to the **original
  1981 2.8/2.9BSD binaries under `apsim`** and compares — the `c2` stage
  byte-exact, the `c1` stage by assembling both outputs and comparing the `.o`.
  This is where "byte-identical to native" is proven (cc-corpus 91/91).

---

## 8. For a maintainer

- **The intermediate stream is the contract.** Any change must keep the
  `temp1`/`temp2` bytes identical to native `c0`; the oracle proves it — run it,
  don't eyeball the tree.
- **Do not reorder the node prefix.** `op/type/subsp/strp` must stay first and
  identical across `tnode`/`cnode`/`lnode`/`fnode`; the union superset and
  `-fms-extensions` are load-bearing, not stylistic.
- **Keep the arena on `malloc`.** Reintroducing `sbrk` corrupts the host heap
  `c0`'s stdio shares; the fixed region and per-function reset are deliberate.
- **Keep the NULL guards on the error paths.** They turn a host segfault back
  into the diagnostic the PDP-11 would have limped through.
- **`MENLO_OVLY` must stay defined** (via `whoami.h`). `c0` unconditionally emits
  `SETSTK` as `-maxauto+STAUTO`, and only `c1`'s MENLO stack formula matches it;
  with it undefined the two passes disagree on the frame and a callee clobbers a
  saved register. The `-V` overlay path it enables stays dormant (`cc` passes
  only `-P`).
