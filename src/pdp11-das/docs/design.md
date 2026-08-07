# pdp11-das — design

This document explains what `pdp11-das` is, the input shapes it reads, the two
things it is strict about (decoding from the assembler's own tables, and
round-tripping through `as`), and the era dimension a PDP-11 disassembler has:
reading every object format from First Edition (1971) to 2.11BSD, and speaking
each era's syscall and instruction-set dialect. For how to *use* it, see the
[user guide](user-guide.md).

The source is a single `das.c`. The instruction set comes from the assembler's
shared table `../pdp11-as/optab.h`, and the on-disk header/nlist/archive layout
from `../common/cross/a.out.h` and `../common/cross/ar.h`. It runs on the LP64
host and reads the 16-bit little-endian formats **explicitly** (`w16()` byte
loads, never a struct overlay of the host's own `int`/`long`), so it is
endian- and word-size-clean.

The reassembly engine has grown large. This document is the architecture; the
[field guide](fieldguide.md) is the byte-level companion — the slot-pinning,
splice, and metadata rules that carry `-a` output to byte-exact round-trip.

---

## 1. What it is

`pdp11-das` is the **inverse of `as`**: it reads a PDP-11 object and prints the
instructions back. It decodes through the *same* opcode table the assembler
encodes from — `das.c` does `#include "../pdp11-as/optab.h"` with its own
`struct op { char *name; int type; int opcode; }`, the single source of truth —
so the two agree by construction on mnemonics, operand forms, and the whole
instruction set. The decoder covers the no-operand ops, the double- and
single-operand groups (byte variants included), branches, `jsr`/`rts`/`sob`/
`mark`, the EIS set (`mul`/`div`/`ash`/`ashc`/`xor`), the FP11 floating unit, the
MMU move-to/from-previous-space ops (`mfpi`/`mtpi`/`mfpd`/`mtpd`/`mfps`/`mtps`),
`sys`/`emt`, the condition-code ops, and all eight addressing modes including
immediate (`$x`), absolute (`*$x`), and PC-relative. `r7`/`r6` print as `pc`/`sp`.

---

## 2. Input shapes and the file-type dispatch

`main` reads the whole file into memory and dispatches on the first word:

- **archive** (`ARMAG`, `0177545`) → `do_archive`;
- **`0405`** → `do_object` directly: a First-Edition **12-byte-header**
  executable, whose text *includes* the header;
- **`0407`/`0410`/`0411`** → `do_aout_split`, which splits if the linker left
  per-object boundaries and otherwise emits one listing;
- anything else → `not a PDP-11 a.out, object, or archive`.

A **bare object** (`.o`) disassembles to one listing. A **linked `a.out`** is
split back into per-object listings using the `N_FN` (037) file-name symbols
`ld` leaves in the symbol table — each marks where an input object's text began;
they are sorted by text address and one `<stem>.<object>.dis` is written per
object (a duplicated file name gets a sequence suffix). Data and bss cannot be
attributed to an object from the `a.out` alone, so they go to a single
`<stem>.DATA.dis`. With no `N_FN` boundaries — or under `-p` — it is one listing
to stdout. An **archive** disassembles each member (skipping `__.SYMDEF`) to its
own listing; the 26-byte member header's size is a PDP-11 `long` read HIGH word
first, and members are word-aligned.

---

## 3. The era dimension: two symbol-table formats

A disassembler answers to the era exactly where the object format does, and for
`das` that is the **symbol table**. Two shapes are live:

- **classic nlist (V1–2.10)** — a **12-byte** entry: `n_name[8]` stored
  **inline** (no string table), a type byte, an overlay byte, and a value word.
- **2.11 `Newsym` (`as -n`)** — an **8-byte** entry: an `off_t` string index (a
  PDP-11 `long`, **HIGH word first**), a type word, and a value word, followed by
  a **string table** (its leading `long` is the total length; offsets count from
  4). Names run up to ~39 characters.

`readsyms` tells them apart by **geometry**, not a flag: the classic format fills
the object exactly, whereas the `Newsym` layout leaves exactly a string table
(a length word whose span ends at the object's end) after the symbols. Because
the long 2.11 names have to survive formatting, `struct sym.name` is `[40]` and
the operand scratch buffers are `o1[96]`/`o2[96]` — sized to hold a ~39-char name
plus its `$`/offset decoration. The banner announces the `Newsym` case
(`[nsym: … as -n]`) so the harness reassembles with the matching flag.

The classic reader also repairs what `as` records lossily: it blanks names
carrying bytes outside `as`'s identifier charset (the 1972 `unix.out` kernel has
such entries) and entries `as` could not have produced, and it gives a later
duplicate of a full-8-char name a synthetic 9th character so the two spellings
land in distinct hash chains (V6 `libp`'s truncated `_IEH3out…`). A First-Edition
`0405` image additionally has its V1 symbol flags (00 undef / 01 abs / 02 reg /
03 relocatable / |40 global) translated into the later base types, and its
relocation is a **2-bit-code bit stream** after the symtab, decoded into the same
parallel array the later per-word relocation feeds — so one machinery serves both.

**Syscall and ISA dialects are separate axes**, selected by option (see the user
guide): the `sys`/`trap` inline-argument counts and the V1-era `..` load bias
(`-2`/`--sys=v1`, `-6`/`--sys=v6`, `--sys=bsd211`), and the opt-in late-hardware
decode `-J` (MFPT, SPL, CSM, TSTSET/WRTLCK, FIS, MED/XFC, CIS). Object *formats*
are auto-detected; only these behavioral dialects are chosen by flag, and `--std`
is a convenience that sets them together per named era.

---

## 4. Code/data separation — the recursive-descent walk

A linear sweep mis-decodes data embedded in the text (a jump table, a `jsr r5`
argument byte that is itself a `jmp` opcode) as instructions and fabricates bogus
branch targets `as` then rejects. `das` instead walks the actual **control flow**
(`markcode`) to map which bytes are reachable as code; the rest is emitted as
data words. The walk is seeded from: every defined text symbol (an odd-valued
symbol becomes a barrier, never an instruction start); every **absolute text
pointer** (a word relocated `RTEXT` but *not* PC-relative, in either text or
data — a jump-table entry or a handler an interrupt vector points at); and, when
there is no text symbol at all, address 0. The `sys` inline-argument words are
stepped over using counts mirroring the kernel's `sysent` table (`sy_narg −
sy_nrarg`), varied by the `-2`/`-6`/`bsd211` personality.

Two refinements matter. **The PC-relative theorem:** data is never PC-relative,
so a gap carrying a PC-relative relocation is necessarily code — `das`
trial-decodes it and commits only if the decode is self-consistent (every pcrel
word lands on an operand, the instructions tile the gap exactly, every branch
target hits a boundary), which is what recovers interrupt stubs reached only via
hardware vectors. **Code in `.data`:** `markdata` applies the same idea to the
data segment. For a **stripped** binary — no symbols, no relocation — the walk
seeds the text start plus every `jsr r5,csv` prologue (`004567`), the standard
2.8 C entry, so it reaches functions the absolute-call chain would otherwise hide.
bss and data are rebuilt from the symbol table at **byte** granularity, so a
`.byte`-sized field at an odd address keeps its label rather than being dropped.

---

## 5. The round-trip guarantee

Because `das` decodes from the assembler's own table, its `-a` output is a
faithful inverse: **`das -a | as` reproduces the object's text and data byte for
byte.** That is the strongest correctness statement available for a disassembler,
and the acceptance test the engine is built around; it is proven over the wide
corpus (1130/1130 objects) and the 2.11 corpus (1665/1665). `-a` emits clean `as`
source — `/` comments, `.globl` declarations, no address/byte columns — not a
listing.

Reaching byte-exact over hand-written assembly needed the machinery the
[field guide](fieldguide.md) documents in full: numeric local labels resolve
in-memory in `as` and are never written to the symtab, so a target with no named
symbol gets a synthetic `.L<addr>` (an odd target anchors as `.L<even>+1`);
symbol-table order is reproduced by generating the body first and **interleaving**
`.comm`/`.globl`/`.lcomm` declarations at each symbol's first-mention position;
and any operand encoding `as`'s grammar provably cannot request is **spliced** as
raw `.byte`s while relocation-bearing words inside the splice still emit
symbolically. Splicing decouples byte-fidelity from grammar completeness — a
wrong heuristic can then only ever degrade a label or an ordering, never corrupt
content. The engine runs the walk under a rebuild loop with an escalation ladder
(promote-wishes → assign-pins → force-synth) so a symbol the natural walk cannot
pin in slot order still interns at the right index.

---

## 6. Relocation-stripped images (kernels, unstripped executables)

By default (`SymNoReloc`, on) `das` **keeps** the symbol table of a
relocation-stripped image — kernels and unstripped executables of every magic —
labelling at symbol addresses even though no operand relocates. Symbols `as`
cannot spell directly are emitted through `name = value ^ donor` casts; operands
stay numeric; the harness (`ldnr.py`) restores `ld`'s symtab order and strips the
relocation `das` adds. `-s` opts *down* to the older all-numeric content mode
(symbols dropped); `-y` is accepted as a no-op for an older harness spelling.

An `ld -i` kernel or a shared/separate-I&D executable stores its data and bss
symbol values **data-space-relative** (they restart near 0), so `das` normalizes
them into a unified address by `+Tsize` — format-defined for `0411`, and decided
by an address-distribution **vote** for `0410`/`0407` where linking convention
varies. The sum is masked to 16 bits, so a high bss symbol wraps; where its
masked address lands in the wrong segment, `seg_mismatch` routes it to a cast
rather than a mislabelled datum.

---

## Testing

The behavioral test is `tests/cc/das.sh` at the repo root, four stages against
freshly compiled input: (1) a bare object disassembles with symbol labels
(`_sq:`, a decoded `mul`, the `jsr` csv prologue); (2) a linked `a.out` **splits**
into per-object `.dis` files with `_main` and `_helper` in the right ones;
(3) an **archive** member disassembles to its own file; (4) the **round-trip** —
`das -a | as` — is byte-identical over the text and data segments. Beyond that,
the `oracle/das-*.sh` sweeps (`das-sweep`, `das-exec`, `das-linked`, `das-sepid`,
alongside `as-corpus`, `lib-sweep`, `kernel-sweep`, `ld-sweep`, `s-sweep`) run the
round-trip and idempotency checks across the whole multi-era corpus and bucket
the failures — the discipline the field guide insists on: sweep first, keep second.

---

## For a maintainer

- **Decode from `optab.h`, always.** The agreement with `as` — and the
  round-trip — depends on the one shared opcode table. A second, hand-copied
  decode table is a second thing to keep in step, and how an off-by-one reaches
  exactly one instruction in one file.
- **Both nlist shapes are live, and told apart by geometry.** The classic inline
  12-byte format and the 2.11 8-byte `Newsym`-plus-string-table format both occur;
  do not assume either, and keep the name/operand buffers sized for the ~39-char
  2.11 names.
- **The walk is control flow, not a linear sweep.** Preserve the seed set (text
  symbols, absolute text pointers, csv prologues, entry 0), the odd-address
  barrier, the `sys` inline-arg stepping, and the PC-relative gap theorem —
  each earns a real class of files.
- **A splice can only ever cost metadata.** Keep the invariant that reached,
  correctly-decoded instructions splice to raw bytes when `as`'s grammar cannot
  request the exact encoding, and that relocation-bearing words inside a splice
  still emit symbolically. That is the safety floor the heuristics stand on.
- **Verify every new heuristic against the whole corpus.** A rule that looks
  perfect on the file that motivated it will silently misfire on hundreds of
  others; measure its firing rate before keeping it, and keep it only at or above
  the current best.
