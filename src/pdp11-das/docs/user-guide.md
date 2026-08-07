# pdp11-das — user guide

`pdp11-das` is the PDP-11 disassembler — the inverse of `as`. It reads a PDP-11
object, linked executable, or archive from any era (First Edition 1971 through
V7 and 1/2.8/2.9/2.10/2.11BSD) and prints the instructions back, labelling
functions, data, and branch/call targets from the symbol table. It decodes
through the same opcode table the assembler encodes from, so the two agree on the
whole instruction set. It runs on your host.

For the input shapes, the two era symbol-table formats, the code/data walk, and
the round-trip property, see the [design document](design.md); for the byte-level
reassembly rules, the [field guide](fieldguide.md).

---

## 1. Synopsis

```
pdp11-das [-a] [-p] [-s] [-6] [-2] [-y] [-J]
          [--sys=...] [--isa=...] [--std=...] file
```

`file` is a bare object (`.o`), a linked `a.out`, or an archive (`.a`). The
format is auto-detected from its first word — a First-Edition `0405` executable,
a `0407`/`0410`/`0411` a.out, or an archive (`0177545`); the era *dialects* below
are the only things chosen by option.

---

## Options

| option | meaning |
|---|---|
| *(none)* | a listing: address, bytes, and disassembly, with symbols as labels |
| `-a` | emit **reassemblable** `as` source — `as` turns it back into the object |
| `-p` | write every listing to **stdout** (instead of per-object `.dis` files) |
| `-s` | content-only: **drop** the symbol table of a relocation-stripped image (all-numeric) |
| `-6` | V5/V6 syscall personality (`sys`/`trap` inline-argument counts) |
| `-2` | 1972 syscall personality, plus the V1-era `..` load bias (`040000`) |
| `-y` | accepted as a no-op (its behavior is now the default) |
| `-J` | decode late J11 hardware (MFPT, SPL, CSM, TSTSET/WRTLCK, FIS, MED/XFC, CIS) |
| `--sys=` | syscall axis alone: `none`, `v1`, `v6`, `bsd211` |
| `--isa=` | instruction set: `v1`, `v4`, `bsd211`, `extended` (`extended` = `-J`) |
| `--std=` | era dialect(s), comma-separated: `v1`..`v7`, `bsd`, `newbsd`, `extended` |

By default, `das` keeps the symbol table of a relocation-stripped image (a kernel
or unstripped executable) and labels from it; `-s` reverts to the older
all-numeric mode. `--std` is a convenience that sets the syscall and ISA axes
together for a named era; the object *format* is always detected from the file,
never from a flag.

---

## Output

By default a single listing — a bare object, or an `a.out` with no per-object
boundaries — goes to **stdout**. When there are several listings to separate,
each is written to its own `<stem>.<object>.dis` file:

- a **linked `a.out`** is split back into per-object listings by the `N_FN`
  file-name symbols the linker left behind (`<stem>.crt0.o.dis`,
  `<stem>.main.o.dis`, …); its data and bss, which cannot be attributed to an
  object, go together to `<stem>.DATA.dis`;
- an **archive** disassembles each member to `<stem>.<member>.dis`.

`-p` overrides that and sends everything to stdout instead. Each output opens
with a banner naming the tool and the object; a listing also prints the magic and
segment sizes, and the banner flags any reassembly caveat it detected (`as -n`
for a 2.11 string-table symtab, `as -V` for an overlay-assembler object,
`as --isa=bsd211` for a PC-relative MMU operand).

In `-a` mode the output is clean `as` source — `/` comments, `.globl`
declarations, synthetic `.L<addr>` labels for un-named targets — with no
address/byte columns, so `as` can read it straight back.

---

## The round-trip

`das -a` produces assembly that `as` reassembles into the **same object, byte for
byte** across the text and data segments — the disassembler is a faithful
inverse, proven over the whole multi-era corpus. This is useful for inspecting,
patching, and rebuilding an object you only have in binary form.

```
pdp11-das -a foo.o > foo.s        # recover reassemblable source
pdp11-as -o foo2.o foo.s          # foo2.o's text+data is byte-identical to foo.o
```

---

## Exit status

Zero on success. Non-zero on a usage error (an unknown option or a missing
`file` argument), a file it cannot open or fully read, or input whose first word
is not a recognized PDP-11 object, executable, or archive magic.

---

## Examples

```
pdp11-das foo.o                   # a listing to stdout
pdp11-das -a foo.o > foo.s        # reassemblable source
pdp11-das -p prog                 # a linked a.out, all objects to stdout
pdp11-das prog                    # split into prog.<object>.dis + prog.DATA.dis
pdp11-das libc.a                  # each member to libc.a.<member>.dis
pdp11-das -2 --sys=v1 v1.out      # First-Edition personality: 1972 syscalls, .. bias
pdp11-das -J --isa=bsd211 kern    # decode the late J11 hardware instructions
pdp11-das -s vmunix               # a stripped kernel, all-numeric content
```

Continue to the [design document](design.md) for the input shapes, the two
symbol-table formats, the recursive-descent code/data walk, and the round-trip
guarantee.
