# das (disassembler)

`das/das.c` is the inverse of this toolchain's `as`: a disassembler for the
three 2.8BSD PDP-11 binary formats.

```sh
pdp11-bsd29-das  file        # write <stem>.<object>.dis listings (one per object)
pdp11-bsd29-das -p file      # write a single combined listing to stdout
pdp11-bsd29-das -a file      # emit reassemblable `as' source instead of a listing
```

## Reassemblable output (`-a`)

With `-a`, das emits clean `as` source (no address/byte columns, `/` comments,
`.globl` declarations) that **reassembles to a byte-identical object**.  It is
the verified inverse of `as` across the **entire 2.8BSD `.s` corpus**: every one
of the 197 source files that assembles (`as o.o ... | das -a | as` again)
round-trips with byte-identical text+data — zero differences — covering compiler
output, all of libc, the syscall stubs, the boot loaders, the overlay runtime,
the Pascal interpreter (`px`), the kernel machine-config (`m40`/`mch`), and the
assembler's own source.  This requires reading the **relocation table** so each
relocated operand comes back as a symbol (`jsr pc,*$_doprnt`,
`mov $__iob+12,(sp)`) rather than a raw word.

Authentic 2BSD `as` keeps numeric local labels (`1:`/`1f`/`1b`) out of the
symbol table entirely -- they resolve in-memory (the `curfb` table) and are
never written to the `.o` (our `as` matches this).  A branch/reference whose
target therefore has no named symbol gets an **objdump-style synthetic label**
`.L<addr>`, emitted at the definition and used by every reference -- so the
disassembly stays readable and reassembles.

### Code/data separation (recursive descent)

A linear sweep mis-decodes data embedded in the text segment (jump tables, a
`jsr r5,error; 'x' char argument whose byte value is itself a `jmp' opcode) as
instructions, which fabricates bogus far branch targets that `as' then rejects.
das instead walks the actual **control flow** from every defined text symbol
(plus absolute text pointers, i.e. jump-table entries) to map which bytes are
reachable as code; the rest is emitted as data words.  A multi-word decode that
would straddle a known branch target is treated as data, and a relocated word
where an opcode should be (a `sys'/inline argument) is data too.  Compiler
output (clean code/data separation) is unaffected; the win is hand-written
assembly that interleaves data with code.

Some code is reachable only through a path the static walk cannot follow — a
hardware interrupt vector installed at run time, a computed jump.  Such a gap is
recovered when it carries a **pc-relative relocation**: data is never
pc-relative, so a pcrel word is necessarily an instruction operand, hence the
gap is code.  das trial-decodes the gap and commits it only if the decode is
**self-consistent** — every pcrel word falls on an operand (never an instruction
boundary), the instructions tile the gap exactly, and every branch target lands
on a real boundary.  A misaligned or data decode trips that test and stays data;
a false positive is harmless, since a self-consistent decode reassembles to the
identical bytes.  (This is what lets `m40.s`'s `jsr r0,call1' interrupt stubs,
entered only via hardware vectors, round-trip.)

The `sys' macro (`as''s name for the `trap' instruction) can carry inline
argument words -- `sys 0; 9f' is the trap plus the address of `9:'.  das knows
how many words each call takes from a table mirroring the kernel's `sysent`
(the `sy_narg - sy_nrarg` the trap handler skips, `sys/trap.c`), and steps the
control-flow walk over them; a relocated word where an opcode should be is
inline data too (`sys 0' / `9f', `mov 6(r5),0f+2').

### Segment reconstruction (byte granularity)

bss and data are rebuilt from the symbol table at **byte** granularity, because
a variable can be a `.byte`-sized field at an **odd** address — `m40.s` packs
`bflg`/`jflg`/`fflg`/`nofault` into four adjacent bytes, and `as11.s` puts
`outfile` mid-struct.  bss reserves space with `.=.+N` per label (byte steps, so
odd-addressed labels are kept); a **data** symbol on an odd byte splits its word
into two `.byte`s with the label between them.  A word-granular dump would
silently drop every odd-addressed symbol and shift all references to it — which
was, e.g., the whole of `m40`'s apparent "hardware-vector" round-trip difference
(the addresses were in the symbol table all along; no external data was needed).

## What it does

- **Object (`.o`)** — disassembles the one object to a single listing.
- **Linked a.out** — *splits it back into per-object listings*.  When `ld`
  links objects it leaves an `N_FN` (037) file-name symbol in the symbol table
  at the text address where each input object's contribution begins; `das`
  uses those to partition the text and writes one `<stem>.<object>.dis` per
  input object (e.g. `prog.crt0.o.dis`, `prog.main.o.dis`, …).  The data and
  bss segments cannot be attributed to objects from the a.out alone, so they
  go to a single `<stem>.DATA.dis`.
- **Archive (`.a`)** — disassembles each object member to its own
  `<stem>.<member>.dis`.

## Symbol labelling

Available debugging symbols label the output:

- text symbols (functions, local `L`-labels) become `name:` labels and resolve
  branch/`jsr`/`jmp` targets,
- data and bss symbols label the data dump and resolve absolute references,
- `pc`/`sp` are printed for r7/r6.

For a *bare* `.o`, references to external symbols (e.g. `jsr pc,*$_helper`)
appear unrelocated, since the relocation has not been applied; in a linked
a.out everything resolves.

## Formats (read explicitly, never by struct overlay, so it's LP64-clean)

- a.out/object header: 8 words (16 bytes) — magic, text, data, bss, syms,
  entry, unused, flag.  Symbol table follows text+data (plus the relocation
  area when `a_flag==0`, i.e. an unstripped `.o`).
- `nlist`: 12 bytes — `n_name[8]` (inline, no string table), `n_type`,
  `n_ovly`, `n_value`.  Types: N_TEXT=02, N_DATA=03, N_BSS=04, N_FN=037,
  +N_EXT=040.
- archive: the binary `ar` format, `ARMAG` 0177545, 26-byte member headers.

## Decoder

A direct decode of the PDP-11 instruction word(s) to mnemonic + operands —
double/single-operand, branches, `jsr`/`rts`/`sob`/`mark`, the EIS
(`mul`/`div`/`ash`/`ashc`/`xor`), the FP11 floating-point set, the MMU
move-to/from-previous-space ops (`mfpi`/`mtpi`/`mfpd`/`mtpd`/`mfps`/`mtps`),
`sys`/`emt`, condition-code ops, and all eight addressing modes including
immediate (`$x`), absolute (`*$x`), and PC-relative.  A PC-relative operand is
resolved to a symbol; one relocated RABS (a fixed absolute address reached
pc-relative, e.g. a kernel hardware register) is kept as a literal, and a branch
to the text/data boundary is emitted `.+N` (a `.L` label there would fall into
data).  An odd target byte is anchored to the even word as `.L<even>+1`.
Verified instruction-for-instruction against the GNU binutils `pdp11-aout`
objdump.
