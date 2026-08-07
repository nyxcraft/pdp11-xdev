# Porting guide: nm, size, strip

The three single-file binary utilities. These are the easiest ports — each
is one self-contained C file that reads (and, for `strip`, rewrites) a
PDP-11 a.out using the [`cross/` headers](cross-headers.md).

| Tool | Source | Function |
|------|--------|----------|
| `nm`   | `nm/nm.c`     | list the symbol table |
| `size` | `size/size.c` | report text/data/bss sizes |
| `strip`| `strip/strip.c` | remove the symbol table and relocation |

## Build

```make
${HOSTCC} ${O} ${COMPAT} -Icross -o ${BIN}/${PREFIX}-nm    nm/nm.c
${HOSTCC} ${O} ${COMPAT} -Icross -o ${BIN}/${PREFIX}-size  size/size.c
${HOSTCC} ${O} ${COMPAT} -Icross -o ${BIN}/${PREFIX}-strip strip/strip.c
```

`-Icross` makes them use the fixed-width on-disk structs.  `COMPAT` is now
`-std=c99 -D_POSIX_C_SOURCE=200809L` plus the three correctness flags
(`-fno-strict-aliasing`, `-fwrapv`, `-fcommon`) — the host tools were
modernized to clean C99 (ANSI prototypes, explicit types, no BSD types),
and every function they call is ISO C99 or POSIX, taken from its standard
header (`_POSIX_C_SOURCE` requests the POSIX.1-2008 standard so `-std=c99`
does not hide them).  No warning suppressions, no hand-written POSIX
prototypes.  (The target C library stays K&R; it is compiled by our own
`cc`, not the host compiler.)

## Porting fixes

These tools are recent enough (not the pre-1977 compiler) that the only
real change is one portability bug in `strip`:

- **`mktemp` on a string literal.** `strip` called
  `mktemp("/tmp/sXXXXX")`, and `mktemp` rewrites its argument in place.
  On the PDP-11, program text was writable, so this worked. On Linux a
  string literal lives in read-only `.rodata` and the write segfaults.
  Fix: give it a writable buffer with **six** trailing `X`s (the original
  used five) and switch to `mkstemp`, which creates *and* opens the temp
  file 0600 and returns an fd:

  ```c
  char tnamebuf[] = "/tmp/sXXXXXX";   /* writable, 6 X's */
  ...
  int fd = mkstemp(tnamebuf);         /* reserves the name; creates it 0600 */
  ```

`nm` and `size` needed no source changes beyond the `cross/` headers.

## A note on `nm` and the GNU oracle

`nm` decodes 2.8BSD-format symbol tables that the modern GNU `pdp11-aout`
binutils cannot read (it reports "no symbols" on some old objects). That is
expected: our `nm` is the authentic 2.8BSD tool, so where the two disagree
on an old object, ours is the reference. They do agree on objects GNU can
read, and `size` matches the GNU oracle exactly.

## Verification

`tests/binutils/size_nm_strip.sh` runs all three against a committed real
2.8BSD kernel object, `tests/fixtures/dkleave.o`:

- `size` reports the exact segment sizes (cross-checked against
  `pdp11-aout-size`).
- `nm` decodes the 16-entry symbol table and prints values in 6-digit
  octal.
- `strip` round-trips: the symbol table is gone and the no-relocation flag
  is set afterward.

## Deferred

`ar` (the archive manager) and `ld` (the link editor) are **not** simple
ports and are documented/handled separately:

- `ar` is the archaic V7 binary-archive format (inline struct, raw inode
  `stbuf`); it is only needed to build `libc.a` and is folded into the libc
  stage.
- `ld` is a major port: its whole I/O model treats `int` arrays as buffers
  of 16-bit PDP-11 words, so every machine-word `int` needs converting. It
  is sequenced after the assembler so it can be verified end to end.
