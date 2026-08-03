# pdp11-ar

The archiver — authentic 2BSD `ar`, producing archives byte-identical in
layout to what the native tool wrote.

    pdp11-ar key [posname] afile name ...
       keys: d r q t p m x  + v c u a b i l

- **The on-disk format is the point.**  The V7 26-byte member header and
  0177545 magic assume 16-bit ints; here they are pinned to packed fixed
  widths, and `ar_date`/`ar_size` go through the `PDPL()` middle-endian
  swap — PDP-11 longs are stored high-word-first.
- **Oracle-proven:** rebuilding every archive in the 2.9BSD tree matches
  the native `ar` (run under apsim) 39/39, zero diffs.
- **Member order is honoured, not normalised** — `ld` lays out pulled
  members in archive order, so order is load-bearing for byte-identical
  linked output (see the libc Makefiles).

## Documentation

- [docs/porting.md](docs/porting.md) — the header-layout porting guide.

## Build and test

    make          # builds ../../bin/pdp11-ar
    make check    # covered by tests/ld/ranlib.sh at the repo root and
                  # the lib-sweep oracle
