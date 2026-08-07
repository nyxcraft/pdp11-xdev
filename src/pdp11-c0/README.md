# pdp11-c0

Ritchie cc pass 1: the parser / front end.  Reads preprocessed C, writes
the intermediate expression-tree stream that c1 turns into PDP-11 code.

    pdp11-c0 source.i tmp1 tmp2 [-P] [-V]

- **This is the genuine 2.9BSD `c0`** (near byte-identical upstream to
  2.8's), carrying the LP64 fixes the 2.8 port lacked: `cmst[]` moved out
  of `tree()`'s dead stack frame (the 2.8 port segfaults on the first
  large function), a `filename[]` overflow guard for long host paths, and
  correct subsp/strp carry on SEQNC nodes.
- **-P** emits profiling counters (`cc -p`); **-V** compiles for text
  overlays (`cc -V`).
- **16-bit target semantics on a 64-bit host:** the target's `int` stays
  2 bytes; only the compiler's own word assumptions were widened.

## Documentation

- **[Design](docs/design.md)** — the first pass -- the lexer, the expression trees, and the intermediate emission.
- **[User guide](docs/user-guide.md)** — how cc drives it and how to run it standalone.
- [docs/porting.md](docs/porting.md) — the porting guide: the anonymous-
  union tree nodes, pointer-in-int, and the stack-frame story.

## Build and test

    make          # builds ../../bin/pdp11-c0
    make check    # covered by tests/c0/ and the cc end-to-end suite at
                  # the repo root, plus the native-compiler oracle
                  # (oracle/run.sh) against the 1981 passes
