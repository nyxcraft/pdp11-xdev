# Porting guide: c1 (compiler pass 2 + codegen table)

The PDP-11 code generator. Reads c0's intermediate stream and emits PDP-11
assembly, driven by a large code-generation table. The biggest and most
intricate port so far.

## Sources

- `c1/c10.c … c13.c` + `c1/c1.h` — the code generator. `c13.c` holds the
  instruction-mnemonic and opcode tables.
- `c1/table.s` — the code-generation **template table** (a custom language).
- `c1/cvopt.c` — authentic 2.8BSD tool: expands `table.s` to assembler.
- `c1/mktab.c` — **new host helper**: converts that assembler to C.

Invoked as `c1 temp1 temp2 out.s` (the two c0 temp files in, assembly out).

## The codegen table — the c1-specific wrinkle

On the PDP-11, `table.s` is processed by `cvopt` into assembler, assembled,
and linked into the c1 binary as data the code generator interprets at run
time. The host can't assemble PDP-11, so the table is turned into C:

```
table.s ──cvopt──▶ table.i ──mktab──▶ table.c ──cc──▶ table.o
        (templates→asm)     (asm→C)
```

`mktab.c` parses the regular `cvopt` output and emits:

- template strings `Ln:<jmp>;.byte 301` → `static char Ln[] = "jmp\301";`
  (a `.byte N` becomes `\NNN`, `\n` a newline, `"` is C-escaped, `\0` ends
  the string);
- the index tables `_regtab`/`_cctab`/`_efftab`/`_sptab` → `struct table
  NAME[] = { {op, optab}, …, {0,0} };`
- the per-operator optab arrays `crNNN=.+2;0` … → `static struct optab
  crNNN[] = { {deg1,typ1,deg2,typ2, str}, …, {0,0,0,0,0} };`
- `Ln=name` aliases and multi-label template heads (`add1:;L44:<…>`) →
  `#define`s.

Templates are emitted first, then the optab arrays, then the index tables,
so each only references names already defined.

`cvopt` itself needed the `=op` → `op=` operator fix to compile on the host;
otherwise it is the original.

## Porting fixes (compiler-pass classes, plus codegen specifics)

1. **`=op`/`=<<`/`=>>` operators** and **old-style initialisers** (including
   all the `char mov[] "mov";` instruction tables in c13.c) — as in c0.

2. **`const()` renamed.** A function named `const` is a syntax error in
   modern C (reserved keyword) → `constfold`.

3. **Node superset.** c1's variant node structs (`tname`, `xtname`,
   `tconst`, `lconst`, `ftconst`, `fasgn`) share only `{op,type}`, so
   `tnode` is a superset with a nested anonymous union
   (`{degree,tr1,tr2,mask} | {class,regno,offset,{nloc|name}} |
   {value,fvalue} | lvalue`), compiled with `-fms-extensions`.

4. **LP64 pointer truncation — pervasive and the main source of crashes.**
   *Every* function returning a node pointer must have an explicit
   `struct tnode *` return type **and** a visible prototype, or its 64-bit
   result is truncated to `int` (symptom: deref of an address with the top
   32 bits zeroed, e.g. `0x5556a000` for `0x55555556a000`). Fixed for
   `outname`, `tconst`, `unoptim`, `lvfield`, `acommute`, `isconstant`,
   `hardlongs`, `sdelay`, `ncopy`, … Pointer **parameters** declared
   K&R-implicit-`int` truncate the same way — e.g. `chkleaf`/`delay` took a
   `table` argument used as `struct table *`.

5. **Unsequenced side effects.** `*sp++ = tnode(op, geti(), *--sp, t)`
   relied on the PDP-11 compiler's fixed argument-evaluation order and
   stashed a node pointer in the `int t`. Rewritten as sequenced,
   properly-typed statements.

