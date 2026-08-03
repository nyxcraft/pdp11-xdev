# pdp11-cpp

The C preprocessor — the pristine 2.9BSD `cpp` (Reiser lineage), built
with `FLEXNAMES` so macro names go to 128 characters like the native 2.9
binary.

    pdp11-cpp [-P] [-E] [-R] [-Dname[=val]] [-Uname] [-Idir] [-C] [in [out]]

- **Two fixes the 2.8 revision lacks:** the `defined(NAME)` /
  `flslvl` state leak is gone (the 2.8 behaviour is preserved as a golden
  record in `tests/cpp-bsd28/`), and `#include <...>` is scanned with
  macro expansion off.
- **Era headers without flags.**  The default system include directory is
  `../include/<universe>/` resolved relative to the binary
  (`$PDP11_UNIVERSE`, default `bsd29`), with a flat `../include/` as
  fallback — the same relocatable scheme cc and ld use.
- **Faithful output.**  FLEXNAMES only widens cpp's internal name table;
  the emitted text is unchanged, which is what lets downstream byte-match
  oracles work.

## Documentation

- [docs/porting.md](docs/porting.md) — the LP64 porting guide (K&R
  varargs, the yacc'd `#if` grammar, table sizes).

## Build and test

    make          # builds ../../bin/pdp11-cpp (yacc generates cpy.c)
    make check    # covered by the cpp golden tests in tests/cpp/ at the
                  # repo root (run.sh -u regenerates the .expected files)
