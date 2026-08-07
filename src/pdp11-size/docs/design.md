# pdp11-size — design

This document describes how `pdp11-size` reports the segment sizes of a PDP-11
a.out file: the 16-bit header it reads, how it turns three segment words into the
`text data bss dec oct` line, the two special cases (First Edition and
auto-overlays), and what makes it safe on a 64-bit host. For how to *use* it, see
the [user guide](user-guide.md).

The tool is one small file, `size.c` (~80 lines, revision 2.5). It is a **host**
program — built by the modern host `cc`, run on LP64 Linux — that reads
little-endian 16-bit PDP-11 objects. It reads only the header (and, for overlay
images, the overlay header that follows it); it never touches the text, data, or
symbol table. The width-corrected `struct exec` and `struct ovlhdr` come from
`../../common/cross/a.out.h`, pulled in with `-Icross` (§6).

---

## 1. One struct, one pass

`size` reads a single `struct exec` — 8 words / **16 bytes** — from the front of
each file, and that is nearly all it needs. From the magic number it decides
which of three interpretations of the header applies, adds up the segments, and
prints one line. There is no symbol-table walk and no seeking (except to read the
overlay header of an auto-overlay image), so the whole tool is a `fread`, a
`switch` on the magic, and two `printf`s.

The column header is printed once, before the file loop:

```
text	data	bss	dec	oct
```

and each object adds a tab-separated line whose separators are literal `+` and
`=` signs, ending in the file name.

---

## 2. The header and the magic check

Before trusting any field, `size` validates the magic number with `N_BADMAG`
(from `a.out.h`), which accepts exactly the six PDP-11 magics and rejects
everything else as `not in object file format`:

| macro | octal | meaning | size path |
|---|---|---|---|
| `A_MAGIC1` | `0407` | normal (writable text) | normal columns (§3) |
| `A_MAGIC2` | `0410` | read-only (pure) text | normal columns |
| `A_MAGIC3` | `0411` | separated I&D | normal columns |
| `A_MAGIC4` | `0405` | First Edition a.out(V) | counted-in header (§4) |
| `A_MAGIC5` | `0430` | auto-overlay, non-separate | normal columns + overlay line (§5) |
| `A_MAGIC6` | `0431` | auto-overlay, separate | normal columns + overlay line |

A short read (fewer than 16 bytes) is treated the same as a bad magic: `not in
object file format`, then on to the next file.

---

## 3. The size report

For the normal magics the three segment words are printed as they stand and
summed:

```c
printf("%u +\t%u +\t%u =\t", buf.a_text, buf.a_data, buf.a_bss);
sum = (long)buf.a_text + (long)buf.a_data + (long)buf.a_bss;
printf("%ld =\t%lo", sum, sum);        /* decimal sum, then octal sum */
printf("\t%s\n", *argv);
```

So the five columns are **text**, **data**, **bss**, the **decimal** total
(`dec`), and the **octal** total (`oct`) — the octal column is why the header
names it `oct`. The sum is accumulated in a host `long` (`a_text + a_data +
a_bss` cannot overflow 16 bits collectively, but the `long` keeps the `dec`/`oct`
formatting honest).

---

## 4. First Edition (`0405`) — the counted-in header

The Research V1 `a.out(V)` header is laid out differently, and `size` handles it
as a distinct case rather than misreading its fields:

- the 6-word (12-byte) header is **included in** `a_text`, so the real text size
  is `a_text - 12`;
- there is **no data segment** (the printed data column is `0`);
- the word in the `a_syms` slot is the data area — the era's **bss**.

```c
unsigned t = buf.a_text - 12, b = buf.a_syms;   /* text sans header; bss */
printf("%u +\t%u +\t%u =\t", t, 0, b);
sum = (long)t + (long)b;
```

This keeps a V1 binary from being reported with a 12-byte-too-large text and a
zero bss.

---

## 5. Overlays (`0430`/`0431`)

`size.c` includes `<whoami.h>`, which pulls in `<sys/localopts.h>`, which defines
`MENLO_OVLY`. So — unlike `pdp11-nm`, whose default build leaves that macro
undefined — the overlay-reporting code **is** compiled into the shipped
`pdp11-size`. For an auto-overlay image it reads the `struct ovlhdr` that follows
the exec header, sums the non-zero overlay sizes into the base text to get the
true core footprint, and prints a second line:

```c
coresize = buf.a_text;
for (i = 0; i < NOVL; i++)
    if (ovlbuf.ov_siz[i]) coresize += ovlbuf.ov_siz[i];
printf("%ld total text, overlays: (", coresize);
/* ... comma-separated non-zero ov_siz[] ... */
```

`struct ovlhdr` is `int16_t max_ovl` + `uint16_t ov_siz[NOVL]` (`NOVL` = 7),
16 bytes, read straight after the 16-byte exec. Only overlays with a non-zero
size are listed.

---

## 6. Reading 16-bit structs on a 64-bit host

`size` reads objects written for a 16-bit machine while running on an LP64 one.
Two things make that correct:

- **Fixed widths.** `cross/a.out.h` spells every header field with
  `int16_t`/`uint16_t`, so `struct exec` is 16 bytes and `struct ovlhdr` is
  16 bytes on the host, exactly as on the PDP-11 — no field silently widens to
  the host's 4-byte `int`.
- **No byte swap.** PDP-11 and x86-64 are both little-endian, so the 16-bit
  fields need only the right width. (`size` reads no `long` from disk, so the
  middle-endian `PDPL` swap that `ar`/`nm` need does not arise here.)

It is built `-std=c99 -D_POSIX_C_SOURCE=200809L` with the tree's correctness
flags (`-fno-strict-aliasing`, `-fwrapv`, `-fcommon`) and compiles warning-free
with no `-Wno-*` suppressions.

---

## Testing

`tests/binutils/size_nm_strip.sh` (run from the repo root) checks `size` against
a committed real 2.9BSD kernel object, `tests/fixtures/dsort.o` (a pure-text
object): text `158`, data `0`, bss `0`. Those numbers are cross-checked against
the GNU oracle `pdp11-aout-size`, which `size` is required to match exactly — see
`docs/binutils-porting.md`.

---

## For a maintainer

- **The magic table and `N_BADMAG` live in `cross/a.out.h`.** Add or reject a
  format there; `size.c` only branches on `a_magic` for the two special layouts.
- **`0405` is the odd one out** — its header is counted inside `a_text` and it
  has no data segment. Do not "simplify" it back into the normal three-word path.
- **Overlay reporting is real in this build**, because `<whoami.h>` defines
  `MENLO_OVLY`. If that include or that macro goes away, the second `total text,
  overlays:` line silently disappears — the tests only assert the first line, so
  watch for it.
- **`size` reads only the header** (plus `ovlhdr`). Keep it that way; segment
  sizes are a header fact, and reading further is how a size tool grows bugs.
