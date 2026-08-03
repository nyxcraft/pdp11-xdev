# libc + crt0 (runtime)

The C runtime and a substantial slice of the **authentic 2.8BSD libc**
(`libc/` mirrors `usr/kernel/src/libc/`), built by this toolchain's own `cc`
(C sources) and `as` (assembly), archived by its `ar`, and indexed by its
`ranlib` into `usr/lib/libc.a` (107 members).  `cc` links a program as
`ld -X crt0.o <objs> -lc`.

## Pieces

Startup & runtime helpers (assembly, `crt/` + `csu/` + `gen/`):
- `csu/crt0.s` — C startup `start:`: argc/argv/environ on the stack, calls
  `_main`, passes its return value to `exit`.  Installed as `usr/lib/crt0.o`.
- `crt/csv.s` — `csv`/`cret`, the prologue/epilogue helpers c1 calls in every
  function.
- `crt/cerror.s` — error return (sets `errno`, returns -1).
- `crt/{lmul,ldiv,lrem,almul,aldiv,alrem}.s` — the long-arithmetic helpers c1
  emits calls to for 32-bit `long` multiply/divide/remainder; `crt/mcount.s`
  for `-p` profiling counters.
- `gen/{cuexit,setjmp,abort}.s` — `exit()` (flush via `_cleanup` then the trap),
  `setjmp`/`longjmp`, `abort`.

C library (compiled by our `cc`):
- `gen/` — string/memory (`strcmp` `strlen` `strcpy` `strcat` `strncmp`
  `strncpy` `strncat` `index` `rindex`), numeric (`atoi` `atol` `abs`),
  char-class table (`ctype_`), `qsort`, and small utilities (`getenv`
  `mktemp` `perror` `errlst` `swab` `isatty` `isapipe` `stty`/`gtty` `sleep`
  `atof`, the authentic free-list `malloc`/`free`/`realloc`/`calloc`, and the
  float helpers `ecvt`/`fcvt`/`modf`/`ldexp`/`frexp`).
- `stdio/` — the full buffered-I/O layer: `printf`/`fprintf` (over the asm
  `_doprnt`), `fopen`/`freopen`/`fdopen`, `fgets`/`fputs`/`fgetc`/`getchar`/
  `putchar`/`puts`/`gets`, `ungetc`, `fseek`/`ftell`/`rew`, `setbuf`, `sprintf`, the
  internals (`data` `filbuf` `flsbuf` `findiop` `endopen` `rdwr` `strout`
  `clrerr`, `gcvt`), `getw`/`putw`, the float-print hooks `fltpr` (pfloat/
  pscien/pgen, also defining `fltused`), and the `scanf`/`sscanf` family.
- `sys/` — ~40 syscall stubs (`write` `read` `open` `close` `creat` `lseek`
  `stat` `fstat` `lstat` `dup` `pipe` `fork` `wait` `exec*` `getpid`
  `get/set uid/gid` `access` `chmod` `chown` `chdir` `link` `unlink` `kill`
  `ioctl` `umask` `time` `alarm` `sbrk` …), each assembled with
  `libc/include/sys.s` (the syscall-number defs) prepended — exactly as
  2.8BSD built them.

## Floating point

Fully supported in the authentic DEC F (32-bit) / D (64-bit) format — NOT
IEEE.  c1 emits FP11 codegen, `as` encodes it, and `apsim` emulates the FP
accumulators (converting DEC<->host-double on each load/store).  printf
`%f`/`%e`/`%g`, scanf `%f`, `atof`, and `ecvt`/`fcvt`/`gcvt`/`modf`/`ldexp`/
`frexp` all work.  See the `float` section of NOTES.md and tests/cc/float.sh.

## Out of scope (omitted)

The passwd/group database, networking, and the overlay / non-FP / profiling
build variants.

## Porting notes

Importing the library exercised paths `cc` output never had, which surfaced
four real compiler bugs (all fixed):
- the **`switch` table** packed wrong on the host (getblk pads to the
  node-union size, so `pswitch` read every case as 0 — c1/c11.c);
- **`long + char`** segfaulted c1 because the ITOL promotion node's unset
  `tr2` was dereferenced (a NULL the PDP-11 tolerated — c10.c);
- **`sreorder`** crashed when `optim()` reduced a node to a leaf and the
  operator logic then read its union-overlapped `tr1` as a pointer — a
  layout-dependent crash that only fired under cc's heap layout (c10.c);
- **structure assignment emitted no copy** (`*q = *p`, `*t++ = *s++`):
  `strasg` computed the word count as `mask / sizeof(int)`, but c1 runs on
  the host where `sizeof(int)==4`, so a 2-byte struct gave `2/4 == 0` words
  and the copy loop ran zero times.  Fixed with a target `SZINT` (=2)
  constant (c1/c11.c) — this is what made the authentic free-list
  `malloc`/`free`/`realloc` work (realloc copies blocks with `*t++ = *s++`).

The hand-written assembly also needed `as` features `cc` never emits (see
docs/as.md): multi-file input, numeric local labels, the `..` relocation
base.  `apsim` grew `break`/sbrk (so the heap can grow), `ioctl` (reports
ENOTTY so `isatty()==0` and stdio block-buffers), and `unlink`.

## Build

`make libc` compiles/assembles the objects and builds `usr/lib/{crt0.o,
libc.a}` (ranlib'd, so members may be in any order).  The member lists live in
the `LIBC_*` variables at the top of the `libc:` rule in the Makefile.
