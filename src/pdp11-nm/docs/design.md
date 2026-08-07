# pdp11-nm — design

This document describes how `pdp11-nm` lists an object's symbols: the 16-bit
PDP-11 a.out header it recognises, the three symbol-table dialects it decodes,
the type letter it assigns each symbol, and how it walks the old binary archive.
For how to *use* it, see the [user guide](user-guide.md).

The tool is one file, `nm.c` (~350 lines). It is a **host** program — built by
the modern host `cc` and run on your LP64 Linux box — that reads little-endian
16-bit PDP-11 objects. There is no shared reader class: `nm.c` freads the
on-disk structs directly, using the width-corrected definitions of `struct exec`,
`struct nlist`, and `struct ar_hdr` from `../../common/cross/a.out.h` and
`../../common/cross/ar.h` (pulled in with `-Icross`) so K&R-era source reads the
right byte layout on a 64-bit machine (§7).

---

## 1. One file, three on-disk structs

`nm` touches exactly three structures, all fixed-width in the `cross/` headers:

| struct | on disk | fields of interest |
|---|---|---|
| `exec` | 8 words / **16 bytes** | `a_magic`, segment sizes, `a_syms`, `a_flag` |
| `nlist` | **12 bytes** | `char n_name[8]`, `int16_t n_type`, `uint16_t n_value` |
| `ar_hdr` | 26 bytes, packed | `ar_name[14]`, `ar_date`, `ar_size` |

The whole program is: read the header, decide where the symbol table starts and
how many entries it holds, read each 12-byte `nlist`, map it to a type letter,
filter, sort, and print. Everything hard is in "decide where it starts" (the
magics and the First Edition case, §2–3) and "map it to a letter" (§4).

---

## 2. The a.out header and the magic numbers

The first word is the magic number; `N_BADMAG` rejects anything that is not one
of the six PDP-11 magics before `nm` reads further:

| macro | octal | meaning | symbol table located by |
|---|---|---|---|
| `A_MAGIC1` | `0407` | normal (writable text) | header + text + data (+ reloc) |
| `A_MAGIC2` | `0410` | read-only (pure) text | same |
| `A_MAGIC3` | `0411` | separated I&D | same |
| `A_MAGIC4` | `0405` | First Edition a.out(V) | its own path (§3.2) |
| `A_MAGIC5` | `0430` | auto-overlay, non-separate | same as normal |
| `A_MAGIC6` | `0431` | auto-overlay, separate | same |

For the normal magics the symbol table follows the text, the data, and — unless
the relocation-stripped bit (`a_flag & 01`) is set — an equal-sized block of
relocation info. `nm` computes that offset inline (`o = a_text + a_data`, doubled
when reloc is present, seeked from just past the 16-byte header) and reads
`n = a_syms / 12` entries. The `a.out.h` comment still calls `0405` "overlay"
(its later-BSD name), but the tools decode it as the First Edition layout — the
reading their in-code comments document and the test objects need.

---

## 3. The symbol table and its dialects

### 3.1 The 2.9BSD form (the default build)

A `struct nlist` is a fixed 8-byte name, a 16-bit `n_type`, and a 16-bit
`n_value` — 12 bytes. This is what the shipped `pdp11-nm` reads: one 16-bit
`n_type` per symbol, no per-symbol overlay byte (2.9BSD keeps overlays out of the
symbol table — they live in a `struct ovlhdr` on the `0430`/`0431` image).

### 3.2 First Edition (`0405`)

The Research V1 `a.out(V)` layout needs its own branch. Its 6-word header is
*included in* `a_text`, so the symbol table (still 12-byte entries) begins at file
offset `a_text`, its byte size is in `a_data`, and the `a_syms` slot holds the
bss instead. `nm` seeks **relative** to the just-read header so the same code
works for an archive member:

```c
fseek(fi, (long)(unsigned short)exph.a_text - (long)sizeof(struct exec), 1);
n = (unsigned short)exph.a_data / sizeof(struct nlist);
```

The V1 type byte uses an older encoding — `00` undef, `01` abs, `02` register,
`03` relocatable, `|040` global — which `nm` translates into the later
`N_UNDF`/`N_ABS`/`N_REG`/`N_TEXT` + `N_EXT` values so the one type-letter switch
(§4) handles every era.

### 3.3 The MENLO_OVLY variant (not in the default build)

`nm.c` also carries a `#ifdef MENLO_OVLY` variant that splits the middle word
into a 1-byte `nn_type` + 1-byte `nn_ovno` (the 2.8BSD manual-text-overlay form)
and prints the overlay number after the name. That macro is **not** defined when
`nm.c` compiles — it includes `<sys/param.h>`, not `<whoami.h>` — so that code is
inert unless someone builds with `-DMENLO_OVLY`. The shipped tool is the plain
2.9BSD reader of §3.1.

---

## 4. The type letter

Each symbol's letter comes from a switch on `n_type & N_TYPE`, lower case first,
then upper-cased if the `N_EXT` (global) bit is set:

| `n_type & 037` | letter | meaning |
|---|---|---|
| `N_UNDF` (0), value 0 | `u` / `U` | undefined reference |
| `N_UNDF` (0), value ≠ 0 | `c` / `C` | common (uninitialised global) |
| `N_ABS` (01) / default | `a` / `A` | absolute |
| `N_TEXT` (02) | `t` / `T` | text (code) |
| `N_DATA` (03) | `d` / `D` | data |
| `N_BSS` (04) | `b` / `B` | bss |
| `N_REG` (024) | `r` / `R` | register name |
| `N_FN` (037) | `f` / `F` | file-name symbol |

