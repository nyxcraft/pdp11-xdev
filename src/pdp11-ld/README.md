# pdp11-ld

The link editor — the authentic 2.9BSD `ld.c` on an LP64 host.  Links
classic 2BSD a.out objects and archives into 0407/0410/0411 executables
and 0430 auto-overlay images.

    pdp11-ld [-o a.out] [-X|-x] [-r] [-s] [-n] [-i] [-d] [-t]
             [-Z ovl.o ... -Z ... -L] [-u sym] [-e entry] files... [-lx]

- **Overlays are not an afterthought.**  The MENLO overlay machinery is
  unconditional in the 2.9 source (`-Z` brackets overlay segments, `-L`
  ends them, NOVL=7), and the `roundov()` LP64 infinite-loop fix here is
  what makes overlay links terminate.  Proven by linking the GENERIC
  2.9 kernel: `unix` comes out byte-identical to the native link.
- **Era libraries without flags.**  `-lx` resolves against
  `../lib/<universe>/libx.a` relative to the binary
  (`$PDP11_UNIVERSE`, default `bsd29`), falling back to a flat `../lib`.
- **Middle-endian faithful.**  Archive dates/sizes and `__.SYMDEF`
  offsets pass through the `PDPL()` PDP-11 long swap, so what ld reads is
  exactly what native tools wrote.

## Documentation

- **[Design](docs/design.md)** — the two-pass link model, segment and overlay layout, and relocation resolution.
- **[User guide](docs/user-guide.md)** — options, overlay control, library search, and examples.
- [docs/porting.md](docs/porting.md) — the 16-bit-I/O porting story
  (every on-disk word pinned to uint16_t).

## Build and test

    make          # builds ../../bin/pdp11-ld
    make check    # covered by tests/ld/*.sh at the repo root and the
                  # ld-sweep / kernel-link oracles (213 clean links, 0
                  # differ; byte-identical overlay kernel)
