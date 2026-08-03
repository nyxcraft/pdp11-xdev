# Porting guide: c0 (compiler pass 1)

The front end of Ritchie's cc: lexing, parsing, declaration processing.
It reads preprocessed C and writes an intermediate byte stream that c1
turns into PDP-11 assembly.

## Sources

`c0/c00.c … c05.c` + `c0/c0.h`. Invoked as `c0 source temp1 temp2 [flags]`
— `temp1` gets the code stream, `temp2` the string/data, both read back by
c1 (see [c1.md](c1.md)).

## The intermediate stream (the c0 → c1 interface)

A flat, host-independent byte stream emitted by `outcode(fmt, ...)`:

| code | bytes | meaning |
|------|-------|---------|
| `B`  | value, `0376` | an operator/opcode byte + marker |
| `N`  | lo, hi | a 16-bit little-endian word |
| `L`  | 4 bytes | a 32-bit little-endian long |
| `S`  | `_name\0` | a symbol name (≤ NCPS, `_`-prefixed) |
| `F`  | string`\0` | a float-constant string |
| `0`/`1` | word | the constant 0 / 1 |

Opcodes are the `#define`s in `c0.h` (`SYMDEF` 207, `PROG` 202, `RLABEL`
114, `SAVE` 208, `CON` 21, `RFORCE` 110, `RETRN` 209, `EXPR` 214, `EOFC`
0). Because the stream is defined in bytes and 16-bit words, it is the same
on the PDP-11 and the host — only the *programs* at each end change.

## Porting fixes

c0 is **pre-1977 C** and needs several classes of change:

1. **Ancient compound-assignment operators** (61 sites): `=+ =- =* =/ =%
   =& =| =^` and `=<< =>>` → `+= … <<= >>=`. Modern gcc silently reparses
   `i =+ x` as `i = +x`, and `v =/ x` is a syntax error. In genuine 1981
   source the spaced form is unambiguous (the language *required* the space
   to avoid `=op`).

2. **Old-style initialisers without `=`**: `int isn 1;`,
   `int opdope[] {…}`, `struct tnode funcblk {…}` → insert `=`.

3. **`outcode()` K&R varargs.** It walked the argument list as PDP-11 stack
   words (`ap = &a; *ap++`), where an `int`/pointer is one word and a
   `long` two consecutive words. Invalid on x86-64 (register args). Rewrote
   with `<stdarg.h>`; the `long` that two `N` codes used to consume by
   walking two words now uses a dedicated `L` code. Added a prototype so
   callers use the varargs calling convention.

4. **The old-C "global struct members" idiom.** The node structs
   (`tnode`, `cnode`, `lnode`, `fnode`) share the `op/type/subsp/strp`
   prefix; the original reached any variant's tail through a plain
   `struct tnode *`. `tnode` is made a **superset** with an anonymous union
   holding `tr1/tr2 | value | lvalue | cstr` (compiled with
   `-fms-extensions`); the prefix still aliases `cnode`/`lnode`/`fnode`.
   The symbol-table struct `hshtab` is reached via the bare names
   `type/subsp/strp`, which alias `htype/hsubsp/hstrp` by offset on the
   PDP-11 — those accesses are renamed; a `tnode->tr1` used as a symbol is
   cast to `(struct hshtab *)`; field info reached through `strp`/`tr2` is
   cast to `(struct field *)`.

5. **LP64 pointer-in-int.** The parameter list was chained through the
   `int hoffset` field — a real `hpnext` pointer was added to `hshtab` so
   the link is not truncated. A node pointer assigned to `int t1` uses the
   spare `tnode *p3`. `paraml`/`parame` were declared `hshtab **` but used
   as `hshtab *`.

6. **`extern` on the data tables** (`opdope`/`ctab`/`cvntab`/`cvtab`)
   defined with initialisers in c05.c — otherwise `-fcommon` warns about
   tentative-definition size clashes.

## Build

```make
C0FLAGS = ${O} ${COMPAT} -fms-extensions -Icross -Ic0
```

## Verification

`tests/c0/return42.sh` parses `int main(){return 42;}` and checks the
emitted byte stream by opcode: `SYMDEF _main`, `RLABEL _main`, the
constant 42 as `CON`/`INT`/`0x2a`, and `RETRN`. Decoding the stream by hand
gives exactly the expected pass-1 form:

```
SYMDEF _main · PROG · RLABEL _main · SAVE · SETREG · BRANCH · LABEL
· CON(INT, 42) · RFORCE · EXPR · BRANCH · LABEL · RETRN · SETSTK · EOFC
```

## Caveats

c0 is verified on the straightforward integer path. A few pointer/int
comparison warnings remain around its `sbrk`/`coremax` memory management
(the `LNCPW`-assumes-a-16-bit-host code); fine for ordinary programs,
to be revisited as more complex code flows through.
