# pdp11-cpp — design

This document explains what `pdp11-cpp` is, how it was made a *cross*
preprocessor for the PDP-11 without changing what it does, and the handful of
things the LP64 host forced. For how to *use* it, see the
[user guide](user-guide.md).

The sources are `cpp.c` (the preprocessor), `cpy.y` (the `#if` expression
grammar) and the `cpy.c` a yacc run generates from it, `yylex.c` (the `#if`
lexer, `#include`d by the grammar's tail), and `universe.h` (the shared table
that names the era include subdirectories). A companion
[porting guide](porting.md) covers the LP64 diff in more detail.

---

## 1. What it is

This is **John Reiser's `cpp`** (July/August 1978), the fast C preprocessor of
the 2.9BSD line — the one pcc was built against and that the 1978 comment blocks
throughout `cpp.c` still describe. It handles `#define`/`#undef`,
`#include`, the `#if`/`#ifdef`/`#ifndef`/`#else`/`#endif` family, `#line`, macro
expansion (object- and function-like, with formals recognised even inside
quotes, exactly as the `README` documents), and comment removal. It is a
**pre-ANSI** preprocessor: there is no `#`/`##` stringise-and-paste, and that is
deliberate — it is the contract pcc compiles against.

It is built with **`FLEXNAMES`** (`-DFLEXNAMES` in the Makefile), which sets
`NCPS` to 128 so macro names run to 128 characters like the native 2.9 binary,
rather than the historic 8. `FLEXNAMES` only widens cpp's internal symbol
table; the emitted text is byte-for-byte what the un-widened cpp would emit,
which is what lets the toolchain's downstream byte-exact oracles work.

---

## 2. Made a cross preprocessor

The one thing a preprocessor must get right for a *different* machine is where
it finds headers, and what it predefines. Both are aimed at the PDP-11 target,
never the build host.

**Predefined macros.** The Makefile compiles `cpp.c` with `-Dunix=1 -Dpdp11=1`,
which selects the `#if unix` and `#if pdp11` arms of `main()`; those call
`stsym("unix")` and `stsym("pdp11")`, so the built binary predefines **`unix`**
and **`pdp11`** (both to `1`). It also installs the dynamic **`__LINE__`** and
**`__FILE__`** (`ulnloc`/`uflloc`, expanded in `subst()`). That is the classic
set — the system and the machine — and everything else comes from the target
headers or from `-D`. (The `pdp11-cc` driver passes `-Dunix -Dpdp11` on the cpp
command line as well; harmless, since cpp already defines them to the same
value.)

**Include search.** The original walked `$PATH` through the UCB library to turn
each `.../bin` element into `.../include`; that assumes an installed `/usr`
tree. `getincdir()` replaces it with the same binary-relative scheme cc and ld
use: if `argv[0]` is a bare name it reads `/proc/self/exe`, finds the final
path component, requires the directory above it to be exactly `bin`, and
rewrites `.../bin/<prefix>-cpp` to `.../include`. It then appends the era
subdirectory `include/<universe>` — `$PDP11_UNIVERSE`, or
`PDP11_UNIV_DEFAULT_NAME` (`"bsd29"`, from `universe.h`) — and `stat()`s it,
falling back to a flat `.../include` when that subdir is absent. `main()`
appends this resolved directory, then a relative `include` (build-tree headers,
pre-install), then `/usr/include`, after any `-I` directories.

---

## 3. `BUFSIZ` is pinned to 512

cpp scans in a fixed window, `buffer[NCPS + BUFSIZ + BUFSIZ + NCPS]`, split at
`pbeg`/`pbuf`/`pend`, and when macro expansion needs to back up past the start
of the window it spills text into a side buffer (`sbf[SBSIZE]`, `SBSIZE`
24000) whose slabs are `BUFSIZ` bytes each. All the threshold arithmetic — the
`SBSIZE - BUFSIZ` "no space" guards, the `(p - inp) >= BUFSIZ` long-comment
split, `unfill()`'s slab size — is written for **`BUFSIZ == 512`**, the PDP-11
stdio value. glibc defines `BUFSIZ` as 8192, which makes `SBSIZE - BUFSIZ` tiny
and trips the side-buffer bounds checks immediately, so `cpp.c` does
`#undef BUFSIZ` / `#define BUFSIZ 512` up front and every buffer is sized from
that. The same 512 is handed explicitly to `setvbuf()` on the output file,
because stdio would otherwise assume its own 8192-byte `BUFSIZ` for the
512-byte `_sobuf` and overrun it (4 NULs at the boundary).

---

## 4. The superimposed-code fast path

Reiser's speed comes from not calling the symbol table for tokens that cannot be
macros. `main()` builds byte-classification tables — `fastab`/`slotab` (bit
flags `IB`/`SB`/`NB`/`CB`/`QB`/`WB` for identifier, separator, number, comment,
quote, warn), `toktyp` (`BLANK`/`IDENT`/`NUMBR`), and `macbit`, a superimposed
code keyed by each character *and its position* in the identifier. As
`cotoken()` scans an identifier it ANDs `macbit` bit-by-bit (`scw1`, bits
`b0..b7`); the moment a position's bit is clear, no defined macro has that
character there, so the token jumps to `nomac` and is emitted without a
`slookup()`. `#define` sets those bits (`xmac1`) when it records a name. The
pairwise variant `scw2` is compiled out (`scw1 1`, `scw2 0`).

