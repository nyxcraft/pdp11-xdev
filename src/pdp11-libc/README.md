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
- **Stubs branch on it.**  Each syscall stub is a *multi-convention* stub that
  tests `__univ` — a run-time `if` on a link-time constant — and serves **three
  families from one archive**:
  - the **V7 family (V5, V6, V7, 2.8BSD, 2.9BSD)** — the inline/indirect `sys`
    trap convention, V7-era numbers (the pristine body);
  - the **4BSD family (2.10BSD, 2.11BSD)** — args already on the C stack, the
    4.x numbers (stat 18→38, fstat 28→62, …).  For `__univ >= 210` the stub
    traps directly with the era number before the V7 frame is ever built;
  - the **First Edition family (V1 and V2, `__univ` 1 or 2)** — the 1972 convention: the fd (if
    any) in r0 and the remaining args as *inline words after the trap*, which
    for a C call are run-time values, so the stub patches those words before
    trapping (self-modifying text; the 11/20 had no split I&D).  `pdp11-ld`
    emits the 0405 format (12-byte header, code at 040014); apsim emulates the
    11/20's missing EIS and `setd`, so the same `libc.a` — `printf` and all —
    runs on 1972 First AND Second Edition UNIX.

  The 4BSD/V1 halves are machine-generated: `tools/mkdual.py` wraps each
  pristine V7 stub, reading the 4.x numbers from `tools/sysnums.tsv` (extracted
  from the target trees' own `syscall.h`) and the V1 inline shapes from a small
  table.  The battery (hello/arith/str/float) compiles once per universe and
  runs under all **nine** (`oracle/cross-universe.sh`).  Still V7-only under
  2.10/2.11 (a documented edge, being closed): `stat`/`fstat`/`lstat` (they
  also need the 52-byte `struct stat` header) and `fork`/`pipe`/`wait`
  (two-value returns) — the same `struct stat` layout edge vax11-libc
  documented.  Under V1/V2 the covered set is the common syscalls (write/read/
  open/close/creat/unlink/lseek/chdir); the rest stay V7-shaped there.

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
