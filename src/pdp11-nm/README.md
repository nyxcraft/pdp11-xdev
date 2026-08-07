# pdp11-nm

Lists the symbol table of a.out objects, executables, and archives — the
2.9BSD `nm`.

    pdp11-nm [-g] [-n] [-o] [-p] [-r] [-u] [file ...]

- **Reads what the era wrote:** 12-byte fixed-name nlist entries, the
  16-bit `n_type` with overlay numbers, and `__.SYMDEF`-bearing archives
  (the 0177545 magic compares correctly on LP64 via an unsigned-short
  pin — a real host bug fixed in this revision).
- Sorts alphabetically by default; `-n` by value, `-p` in table order,
  `-u` undefined only, `-g` externals only.

## Build and test

    make          # builds ../../bin/pdp11-nm
    make check    # covered by tests/binutils/size_nm_strip.sh at the
                  # repo root

## Documentation

- **[Design](docs/design.md)** — the nlist decoders and the sort/format machinery.
- **[User guide](docs/user-guide.md)** — the options and the output formats.