6. **Raw word access of scalars.** `X.fvalue.intx[i]` / `X.lvalue.intx[i]`
   (the old "global member" trick to view a `double`/`long`'s 16-bit words)
   → `((short *)&X.fvalue)[i]`, correct on a little-endian host for `long`.

7. **16-bit output width.** Negative constants printed with `%o` came out at
   32-bit width; the stack adjustment is masked with `& 0177777`. `psoct`
   emitted a NUL via `printf("%c", 0)` for positive numbers — guarded.

## Build

```make
C1FLAGS = ${O} ${COMPAT} -fms-extensions -Icross -Ic1
```

## Verification

`tests/c1/codegen.sh` drives cpp → c0 → c1 and checks the assembly:

```
int main(){ return 42; }      ->  mov $52,r0      (52 octal = 42)
int main(){ return 2 + 3*4; } ->  mov $16,r0      (constant-folded to 14)
int x=7; int main(){ return x; }
                              ->  _x: 7 in .data;  mov _x,r0
```

with the correct prologue/epilogue (`jsr r5,csv` / `jmp cret`).

## Floating point (done)

PDP-11 floating point is fully supported (authentic DEC F/D format, not
IEEE).  c1 already emitted the FP11 codegen; two host-vs-target porting bugs
were fixed:

- **Float constants** were emitted as the host's IEEE-754 words (the four
  `((unsigned short*)&fvalue)[i]`).  Added `decfloat()` (c10.c) to convert the
  host double to DEC F/D words instead.
- **Float negation** flipped bit 15 of word 0 of `fvalue` -- the sign bit on
  the PDP-11 (sign/exponent word first), but a low-mantissa bit on the host's
  little-endian double -- so negative constants were corrupted.  It now
  negates the host double directly (c12.c).

See `as` (FP11 encoding), `apsim` (FP11 emulation), and NOTES.md for the rest.

## Deferred

- A general pass to ensure all 16-bit-quantity constants print at PDP-11
  width may be needed for the assembler; only the visible `SETSTK` case is
  masked so far.

## LP64 robustness fixes (compiling real programs)

The initial port handled `return <const>` but crashed or miscompiled on
locals, loops, arithmetic, arrays, function calls and recursion.  Root
causes, all from porting pre-1977 K&R C to an LP64 host:

- **mktab octal bug (the big one).** `cvopt` emits the table's degree/type
  match bytes with `%o` (octal); `mktab` read them with `%d` (decimal), so a
  field like octal `77` (=63) became decimal `77`.  In `match()`,
  `tabdeg1 >= 0100` then wrongly fired the "operand must be `*`" skip, so
  **no code-table row matched any arithmetic operator** ("No code table for
  op: +").  Fixed by parsing/emitting those bytes in octal.
- **Undeclared pointer parameters / locals truncated to int.** `acommute`,
  `insert`, `ispow2`, `pow2`, `comarg` (params) and `pexpr` in c0 (a local
  `t = tree()`) defaulted to `int` and truncated the 64-bit node pointer.
  Each got its proper `struct tnode *` type, plus prototypes for the
  node-returning functions so callers don't truncate the return value.
- **getblk/gblock under-allocation.** With the union-superset `tnode`,
  variant structs (`tname`/`cnode`/…) are smaller than `tnode`; allocating a
  node at the variant size and using it as a full `tnode` read past the
  block.  Both allocators now allocate at least `sizeof(struct tnode)` and
  zero each block (`curbase` is reset per function, so blocks are reused).
- **NULL / uninitialised `tr2` deref.** `delay()` has a misplaced brace in
  the 2BSD source so `sdelay(&p->tr2)` ran for unary ops, dereferencing an
  unset `tr2`.  Harmless on the PDP-11 (address 0 reads as 0); a segfault on
  the host.  Guarded `sdelay` against NULL and moved the call inside the
  binary-operator check.
- **Variadic `error()`.** The fixed-parameter `error(s,p1,…)` truncated
  `char*` arguments (and the format string) to int on LP64; rewritten with
  `<stdarg.h>` in both c0 and c1.

After these, c0/c1 compile locals, `for`/`while`, `+ - *`, comparisons,
arrays, K&R function definitions and calls, recursion, globals and strings;
verified by disassembling the output with GNU `objdump -m pdp11`
(`tests/cc/programs.sh`).
