# Porting guide: as (assembler)

The assembler is the one tool that could not be ported by compiling its
source: the 2.8BSD `as` is **pure PDP-11 assembly** (`as/orig/as1?.s`,
`as2?.s`, ~3600 lines) using raw syscalls, so it cannot be cross-compiled.
Per the project's "fully authentic" decision it is **reimplemented in C**
(`as/as.c`) — accepting the exact 2BSD assembler syntax c1 emits and
producing classic 2.8BSD a.out objects.

## Authoritative references used

- **`as/optab.h`** — the opcode/keyword table, extracted verbatim from the
  authentic `as19.s` (147 entries: mnemonic, type class, base opcode). The
  type-class key (also from as19.s) drives operand encoding.
- **`ld/ld.c`** — the relocation word format and a.out layout (this
  toolchain's ld reads what as writes).

## Output format — classic 2.8BSD a.out (NOT GNU)

GNU binutils `pdp11-aout` emits a *newer* BSD a.out with a string table and
4-byte symbol indices; classic 2.8BSD (what `ld`/`nm` here read) is
different, so `as` emits the classic form:

```
header  8 words: magic 0407, tsize, dsize, bsize, ssize, entry, 0, relflg
text    tsize bytes
data    dsize bytes
treloc  tsize bytes   one 16-bit relocation word per text word
dreloc  dsize bytes
symbols ssize bytes   12 bytes each: name[8], n_type, n_ovly, value
```

Relocation word = `(symbolindex << 4) | rtype | pcrel`, with
`rtype ∈ {0, 02 text, 04 data, 06 bss, 010 extern}` and bit 0 = pc-relative
(matching `ld.c`'s `RABS/RTEXT/RDATA/RBSS/REXT/RELFLG`).

## What it implements

- Two passes (pass 1 assigns addresses; pass 2 emits code + relocation +
  symbols).
- All PDP-11 addressing modes: `Rn`, `(Rn)`, `(Rn)+`, `-(Rn)`, `X(Rn)`,
  the `*`/deferred variants, `$imm`, `*$abs`, and bare-symbol PC-relative
  references (`name` → `X(pc)`).
- Instruction classes: double-operand, single-operand, branch, `jsr`/`xor`,
  `rts`, `sys`, EIS `mul`/`div`, `sob`, and the span-dependent `jbr`/`jxxx`
  (always emitted in the long `jmp` form — correct if not minimal; c2 would
  shorten them).
- Directives `.text/.data/.bss`, `.globl`, `.byte`, `.ascii`, `.even`,
  `.comm`, `.if/.endif`, and bare expressions (emit a word).
- Octal-default numbers with the `.`-decimal suffix, `'c` char constants,
  `<...>`/`"..."` strings, `/` comments, `.` location counter.

Verified by disassembling the output with GNU `objdump -m pdp11`: every
instruction, addressing mode, jump target, and immediate decodes correctly
(`tests/as/encode.sh`), and `cc -c hello.c` now produces a valid object
end to end.

## Status / known gaps

- **FP11 floating-point instructions are supported** (movf/movof/movfo,
  addf/subf/mulf/divf/cmpf/modf, clrf/tstf/negf/absf, movif/movfi/movei/movie,
  cfcc/setf/setd/ldfps) -- encodings verified against the binutils oracle and
  run in apsim.
- Numeric local labels (`1:`/`1f`/`1b`), multi-file input, the `..` relocation
  base, and unary `+`/`!` were all added for libc's hand-written assembly.
- A few rare directives outside what cc/libc emit remain unverified.
- Span-dependent jumps are not shortened (always long form).

## Build

```make
${BIN}/${PREFIX}-as: as/as.c as/optab.h
	${HOSTCC} ${O} ${COMPAT} -Ias -o $@ as/as.c
```

## Update: fixes found while assembling real c1 output

- **Duplicate mnemonics (mul/div/ash).** These appear in as19.s first as
  absolute EAE register addresses (type 1) and later as the EIS instruction
  (type 030).  2BSD's symbol table is last-wins; the lexer now keeps the
  last match so `mul` is the instruction, not the address.
- **Embedded NULs.** c1 emits `mov%c` with a 0 modifier for word ops, i.e.
  a literal `mov\0` in the assembly; the original as skipped it.  The lexer
  now treats an embedded NUL as whitespace and detects real end-of-input by
  buffer length, not the first NUL.

## Update: data/bss symbol biasing (found when ld was added)

Classic a.out uses a unified per-object address space: text@0, data@txtsize,
bss@txtsize+datsize (the authentic as's `datbase`/`bssbase`, as21.s).  So a
data symbol's value, and any internal reference to a data/bss symbol, must be
emitted biased by the preceding segments' sizes; ld's `cdrel`/`cbrel` back
the bias out when combining objects (a stored value of `txtsize+offset`
relocates to `offset+dorigin`).  `as` now applies this bias in the symbol
table, `doword`, and `emitextra` (including the pc-relative current-location
bias).  Without it, every data/bss symbol linked to the wrong address.
Also: the external relocation symbol index is assigned between pass 1 and
pass 2 (pass 2 stamps it into REXT words), and undefined-but-referenced
symbols are written `N_EXT|N_UNDF` so ld resolves them.
