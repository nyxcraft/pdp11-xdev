# pdp11-xstr

Bill Joy's 1978 `xstr`: extracts and hashes the string literals of a C
program into a shared `strings` file, replacing them with indexes — the
classic text-space saver for big BSD programs.

    pdp11-xstr [ -v ] [ -c ] [ - ] [ name ... ]

- **Needed for byte-fidelity:** 2.9's `/usr/70/rogue` was built through
  xstr, so reproducing it exactly requires reproducing the string pool
  exactly — which this port does (see the rogue byte-identity result in
  NOTES.md).
- `-c` extracts without the shared-pool merge step, `-` reads standard
  input, `-v` narrates; output goes to `x.c` and the accumulated
  `strings` file, as the original did.

## Build and test

    make          # builds ../../bin/pdp11-xstr
    make check    # exercised via the rogue overlay reproduction

## Documentation

- **[Design](docs/design.md)** — the string-extraction pass and how xs.c and strings are emitted.
- **[User guide](docs/user-guide.md)** — using it inside a build.
