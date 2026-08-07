# pdp11-das

The disassembler — the inverse of `as`, covering every era the assembler
covers: First Edition (1971) through V7 and 1/2.8/2.9/2.10/2.11BSD
objects, executables, archives, and kernels.

    pdp11-das [-a] [-p] [-s] [-6] [-2] [-y]
              [--sys=...] [--isa=...] [--std=...] file

- **`-a` produces reassemblable source.**  Not a listing: a `.s` file
  that our `as` assembles back to the original bytes — symbol-table
  index-order walking, numbered-local synthesis, `.comm`/`.globl`
  interleave pinning, nearest-label±offset anchors.  Corpus-proven at
  1130/1130 wide-corpus objects and 1665/1665 2.11 objects.
- **Stripped binaries too:** walks code from csv prologues
  and computed-jump case bodies when there is no symbol table.
- **String-table (2.11) and fixed-name symbol formats** are both native.

## Documentation

- [docs/fieldguide.md](docs/fieldguide.md) — the field guide to the
  reassembly engine and its edge cases.
- [docs/porting.md](docs/porting.md) — (historical) the porting guide of
  the original small das; the engine has grown well beyond it.

## Build and test

    make          # builds ../../bin/pdp11-das
    make check    # covered by tests/cc/das.sh at the repo root and the
                  # das-* oracle sweeps
