# pdp11-size

Prints the text/data/bss sizes (and their sum) of a.out files — the
2.9BSD `size`, rev 2.5.

    pdp11-size [object ...]

- **Overlay-aware:** for 0430 auto-overlay images it reports each
  overlay's size and the true core footprint, from the `ovlhdr` the
  2.9 `ld` writes.

## Build and test

    make          # builds ../../bin/pdp11-size
    make check    # covered by tests/binutils/size_nm_strip.sh at the
                  # repo root