The tables are indexed by a plain `char`. On the LP64 host that is signed and
runs −128..127, so every table access is offset by **`COFF`** — 128 under
`#if pdp11 | vax`, 0 elsewhere — as in `(fastab + COFF)[c]`. `COFF` is defined
identically at the top of both `cpp.c` *and* `yylex.c`; the two agree only
because the Makefile compiles both translation units with `-Dpdp11=1`. Getting
that wrong (one side offsetting, the other not) makes a high-bit byte misclassify
and `defined` fail to lex.

---

## 5. The `#if` parser

`cpy.y` is the yacc grammar for `#if` constant expressions — the full C
operator set with C precedence, the `?:` and comma operators, and the two forms
of `defined` (`DEFINED '(' number ')'` and `DEFINED number`). `control()` calls
`yyparse()` for a `#if`; a true result bumps `trulvl`, a false one bumps
`flslvl`, and nothing (not even newlines) is emitted while `flslvl` is nonzero.

`yylex.c` is the lexer, `#include`d at the very end of `cpy.y`, so it compiles as
part of the generated parser rather than as its own object. It recognises the
two-character operators, one-character operators, numeric constants (via
`tobinary`, which also handles hex, octal, and the `l`/`L` suffix), character
constants with escapes, and identifiers — an undefined identifier evaluates to
`0`. `defined` is handled by raising `flslvl` so its operand is not macro-expanded
and lowering it again once the operand is read; that balance is the fix for the
2.8 `defined()`/`flslvl` state leak (preserved as a golden record — see Testing).

Unlike the VAX port, **`cpy.c` here is a build artifact**: the Makefile
regenerates it from `cpy.y` with the host `yacc` (`YACC`, which resolves to
GNU bison), it is `.gitignore`d, and `make clean` removes it. It is not a file
to edit — change `cpy.y`.

---

## 6. The LP64 port

The diff against the historic source is modernisation plus the era-neutral
changes above. The parts the 64-bit host forced:

- **`stdarg`, not K&R varargs.** `pperror`/`yyerror`/`ppwarn` funnel through
  `vpperror(char *, va_list)`. Under the old implicit-`int` varargs a pointer
  argument (a filename, e.g. in `Can't find include file %s`) was truncated to
  `int`, so `%s` read a bad pointer and crashed.
- **Prototypes for pointer-returning functions.** `lookup()` (called from
  `yylex.c`) and the other symbol-table routines are declared returning
  `struct symtab *`; without the prototype the returned pointer was truncated to
  `int`.
- **`fout` set in `main()`.** It is initialised to `stdout` at the top of
  `main()` rather than as a static initialiser, since `stdout` is not a
  constant on modern libc.

---

## 7. Recent hardening

Bounds fixes on the option-parsing path, output-neutral for valid input:

- **`dirs[]` grown `10 → 32`.** The `-I` cap (`nd > 8`) plus the roughly seven
  unconditional trailing appends (the resolved include dir, `include`,
  `/usr/include`, the `0` terminator, `dirs[0]`) overflowed a `dirs[10]` once a
  build passed six or more `-I` flags — an ordinary occurrence.
- **`-D`/`-U` guards fixed to `>=`.** `predef`/`prund` were tested with `>`, so
  the 21st `-D` or `-U` (with `NPREDEF == 20`) wrote one past `prespc`/`punspc`.

---

## Testing

`tests/cpp/` holds the golden suite, run by the repo's `tests/run.sh`
(`-u` regenerates the `.expected` files). It compares **whole output** — the
`# line "file"` directives included — across the pre-ANSI contract, object- and
function-like macros, nested expansion, the `#if`/`#ifdef` conditionals, the
`defined` operator with and without parentheses, `#include` resolution, and the
predefined `pdp11`/`unix`. Separately, `tests/cpp-bsd28/` pins the *old* 2.8
`defined()`/`flslvl` quirk as a golden record, so the fix in §5 cannot be undone
without the difference showing. Whole-output comparison is the point: cpp's text
feeds the byte-exact oracles for the rest of the toolchain, so a silent
character-classification or line-directive regression here would surface far
downstream.

---

## For a maintainer

- **Keep it pre-ANSI.** `#`/`##` inert is the contract with pcc, not a bug.
- **`BUFSIZ` must stay 512.** The side-buffer arithmetic and the output
  `setvbuf()` size both depend on it; do not let glibc's 8192 back in.
- **`COFF` is defined in both `cpp.c` and `yylex.c`.** They agree only because
  the Makefile builds both with `-Dpdp11=1` — keep that flag on `cpy.o` too.
- **`cpy.c` is generated and `.gitignore`d.** Edit `cpy.y`; the host bison
  regenerates `cpy.c`.
- **The include default is binary-relative.** `getincdir()` resolves
  `include/<universe>` from `/proc/self/exe`; there is no `/usr`-tree assumption
  and no `$PATH` search left.
