# pdp11-ranlib

Builds the `__.SYMDEF` table of contents in an archive, so `ld` resolves
members regardless of order — the canonical V7/2.9 ranlib paired with our
`ar`.

    pdp11-ranlib archive ...

- **`__.SYMDEF` content matches the native ranlib** (run end-to-end under
  apsim) on all 26 ranlib'd libraries of the 2.9 tree — only the
  embedded timestamp is nondeterministic.
- **Middle-endian offsets:** the symbol-definition file's member offsets
  are PDP-11 longs, written through the same `PDPL()` swap `ar` and `ld`
  use.

## Documentation

- **[Design](docs/design.md)** — how the __.SYMDEF symbol directory is built and kept in step with the ar format.
- **[User guide](docs/user-guide.md)** — usage and when an archive needs a fresh ranlib.
- [docs/porting.md](docs/porting.md) — porting notes.

## Build and test

    make          # builds ../../bin/pdp11-ranlib
    make check    # covered by tests/ld/ranlib.sh and the lib-sweep oracle
