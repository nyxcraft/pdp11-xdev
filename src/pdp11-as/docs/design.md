# pdp11-as — design

This document describes how `pdp11-as` turns 2BSD assembler source into a
PDP-11 object file: how one binary covers every era from First Edition (1972)
to 2.11BSD, where the opcode knowledge lives, how a name is looked up and
era-gated, the two-pass span relaxation, the `sys` pseudo-op and the `~`
local-symbol replay, and the three a.out formats it can write. For how to *use*
it, see the [user guide](user-guide.md); for the era dialects in full, see
[std.md](std.md).

The tool is one file, `as.c` (~2400 lines), a C reimplementation of the 2BSD
`as` — which is itself pure PDP-11 assembly and so could not be cross-compiled.
It runs on a modern LP64 host and produces the classic objects this toolchain's
`ld` and `nm` read (**not** the newer string-table a.out GNU `pdp11-aout`
emits). The opcode knowledge lives in two hand-laid tables, `optab.h` and
`extoptab.h`.

---

## 1. One binary, three orthogonal era axes

The awkward fact `as` exists to handle: across the lineage it descends from,
the *instruction set*, the *system-call vocabulary*, and the *object format*
each changed on their own schedule. Rather than an assembler per era, `as`
carries three independent switches, each also reachable through a `--std`
preset that sets a sanctioned combination:

| axis | flag | state | choices |
|---|---|---|---|
| instructions | `--isa=` | `isa` / `jflag` | `1972` (V1–V3), `0` common (V4–2.10, default), `211` (2.11 / J-11), `extended` (full hardware line, `jflag`) |
| syscall names | `--sys=` | `sysnames` | `0` none (V7+), `1972`, `6` (V4–V6) |
| object format | `--aout=` | `v1fmt` / `newsym` | `v1` First Edition, `v2` 0407 (default), `v2+` 2.11 string-table |

The axes are genuinely orthogonal: `--isa=211 --sys=v6` describes a machine
that never existed, and the assembler will happily be it. A `--std=<era>` token
sets all three plus `v7flag` (undefined-symbol strictness) to one historical
combination — it is a preset, never a fourth axis.

---

## 2. The opcode and keyword tables

`as` never hard-codes a mnemonic; every one lives in a `struct op {name, type,
opcode}` table. There are two headers:

- **`optab.h`** — the default table, extracted verbatim from the authentic
  2.8BSD `as19.s`: registers, the double- and single-operand instructions, the
  branches and their `jbr`/`jxxx` pseudo-forms, `sys`, the FP11 group, EAE,
  and the directives.
- **`extoptab.h`** — four tables that are **not** in the default set: `tab211`
  (the 2.11 / J-11 additions — `mfpi`, `spl`, `mfpt`, `csm`, …), `v1systab`
  (the 1972 syscall-name keywords), `v6systab` (the V4–V6 list), and `extoptab`
  (the extended hardware line — FIS, the full CIS, `med`/`xfc`, FP11 DEC
  names). They are opt-in because their names collide with symbols in the era
  corpora, and a builtin name silently suppresses a same-named `^`-alias
  definition at write-out.

Both headers are wrapped in `/* clang-format off */` on purpose: **the tab
columns are the documentation**, hand-aligned so the table reads as a table.
The marker must be exactly `clang-format off` — trailing text on that line is
not honoured, so the columns would silently reflow otherwise.

The `type` field is a class key (also from `as19.s`) that selects the operand
encoder:

| type | class | type | class |
|---|---|---|---|
| `01` | absolute / no-operand | `020` | `.even` |
| `06` | branch | `023` | `.globl` |
| `07` | jsr / xor | `024` | register name |
| `010` | rts | `025`/`026`/`027` | `.text`/`.data`/`.bss` |
| `011` | sys | `030` | mul / div (EIS) |
| `013` | double-operand | `031` | sob |
| `015` | single-operand | `032` | `.comm` |
| `016` | `.byte` | `035`/`036` | jbr / jxxx |
| `017` | `.ascii` | `040` | `.word` (bare expression) |

---

## 3. Name lookup and era gating

`oplook()` walks the tables in a fixed order and returns the first live match:
`optab` (filtered by `kwskip`), then `tab211` (if `isa==211` or `jflag`),
`v1systab` (if `sysnames==1972`), `v6systab` (if `sysnames==6`), and `extoptab`
(if `jflag`). Duplicate mnemonics in the base table — `mul`/`div`/`ash` appear
first as absolute EAE addresses and later as the EIS instruction — are resolved
**last-wins**, so `mul` is the instruction, matching 2BSD's last-wins symbol
table.

`kwskip()` era-gates entries that exist in `optab` but not in the selected era:

- under `isa==1972`: the `jbr`/`jxxx` pseudo-branches (types 035/036), `.ascii`,
  and the V4 FP additions `ldfps`/`stfps`/`movie`/`movei` are hidden;
- under any `sysnames`: the `wait` **instruction** is hidden, because every era
  that names its calls makes `wait` the syscall (7), so `v1systab`/`v6systab`
  win;
- under `isa==211`: the ancient EAE keywords (`ac`, `mq`, `csw`, `mpy`, …)
  disappear, exactly as 2.11 dropped them.

---

## 4. Two passes and span relaxation

`as` is a two-pass assembler — pass 1 assigns addresses, pass 2 emits code,
relocation and symbols — but pass 1 is really a three-part span relaxation that
reproduces the 2BSD `as1`+`as2` structure so the branch sizes come out
identical:

1. **the `as1` estimate** — a single top-to-bottom pass sizing each branch
   inline: a forward text branch is long, a backward one short iff its
   displacement fits `[-254,0)`. Not iterated to a fixpoint — that over-sizes
   backward branches.
