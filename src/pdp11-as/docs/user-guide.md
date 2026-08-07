# pdp11-as — user guide

`pdp11-as` assembles PDP-11 source in the 2BSD `as` syntax — the syntax the
compiler's `c1` emits and that hand-written kernel and library `.s` use — into a
PDP-11 object file. It is a host tool: it runs on your modern machine and writes
an object the rest of this toolchain (`ld`, `nm`, `ar`) reads. One binary spans
every era from First Edition UNIX (1972) to 2.11BSD; you pick the era with three
independent switches or a `--std` preset.

For how it works inside — the opcode tables, the two-pass span relaxation, the
object formats — see the [design document](design.md). For the era dialects in
depth, see [std.md](std.md).

---

## 1. Synopsis

```
pdp11-as [-o out.o] [-u] [-V] [-n] [-j] [-7]
         [--isa=…] [--sys=…] [--aout=…] [--std=…]
         file ...
```

Several input files are **concatenated** (separated by a newline) and assembled
as one unit — this is how the syscall stubs are built, `as -o x.o
/usr/include/sys.s x.s`. With no `-o`, the object is written to `a.out`.

---

## 2. Options

| option | meaning |
|---|---|
| `-o file` | write the object to `file` (default `a.out`) |
| `-u` | undefined symbols become **externals** for `ld` to resolve; overrides any `--std` strictness (`cc` always passed this, so it is the flagless default) |
| `-7` | **strict**: an undefined symbol never `.globl`'d stays local (type 0) — how every real `as` behaved when invoked bare |
| `-V` | **overlay** assembly (`ovas`): references to defined global text symbols stay external so `ld` builds overlay thunks |
| `-n` | write 2.11's string-table (`Newsym`) symbol format, 32-char names — same as `--aout=v2+` |
| `-j` | enable the **extended** hardware mnemonics (FIS/CIS/MED/XFC, FP11 DEC names) — same as `--isa=extended` |
| `--` | read the source from **standard input** (a lone `-` is accepted and ignored) |

Flags and files may be interleaved in any order. The single-letter flags are the
primitives; the `--` era axes below are presets over them.

---

## 3. Era axes and `--std` presets

Three orthogonal switches choose the era; a `--std` token sets a sanctioned
combination of all three (plus strictness). Mix them freely — an impossible mix
just assembles an impossible machine.

| axis | values | selects |
|---|---|---|
| `--isa=` | `v1` (=`v2`,`v3`,`1972`), `v4` (=`common`,`unix`, default), `bsd211` (=`newbsd`,`211`), `extended` | which **instructions** exist |
| `--sys=` | `none` (default), `v1` (=`1972`), `v6` | which **syscall names** are keywords (`sys write`) |
| `--aout=` | `v1` (=`405`), `v2` (=`407`, default), `v2+` (=`newsym`,`bsd211`) | the **object format** |
| `--std=` | `v1`,`v2`,`v3`,`v4`,`v5`,`v6`,`v7`,`bsd`,`newbsd`,`extended` (comma-composable) | an era preset over the three axes |

Notes worth calling out:

- Selecting a syscall list (`--sys=v1`/`v6`, or a `--std` that implies one) makes
  **`wait` the system call (7)**, not the WAIT instruction — write the bare word
  `1` for the instruction.
- `--std=v1` also switches the *output* to the First Edition format; the other
  `v2`/`v3` presets keep the ordinary object.
- Every `--std` token is **strict** by default (undefined non-`.globl` symbols
  stay local); add `-u` to restore automatic externals.
- `--isa=extended` (and `-j`) is opt-in for a reason: several extended mnemonics
  (`spl`, `mfpt`, `ldexp`, …) are also ordinary symbol names in historical
  source, and a keyword silently shadows the symbol. Do not assemble era sources
  with it on.

---

## 4. Output formats

The object format is chosen by `--aout=` (or the `-n` / `--std` shortcuts):

- **`v1`** — the **First Edition** a.out (magic **0405**, 12-byte header, the
  2-bit bit-stream relocation and V1 symbol flags). A file a simulated 1971
  system or apout can `exec()`; `das --std=v1 x | as --std=v1` round-trips the
  surviving V1-era binaries byte-for-byte.
- **`v2`** (default) — the classic **0407** object: 8-word header, one 16-bit
  relocation word per code word, and 12-byte symbol entries with inline 8-char
  names. This is what this toolchain's `ld` and `nm` read.
- **`v2+`** — the same 0407 object but with 2.11's **string-table** symbols and
  32-character names; required if 2.11's own `ld`/`nm` will consume the object.

All three place symbol values in the unified object address space (text, then
data, then bss); `ld` backs the bias out when it combines objects.

---

## 5. Exit status

- **0** — success.
- **1** — everything else: an unknown option or `--isa`/`--sys`/`--aout`/`--std`
  token, no input file, an unreadable input, out of memory, or **assembly
  errors**.

Note that on an assembly error `as` still writes a complete, relocated object
before returning nonzero — it never unlinks a partial `.o`, matching the native
2BSD assembler (so a build step that ignored the status could still consume a
broken object; check the status).

---

## 6. Examples

```
pdp11-as -o hello.o hello.s              # assemble one file to hello.o
pdp11-as -o x.o /usr/include/sys.s x.s   # concatenate the syscall stubs first
pdp11-as --std=v1 -o chown chown.s       # a runnable First Edition (1971) binary
pdp11-as --std=v6 crt.s                  # V4–V6 dialect: `sys signal`, `.ascii`
pdp11-as --std=newbsd -o m.o mch.s       # 2.11 object: mfpi/spl keywords, 32-char names
pdp11-as --isa=extended -o cis.o cis.s   # (or -j) the full hardware line
pdp11-as -u -o f.o f.s                   # undefined refs -> externals for ld
pdp11-as -- < prog.s                     # read source from standard input
```

Continue to the [design document](design.md) for the tables, the relaxation,
and the object formats; [std.md](std.md) covers the era dialects in full.
