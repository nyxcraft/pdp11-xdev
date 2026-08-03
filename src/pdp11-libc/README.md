# pdp11-libc

The per-universe target C libraries — not a host tool.  Each "full"
universe carries its era's authentic sources and build recipe, compiled
and assembled with the cross tools themselves:

    make            # (repo root: `make libc`) both universes
    make -C bsd29   # headers -> include/bsd29/, libs -> lib/bsd29/
    make -C bsd28   # headers -> include/bsd28/, libs -> lib/bsd28/

Products per universe: `crt0.o`, `libc.a`, `libcurses.a`, `libtermlib.a`
(+ `libtermcap.a` and the profiling crt0 variants where the era had them).

- **The member order is load-bearing.**  `ld` lays out pulled archive
  members in archive order, so each recipe archives in the exact order
  the era's `mklib` used — that is what makes linked output byte-match
  native binaries.
- **bsd29 also builds the era-exact library:** `libc-era.a`/`crt0-era.o`
  reassemble the syscall stubs against the reconstructed Feb–Mar 1983
  `sys-era.s`, reproducing the *shipped* `/lib/libc.a` stubs
  byte-for-byte (166/181 members) — link `-lc-era` to reproduce a 1983
  binary, `-lc` for the current-source library.
- **Both eras default to the non-FPU build** (`NOFP=1`, the `fmklib`
  flavor with the fpsim interpreter archived in), exactly as the shipped
  systems were; `make NOFP=0` builds the FP11 `mklib` flavor.
- **Old-format archives where the era used them:** the 2.9 curses/termlib
  archives are written by `mkcursesa.py`/`mktermliba.py` in the pre-4BSD
  0xff65 format with middle-endian headers, byte-identical to the shipped
  libraries.

## Documentation

- [docs/porting.md](docs/porting.md) — the libc porting guide inherited
  from the source project.

## Build and test

    make libc     # from the repo root (needs the tools built first)
    make check    # covered by tests/cc/libc.sh and the [bsd28] universe
                  # re-run in the end-to-end suite