Two PDP-11 specifics. **`N_TYPE` is `037`** — it keeps all five low bits — so
`N_FN` (`037`) survives the mask and reaches its `case` cleanly; there is no
"file symbol falls through the type mask" hazard here. And **common is decided by
the value, not a distinct type**: an `N_UNDF` entry with a non-zero `n_value` is a
common block (`C`), matching the era's linker convention.

---

## 5. Filtering, sorting, and the output line

Filtering happens as symbols are read: `-g` drops any symbol without the `N_EXT`
bit; `-u` drops any whose letter is not `u`. Survivors are collected into a
`realloc`-grown array and sorted with `qsort(compare)` unless `-p` is given.
`compare` sorts by the 8-byte name by default and by `n_value` first under `-n`;
a single `revsort_flg` (1, or −1 under `-r`) is the return multiplier, so `-r`
reverses whichever sort is active.

Each line is `value type name`:

- the **value** is 6-digit zero-padded **octal** (`FORMAT` is `"%06o"`) — PDP-11
  `nm` has always printed octal, and six octal digits is exactly the width of a
  16-bit `n_value`;
- an **undefined** symbol (`u`/`U`) prints **six blanks** instead of a value;
- with **`-u`** the value and letter are suppressed and only the name is printed
  — a bare list of unresolved references.

---

## 6. Archives

An archive is recognised by its first word, `ARMAG` (`0177545`). That comparison
is the one genuine host bug this port fixes: `ARMAG` has bit 15 set and
`a_magic` is a signed `int16_t`, so on an LP64 host it sign-extends to a negative
`int` and would never equal the positive `int` constant `ARMAG`. `nm` compares
**as `unsigned short`** on both sides (`(unsigned short)exph.a_magic ==
(unsigned short)ARMAG`) so the match holds.

`nextel()` walks the members: it seeks to the running `off`, reads a 26-byte
`ar_hdr`, byte-swaps `ar_size`/`ar_date` with `PDPL` (the PDP-11 stores a `long`
high-word-first — "middle-endian", §7), rounds an odd member size up to the even
boundary, and advances `off`. Each member is then read as an `exec`; one that
fails `N_BADMAG` — including the `__.SYMDEF` ranlib index — is silently skipped,
so there is no name-based special case for the directory member.

Member names are the other hardening. `ar_name[14]` is **not** NUL-terminated
when a name fills all 14 bytes, so a raw `%s` would over-read into the following
`ar_date`. The `nmname()` helper copies the 14 bytes into a 15-byte static
buffer and terminates it, and every place that prints or sorts a member name goes
through it (plain file names, already C strings, pass straight through).

---

## 7. Reading 16-bit structs on a 64-bit host

`nm` runs on the host but reads objects written for a machine where `int` is
2 bytes and `long` is 4:

- **Fixed widths.** `cross/a.out.h` and `cross/ar.h` spell every on-disk field
  with `int16_t`/`uint16_t`/`int32_t`, so `struct exec` is 16 bytes and
  `struct nlist` 12 bytes on the host — no field silently widens to the host's
  `int`.
- **No byte swap for a.out.** PDP-11 and x86-64 are both little-endian, so the
  16-bit fields need only the right width.
- **Middle-endian longs and packing for `ar`.** A PDP-11 `long` stores its high
  half first, so `ar_date`/`ar_size` cross the disk boundary through `PDPL` (the
  swap is its own inverse), and `struct ar_hdr` is `__attribute__((packed))` so
  the host inserts no padding before its 4-byte fields.

Built `-std=c99 -D_POSIX_C_SOURCE=200809L` with the tree's correctness flags
(`-fno-strict-aliasing` for the on-disk word type-punning, `-fwrapv`, `-fcommon`);
compiles warning-free with no `-Wno-*` suppressions.

---

## Testing

`tests/binutils/size_nm_strip.sh` (from the repo root) runs `nm` against a
committed real 2.9BSD kernel object, `tests/fixtures/dsort.o`, and checks that it
decodes the 39-entry symbol table and emits the first line in the `value type
name` octal shape. Because the source is the authentic 2.8/2.9 tool, where it
disagrees with GNU `pdp11-aout` `nm` on an old object (GNU reports "no symbols"
on some), *ours* is the reference; the two agree on objects GNU can read (see
`docs/binutils-porting.md`).

---

## For a maintainer

- **The magic table lives in `cross/a.out.h`** (`A_MAGIC1..6`, `N_BADMAG`) — add a
  format there, not in `nm.c`.
- **The symbol offset is computed inline** (header + text + data + reloc), *not*
  via `N_TXTOFF`. Making the default build handle `0430`/`0431` overlay symbol
  tables means adding the `struct ovlhdr` skip to that inline math — today it
  exists only on the `MENLO_OVLY` path.
- **The type letter is one switch**; a new type is a new `case`, and remember
  common is `N_UNDF` with a value, not its own type.
- **Keep member names going through `nmname()`**, and keep the `ARMAG` compare
  `unsigned short` on both sides — a signed compare silently stops recognising
  archives on an LP64 host.
