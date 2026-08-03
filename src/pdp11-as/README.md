# pdp11-as

The PDP-11 assembler — a C reimplementation of the 2BSD `as` (which is
itself written in PDP-11 assembly), historically parameterized from First
Edition UNIX (1972) to 2.11BSD.

    pdp11-as [-o out.o] [-u] [-V] [-n] [-j] [-7]
             [--isa=v1|v4|bsd211|extended] [--sys=none|v1|v6]
             [--aout=v1|v2|v2+] [--std=v1,...,v7,bsd,newbsd,extended] file

- **Three orthogonal era axes.**  `--isa` selects which instructions
  exist, `--sys` which syscall keywords, `--aout` the object format —
  from the First Edition 12-byte header with bit-stream relocation,
  through classic 16-byte 0407, to 2.11's string-table symbols
  (`--aout=v2+`, 32-char names).  `--std` tokens are presets over the
  axes; see [docs/std.md](docs/std.md).
- **Corpus-proven authenticity.**  A dozen fixes were found by sweeping
  entire vintage source trees and byte-comparing against the native
  assembler under apsim: `..` relocation, `.if`-false interning, span
  off-by-one, 8-bit `sys` trap codes, and more.
- **`-V` is ovas.**  Overlay assembly (undefined non-`.globl` text refs
  left for `ld` to resolve into thunks) is a mode of this one binary, as
  it was in 2.9's MENLO toolchain.
- **Self-hosting, to the byte:** assembling 2.9's own `as0.s`/`as2.s`
  reproduces the native `/bin/as` and `/lib/as2` exactly
  (`oracle/selfhost.sh`).

## Documentation

- [docs/std.md](docs/std.md) — the era-axis user guide.
- [docs/porting.md](docs/porting.md) — design and porting notes.

## Build and test

    make          # builds ../../bin/pdp11-as
    make check    # covered by tests/as/encode.sh at the repo root, the
                  # apsim CIS golden, and the as-corpus/tree-sweep oracles
