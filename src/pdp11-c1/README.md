# pdp11-c1

Ritchie cc pass 2: the PDP-11 code generator.  Reads c0's intermediate
stream and emits 2BSD assembler for `as`.

    pdp11-c1 tmp1 tmp2 out.s [-V]

- **The codegen table is authored, not hand-written.**  `table.s` is the
  authentic template source; the build runs it through `cvopt` (the 2BSD
  table compiler) and then `mktab` (a host helper) so the table compiles
  into the host binary instead of being assembled for the PDP-11.
- **Codegen-correctness fixes over the 2.8 port:** `geti()` sign-extends
  to 16 bits (negative `switch` cases had degraded to compare chains),
  long-constant halves are masked before the int-fold test (`-1L` now
  folds), and negative auto offsets print as 16-bit octal, matching the
  native listings.
- **DEC floating point, not host floating point.**  FP constants convert
  through `softfp.c` into exact 56-bit D-format — the last byte matches
  the 1981 compiler.

## Documentation

- [docs/porting.md](docs/porting.md) — the porting guide, including the
  cvopt/mktab table pipeline.

## Build and test

    make          # builds ../../bin/pdp11-c1 (regenerates table.c)
    make check    # covered by tests/c1/codegen.sh and the oracle corpus
                  # (oracle/run.sh: our c1 vs the 1981 c1 under apsim)
