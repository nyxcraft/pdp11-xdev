# pdp11-objcopy — design

This document describes how `pdp11-objcopy` copies a PDP-11 `a.out` and extracts
its loadable image or a single segment: the format-agnostic identity copy, the
contiguous-layout slicing the extractions need, the formats it refuses to slice,
and the hostile-header bounds check that frames every write. For how to *use* it,
see the [user guide](user-guide.md).

The tool is `objcopy.c` over `../../common/cross/a.out.h`, the fixed-width on-disk
view of the 16-bit header and the magic macros.

---

## 1. An `a.out`-only world: copy or extract, never convert

PDP-11 is an `a.out`-only target — there is no ELF the modern binutils understand,
so, unlike the VAX tree's `objcopy`, there is **no format conversion here**. The
useful operations are an identity copy and pulling raw bytes out of the loadable
image; removing the symbol table and relocation is
[`pdp11-strip`](../../pdp11-strip/docs/user-guide.md)'s job. `-O` accepts only
`binary`, and `-j` only `text` or `data`; anything else is a usage error.

---

## 2. The identity copy is format-agnostic

With no `-O` and no `-j`, `objcopy` slurps the whole input, checks only that it is
plausibly an `a.out` — at least a 16-byte header, and `N_BADMAG` false — and writes
every byte to the output. Because it copies rather than interprets, the identity
copy handles **any** magic, including the First Edition (`0405`) and auto-overlay
(`0430`/`0431`) forms that the extractions refuse. Both an input and an output path
are required; there is no in-place default.

---

## 3. Extraction needs the contiguous layout

`-O binary` and `-j` rely on the `0407`/`0410`/`0411` layout, in which the text and
initialized data are contiguous on disk immediately after the 16-byte header. With
`txtoff = sizeof(struct exec)`:

- **`-O binary`** writes `buf[txtoff .. txtoff + a_text + a_data]` — the loadable
  image, text followed by initialized data, with **no header and no symbol table**:
  exactly the bytes the loader maps.
- **`-j text`** writes `[txtoff, +a_text]`, the text segment raw.
- **`-j data`** writes `[txtoff + a_text, +a_data]`, the initialized-data segment
  raw.

If both `-O binary` and `-j` are given, `-j` selects the slice.

---

## 4. Refusing what it cannot slice correctly

The First Edition (`0405`) and auto-overlay (`0430`/`0431`) magics have a different
on-disk shape — a header-inclusive text size, or several overlay text segments
described by an overlay header between text and data — so their text and data are
not the simple contiguous run the slicing assumes. Extraction from them is
**refused** ("extraction unsupported for First Edition / overlay a.out") rather
than silently producing the wrong bytes. The identity copy still carries those
objects losslessly, because it copies bytes without interpreting the layout.

---

## 5. Hostile-input hardening

The header's `a_text` and `a_data` are attacker-controlled. Before any slice is
written, `objcopy` validates `txtoff + a_text + a_data > len` (plus the redundant
`a_text < 0` / `a_data < 0`) against `len`, the size actually read from disk — so a
header claiming a 64 KB text inside a 40-byte file is refused, not read out of
bounds. The slurp itself is a checked read loop; a stat failure, a `malloc`
failure, or a short read each `die` cleanly rather than proceeding on garbage.

---

## 6. The write model

`slurp` reads the entire input into memory before anything is written, so the
input and output paths may even be the same file. Output is written directly with
`fopen`/`fwrite`/`fclose`, each checked. There is no temp-file-and-rename dance
because `objcopy` is not an in-place rewriter — it always writes a named output,
and stripping in place is `pdp11-strip`'s role, not this tool's.

---

## Testing

`tests/run.sh` is hermetic: it builds a synthetic `0407` object (`a_text = 4`,
`a_data = 6`, `a_syms = 12`) with recognizable bytes and checks that the identity
copy is byte-identical, that `-O binary` equals the text+data image, and that `-j
text`/`-j data` equal the segments — each compared against slices computed
independently in the script. It then checks the guards: a header with `a_text =
0xffff` is refused with exit 1, and a non-`a.out` is refused. Finally a committed
real 2.9BSD kernel object (`dkleave.o`) round-trips through an identity copy. The
object-parser ASan+UBSan fuzzer (`tests/fuzz/run.sh`) additionally runs both the
plain copy and `-O binary` over a corpus of malformed `a.out` files, where the
size-versus-length bounds check is exercised.

---

## For a maintainer

- **No ELF, and no `a.out`-flavour conversion** — this is deliberately not the VAX
  `objcopy`. `-O` accepts only `binary`; `-j` only `text`/`data`.
- **Every slice must be bounds-checked against the slurped length before emit.**
  That single check is the security boundary; keep it ahead of any `emit`.
- **Extraction is refused for `0405`/`0430`/`0431` on purpose.** Do not add a
  "best effort" slice for them — the identity copy already carries those bytes
  losslessly.
- **Output is written directly (not atomically), and both paths are required.**
  The whole-file slurp is what makes `in == out` safe; keep the read-before-write
  order.
- **Exit is 0 or 1** — every error goes through `die()` → `exit(1)`, including
  usage errors; there is no separate usage exit code.
