# pdp11-strip

Removes the symbol table and relocation from an a.out — the 2.9BSD
`strip`.

    pdp11-strip file ...

- **Overlay-aware truncation:** understands the `xexec` overlay header,
  so 0430 images strip to the correct length.
- **Byte-identical to the era:** rebuilding 2.9's `/bin/strip` with this
  toolchain against the era libc reproduces the shipped binary exactly
  (`oracle/selfhost.sh`).

## Build and test

    make          # builds ../../bin/pdp11-strip
    make check    # covered by tests/binutils/size_nm_strip.sh at the
                  # repo root
