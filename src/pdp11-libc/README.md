# pdp11-libc

**One universal C library** for the PDP-11 cross toolchain — not a host
tool.  A single `libc.a` and one `crt0` serve every universe; the era is
chosen when a program is **linked**, not when the library is built.  This
is the [vax11-libc](../../gh-pages/site.json) mechanism, ported to the
PDP-11.

    make libc       # from the repo root: builds lib/libc.a + crt0 + curses/termlib

Products (installed **flat**, not per-universe): `libc.a`, the crt0 flavours
`crt0.o`/`mcrt0.o`/`fcrt0.o`/`fmcrt0.o`, `libcurses.a`, `libtermlib.a`
(+ `libtermcap.a`), and the matched header set into `include/`.

## How one library serves every era

- **`ld` stamps `__univ`.**  `pdp11-ld`, reading `--universe` /
  `$PDP11_UNIVERSE`, defines the absolute symbol `__univ` = the era id
  (28, 29, …) in the executable.  `--universe` does *only* this — it does
  not pick a startup file or an archive path (one `crt0`, one `libc.a`
  serve all).  A disagreeing input definition fails the link.
- **`crt0` records it.**  The first thing `crt0` does is copy `$__univ`
  into a data cell (`extern int __univ;`) the library can read.
- **Movers branch on it.**  Any routine whose ABI moved between releases is
  a single function that tests `__univ` — a run-time `if` on a link-time
  constant.  The library serves the whole **V7-syscall-convention family —
  V5, V6, V7, 2.8BSD, 2.9BSD** — which share the inline/indirect `sys` trap
  convention and (bar `creat`) the same numbers, so no routine has to
  dispatch yet.  The battery compiles once per universe and runs under each
  (`oracle/cross-universe.sh`).  The machinery is in place for 2.10/2.11,
  which use the 4BSD **stack-arg** convention and a 4.x renumber (stat 18→38,
  fstat 28→62, wait 7→84, …) plus a 52-byte `struct stat` — a second
  personality still to be dispatched, exactly as vax11-libc left `struct
  stat` layout as its open edge.

## Source layout

The sources are 2.9BSD's libc (a functional superset of 2.8BSD — identical
syscall numbers, superset `errno`/`signal`, one `struct stat` layout), split
by *what kind* of code a file is, not by universe:

    common/gen      ABI-independent C + asm: string, ctype, malloc, qsort, ...
    common/stdio    the buffered-I/O layer (one UCB_LINEBUF buffering model)
    common/sys      the syscall stubs (assembled against common/include/sys.s)
    common/nonfpcrt software long-arithmetic (EIS) + csv/cerror -- the no-FPU
                    default; fpsim (the FP interpreter) rides along
    common/csu      crt0 and its variants (each stamps __univ at startup)
    include/        the matched header set, installed flat into ../../include
    curses/ termlib/ fpsim/   the screen libraries and the FP interpreter

Byte-for-byte reproduction of the native per-era `libc.a` is deliberately
**not** a goal (that was the old per-universe build), so the archive member
order is free.  Note this made the fake `_cleanup`/`fptrap` fallbacks
(`fakcu`/`fakfp`) unnecessary and, once member order stopped being
load-bearing, harmful — they are excluded so the real `_cleanup` (which
flushes stdio on exit) and the real `fptrap` always win.

## Documentation

- [docs/porting.md](docs/porting.md) — the libc porting guide.

## Build and test

    make libc     # from the repo root (needs the tools built first)
    make check    # exercised by the end-to-end suite (a bsd28 AND a bsd29
                  # re-run of every correctness program, from the one libc.a)
                  # and oracle/cross-universe.sh
