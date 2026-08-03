# pdp11-cc

The compiler driver: runs `cpp → c0 → c1 [→ c2 with -O] → as → ld` to turn
C into a runnable PDP-11 `a.out`.  This is the authentic 2.9BSD `cc`,
extended with universe selection.

    pdp11-cc [--universe=U | -u U] [-c|-S|-E|-P] [-O] [-o out] [-v]
             [-D -I -U -C options] [-V overlay mode] file.c ... [-lx ...]

- **One driver, every era.**  `--universe` (or `$PDP11_UNIVERSE`, default
  `bsd29`) is validated against `src/common/universes.tsv` and exported to
  the child passes, so cpp reads `include/<universe>/` and ld links
  `lib/<universe>/{crt0.o, libc.a}` without flags of their own.
- **No compiled-in paths.**  The passes are resolved relative to the
  driver's own location: a `<prefix>-cc` runs its sibling `<prefix>-cpp`,
  `<prefix>-c0`, ... — the prefix is everything through the last `-` in
  `argv[0]`, so the whole toolchain relocates as a unit.
- **The overlay pipeline is built in.**  `-V` compiles for text overlays
  (`-DC_OVERLAY`, `as -V` ovas mode, `-lovc`), matching 2.9's MENLO_OVLY
  toolchain; there is no separate ovcc/ovas/ovld.
- **Host-porting fixes over the vintage source:** `$TMPDIR` honoured for
  pass temporaries, and the `-E`/`-P` null-temp crash fixed.

`cc` accepts K&R / pre-1977 C (the C the 2.9BSD compiler itself is written
in): old-style definitions, implicit int, and so on.

## Documentation

- [docs/porting.md](docs/porting.md) — the LP64 porting guide inherited
  from the source project.

## Build and test

    make          # (repo root or this dir) builds ../../bin/pdp11-cc
    make check    # covered by the end-to-end suite (tests/cc/*.sh at the
                  # repo root): compile/link/run under apsim, both
                  # universes, plain and -O
