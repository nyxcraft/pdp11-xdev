# Porting guide: ranlib (+ ld __.SYMDEF)

`ranlib` adds a `__.SYMDEF` table of contents to an archive so `ld` resolves
members regardless of order (it otherwise scans a plain archive single-pass,
requiring dependency ordering).  The canonical V7/2.9 ranlib (identical across
the line) -- the matched partner of the canonical [ar](ar.md).

## How it works

It scans each member's a.out symbol table, records (symbol -> member byte
offset) in a `struct tab { char name[8]; long offset; }` array, writes that as
a temp file `__.SYMDEF`, and runs `ar rlb <firstmember> <archive> __.SYMDEF`
to insert it as the archive's first member; the offsets are shifted by the
inserted member's size.  `ld`'s reader does `tnum = symdef_size/12` and
`step(offset>>1)` (byte -> word) to pull each defining member.

## Porting fixes

- **`struct tab` packed to 12 bytes** (`int32` offset, not host `long`=8) so
  it matches ld's on-disk reader and `sizeof` arithmetic.  ld's own
  `struct tab` got the same packing.
- **Magic compare unsigned** (`(unsigned short)a_magic != ARMAG`): `a_magic`
  is `int16_t`, so `0177545` reads negative.
- **4-byte date write** in `fixdate` (not host `sizeof(long)=8`), set to a
  far-future value so ld never treats the table as stale (host filesystem
  clock skew would otherwise force the single-pass fallback).
- **`ar` resolved via `/proc/self/exe`** so `ar rlb` invokes this toolchain's
  prefixed ar (same scheme as cc/ld), not a system `ar`.
- **ld `__.SYMDEF` path**: `ldrand` dereferenced `*pp` (a hash slot that is
  NULL for an unreferenced symdef symbol) -- benign on the PDP-11 (address 0
  readable), a segfault on the host; guarded with a NULL check.

`make ranlib-tool`; `make libc` runs it on `libc.a`.
