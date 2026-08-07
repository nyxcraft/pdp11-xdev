# pdp11-c2

Ritchie cc's peephole optimizer.  Reads c1's assembler output, rewrites
redundant sequences, and emits smaller equivalent assembler; `cc -O` runs
it.

    pdp11-c2 in.s out.s

- **Byte-for-byte faithful.**  The library built with `cc -O` reproduces
  native 2.9 libc members exactly — the optimizer's rewrites match the
  original's, verified across whole archives in the oracle sweeps.
- **Identical in both source trees:** 2.8 and 2.9 shipped the same c2;
  either copy is this one.

## Documentation

- **[Design](docs/design.md)** — the optional peephole optimizer and the transforms it applies.
- **[User guide](docs/user-guide.md)** — what -O turns on and how to run it.
- [docs/porting.md](docs/porting.md) — the porting guide.

## Build and test

    make          # builds ../../bin/pdp11-c2
    make check    # covered by tests/cc/optimizer.sh and the [-O] re-run
                  # of the correctness suite at the repo root
