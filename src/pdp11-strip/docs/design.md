# pdp11-strip — design

This document describes how `pdp11-strip` removes the symbol table and
relocation from a PDP-11 `a.out`: the default header poke + truncating copy that
handles the ordinary magics, the overlay-aware copy for the auto-overlay
(`0430`/`0431`) form, the First Edition (`0405`) special case whose text size
includes the header, and the hardening around a hostile header. For how to *use*
it, see the [user guide](user-guide.md).

The tool is `strip.c` — the 2.9BSD `/bin/strip`, modernized to clean C99 — over
`../../common/cross/a.out.h`, the fixed-width on-disk view of the 16-bit header
(`struct exec`), the overlay header (`struct ovlhdr`), and the magic macros.

---

## 1. Stripping is a header poke, not a symbol walk

The default strip **never reads a symbol**. It copies the 16-byte header with
`a_syms` zeroed and the no-relocation bit set (`a_flag |= 1` — the header field is
commented "relocation info stripped"), then copies the text and initialized-data
image straight through, and stops. Everything that follows the data — the symbol
table and the relocation — is simply not copied, so it all falls off together.

Because the tail is discarded wholesale rather than parsed, one code path strips
the ordinary magics `0407` (normal), `0410` (read-only text), and `0411`
(separated I&D) identically: they differ only in load-time promises, not in what
sits after the image. An object that is already stripped — `ISSTRIPPED(x)` is
`a_syms == 0 && (a_flag & 1)` — is reported as "already stripped" and counts as an
error (see [§6](#6-for-a-maintainer) on the exit status), matching the era tool.

---

## 2. The auto-overlay header (`0430`/`0431`)

An auto-overlay object (`ISOVERLAID` = magic `0430` or `0431`) carries a `struct
ovlhdr` — `max_ovl` plus seven `ov_siz[]` words — immediately after the `exec`
header, describing several overlay text segments that sit *between* the root text
and the data. `N_TXTOFF` accounts for that extra header for these magics.

Strip reads the `ovlhdr`, re-emits it, and then copies `a_text + Σ ov_siz[] +
a_data` bytes. That `Σ ov_siz[]` term is the "overlay-aware truncation" the README
advertises: the overlay segments are preserved, and only the trailing symbol
table and relocation are dropped. Omitting the sum would truncate live overlay
code, so it is load-bearing.

---

## 3. First Edition `a.out` (`0405`)

Magic `0405` is treated as First Edition `a.out(V)`, a genuinely different layout:
a 6-word header **whose `a_text` includes the 12-byte header itself**, with the
symbol table and V1 relocation bits following the text. Strip rebuilds the header
as `{a_magic, a_text, 0, 0, a_syms, a_entry}` — carrying over the magic, text
size, symbol word, and entry, and zeroing the two size words that in this layout
record the following symbol-table and relocation-bits sizes — then seeks to offset
12 and copies `a_text - 12` bytes: the text image minus the header it has already
re-emitted. Everything after that (the symbols and relocation) is discarded.

Two guards frame this. A valid `0405` has `a_text >= 12`; a crafted `a_text < 12`
is rejected as "not in a.out format" **before** the copy — previously that value
drove the `a_text - 12` length negative and into `copy()`'s read/write loop, a
buffer overflow. And a `0405` whose `a_data` and `a_bss` are both zero has nothing
to strip and is reported "already stripped".

---

## 4. The `-n` inverse: keep only the name list

`-n` is the complement of a default strip. It writes a header with
`a_text`/`a_data`/`a_bss` zeroed and `a_syms` kept (the flag is *not* set), then
seeks past the text, data, and relocation — `N_TXTOFF + a_text + a_data`, plus the
overlay sizes for an auto-overlay object, plus `rellen` (the relocation length,
`a_text + a_data` unless the object is already flagged no-relocation, in which
case zero) — and copies `a_syms` bytes. The result is a bare header plus the
symbol table: text and data gone, name list kept.

---

## 5. Hardening and the write model

- **A fresh `mkstemp` temp per input file.** Each pass copies the literal
  `"/tmp/sXXXXXX"` into a writable buffer and calls `mkstemp`, which reserves an
  unpredictable name and creates it `0600`. This replaced the historical
  predictable name recreated with `creat()` every pass, so no attacker-planted
  name or symlink is followed for the temp.
- **`copy()` guards a negative length** — `if (size < 0)` prints "corrupt object
  (negative length)" and fails the file. This is belt-and-suspenders behind the
  `0405` `a_text < 12` pre-check: no length reaches the read/write loop negative.
- **Signals ignored for the run.** `SIGHUP`, `SIGINT`, and `SIGQUIT` are set to
  `SIG_IGN`, so an interrupt cannot leave a half-written file mid-rewrite.
- **Every read and write is length-checked** — a short read is "unexpected eof", a
  short write is `perror`'d; either aborts that file and counts an error.
- **Write-back.** With the stripped image in the temp, the original is reopened
  with `creat(name, 0666)` and the temp copied back over it, then the temp is
  unlinked. The `creat(0666)` means the file's mode is *reset* (subject to umask),
  not preserved — the authentic 2.9BSD behavior, and a deliberate point of
  difference from the VAX tool's rename-preserve.

---

## Testing

`tests/binutils/size_nm_strip.sh` (at the repo root) copies a committed real
2.9BSD object, strips the copy, and runs `pdp11-nm` on the result, requiring the
output to report "no name list" — the symbol table is provably gone and the
no-relocation flag set. The fuzz harness `tests/fuzz/run.sh` builds `pdp11-strip`
under ASan+UBSan and runs it over a corpus of malformed `a.out` objects on a
scratch copy, treating any sanitizer report or abort as a failure; the
negative-length and `0405` `a_text < 12` guards are exercised there. Separately,
rebuilding 2.9's `/bin/strip` with this toolchain against the era libc reproduces
the shipped binary byte-for-byte (`oracle/selfhost.sh`).

---

## For a maintainer

- **The default strip must stay format-agnostic.** It only rewrites
  `a_syms`/`a_flag` and copies text/overlay/data; it never reads an `nlist`. Do
  not "improve" it into walking the symbol table — that is what breaks the
  one-path-strips-all-magics property.
- **`0405` is First Edition, not the `0405` "overlay" the header comment names.**
  The `a_text >= 12` invariant is load-bearing; keep both the pre-check and the
  `copy()` negative guard.
- **The auto-overlay copy length is `a_text + Σ ov_siz[] + a_data`.** Touch overlay
  handling and drop the `ov_siz` sum and you truncate live overlay code.
- **The temp is `mkstemp`; the write-back is `creat` over the original.** Mode is
  not preserved. This matches the era — do not silently turn it into a
  mode-preserving rename without deciding to on purpose.
- **Exit status is the count of files that errored** (`exit(errs)`), and
  already-stripped counts as an error; a temp that cannot be created exits
  `EX_TEMPFAIL` (75).