2. **the `as2` sizing pass**, run once over that estimate — each branch decides
   from `target − dot` (forward refs corrected by `brdelt`, the most recent
   label's estimate drift); the choices are final, recorded in `spanlong[]`.
3. **pass 2** replays those choices and emits.

`jbr`/`jxxx` (types 035/036) collapse to the short branch form when the target
is a near text label; otherwise they stay in the long `jmp` form — correct if
not always minimal. Symbol-table indices are assigned **between** the relaxation
and pass 2, because `emitword()` stamps a symbol's index into each external
relocation word and the index must already match the symbol's position in the
table `writeout()` emits.

---

## 5. The `sys` pseudo-op and named system calls

`sys` (type 011) reaches `sysop()`, which requires an absolute operand and emits
`base | (v & 0377)` — base `0104400`. The trap code is the **low 8 bits**, not
6: the library's job-control stubs write `sys read+200`, and a `&077` mask would
truncate the `0200` flag bit. `emt` (type 041) takes an *optional* operand
(`emt` alone is `emt 0`).

When a `--sys` list is active its names become keywords, so `sys write`
assembles by name instead of `sys 4`. The two lists that ever existed are
carried verbatim: the 1972 table (`v1systab`, with `quit`/`intr`/`cemt`/
`ilgins`) and the V4–V6 table (`v6systab`, with `signal`). Both make `wait`
the call, never the instruction.

---

## 6. The `~` local-symbol replay

c2 emits local/sdb symbols spelled with a leading tilde (`~~funcname:`,
`~var=reg`). The authentic `as` (`as14.s`) strips one `~`, marks the symbol "not
for the hash table", and appends it to the symbol list directly — so these are
**never deduplicated**: each `~var=reg` in a different function is a distinct
entry (the source of the +10 symbol count versus a hashing assembler).

`as.c` mirrors that with `tildesym()`: a per-pass occurrence counter
(`tildeidx`) indexes a flat `tildesyms[]` array, so the Nth `~`-symbol is the
*same object* across every relaxation pass (stable values and `brdelt`) while
distinct occurrences stay distinct. They are never hashed, so ordinary lookup
never returns one — matching the define-only, stripped-from-the-binary role.

---

## 7. Three a.out formats on the way out

`writeout()` emits the symbol table in first-creation order (walking the
`onext` insertion chain, not the hash buckets) because `ld` assigns
common/bss addresses in that order — the order is load-bearing. All three
formats bias a symbol's value into the unified object address space (text@0,
data@`txtsize`, bss@`txtsize+datsize`), which `ld` backs out when combining
objects.

| `--aout=` | header | relocation | symbols |
|---|---|---|---|
| `v1` (`v1fmt`) | 6-word, magic **0405**, 12 bytes | 2-bit **bit-stream**, MSB-first: `00` abs, `01` reloc, `10` pcrel-external, `1101` abs-external (`1100`/`1110` carry a 16-bit addend extension); external words hold the symtab offset `12*index` | V1 flags (00 undef / 01 abs / 02 reg / 03 reloc, `|40` global) |
| `v2` (default) | 8-word, magic **0407**, 16 bytes | one 16-bit word per code word, `(index<<4) \| rtype \| pcrel`, `rtype ∈ {0, 02 text, 04 data, 06 bss, 010 ext}` | 12 bytes: `name[8]`, `n_type`, `n_ovly=0`, value |
| `v2+` (`newsym`, `-n`) | as `v2` | as `v2` | 8-byte nlist (`strx` as a PDP-11 long, high word first; type; value) plus a trailing string table; names up to 32 chars |

The First Edition path merges text and data into one contiguous body before
streaming relocation (their addresses are already contiguous there); the result
is a file a simulated 1971 system, or apout, can `exec()`.

---

## 8. Testing

The standard of proof is byte-identity to the native 2.9 `as`, run under apsim:

- **`oracle/selfhost.sh`** rebuilds the toolchain's own binaries from source —
  assembling 2.9's `as0.s`/`as2.s` with this `as` reproduces the native
  `/bin/as` and `/lib/as2` **byte-for-byte**.
- **`oracle/as-corpus.sh`**, **`cc-corpus.sh`** and **`tree-sweep.sh`** sweep
  whole 2.9 source trees and `cmp` each `.o` against native `as`: **~1400 files,
  zero differences**. The one non-match (`sys/lfstat.s`) is a `sys.s` version
  skew, not an assembler bug.
- **`tests/as/encode.sh`** disassembles the output with GNU `objdump -m pdp11`
  to confirm every mode, target and immediate decodes; an apsim CIS golden
  exercises the `--isa=extended` encodings.
- The **das round-trip** oracles (`das → as → identical object`) prove the
  symbol-table order and external-relocation indices survive losslessly.

---

## 9. For a maintainer

- **The tables are the spec.** `optab.h`/`extoptab.h` are hand-columned under
  `clang-format off`; add an entry in the right alignment and never let the
  formatter reflow them. Put an era-specific mnemonic in the era table
  (`tab211`/`extoptab`), not the default one.
- **Era-gate through `kwskip`/`oplook`**, not by editing the base table. A
  builtin name suppresses a same-named `^`-alias symbol at write-out, so adding
  a keyword can silently drop a corpus symbol.
- **Keep the three axes orthogonal.** `--isa`/`--sys`/`--aout` are independent; a
  `--std` token is only a bundle of them plus `v7flag`.
- **Hold output to `cmp` against native `as`** (selfhost + the corpus sweeps) —
  that catches a wrong data/bss bias, relocation type, or symbol order, which a
  disassembly check alone does not.
- **The object is written even when assembly errored** (authentic 2BSD: `as`
  never unlinks a partial `.o`); the nonzero status is reported after
  `writeout()`.
