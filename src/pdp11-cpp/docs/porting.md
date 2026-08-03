# Porting guide: cpp

The C preprocessor (John Reiser's fast cpp). Handles `#include`, macro
definition/expansion, and `#if` evaluation, emitting preprocessed source
with `# line "file"` markers for the compiler.

## Sources

- `cpp/cpp.c` — the preprocessor.
- `cpp/cpy.y` — a yacc grammar evaluating `#if` constant expressions.
- `cpp/yylex.c` — the `#if` lexer, `#include`d at the end of `cpy.y`.
- `libucbpath/{concat,openl,openlp,ucbpath}.c` — UCB library used to search
  `$PATH` for headers (each `…bin` element is rewritten to `…include`).

## Build

```
yacc cpy.y                                  # -> cpy.c
cc -Dunix=1 -Dpdp11=1 -c cpp.c
cc -Dunix=1 -Dpdp11=1 -c cpy.c              # MUST define pdp11 here too
cc cpp.o cpy.o libucbpath/*.o -o cpp
```

## Porting fixes

1. **Old yacc → POSIX yacc/bison.** In `cpy.y`: `%term`→`%token`,
   `%binary`→`%nonassoc`, and `={ … }` action braces → `{ … }`. Compatible
   with both old and new yacc.

2. **`-Dpdp11=1` on the grammar unit too.** `yylex.c`'s signed-char table
   offset `COFF` is 128 only under `#if pdp11|vax`. The lexer indexes a
   character table that `cpp.c` initialises at offset +128; if `cpy.c` is
   compiled without `pdp11` defined, its `COFF` is 0 and it reads
   uninitialised entries (symptom: `defined(X)` → "Illegal character d").
   The native PDP-11 cc predefined `pdp11`; gcc does not, so pass it
   explicitly to **both** translation units.

3. **LP64 pointer truncation.** `yylex.c` calls `lookup()` with no
   prototype in scope, so its `struct symtab *` return was truncated to
   `int` → segfault on `sp->value`. Declare `struct symtab *lookup();`.

4. **K&R varargs in libucbpath.** `openl`/`openlp` gathered their variadic
   string arguments by taking the address of the last named parameter and
   walking memory — valid only when arguments are stack-contiguous (PDP-11),
   not on the x86-64 register ABI. Rewritten as `<stdarg.h>` variadic
   functions that copy the `char*` arguments into an array, which the
   unchanged `_concat`/`makefp` consume.

5. **Modern-libc details in cpp.c.**
   - `FILE *fout = stdout;` at file scope — `stdout` is not a compile-time
     constant on modern libc; initialise to `0` and assign in `main`.
   - `extern char _sobuf[];` — old libc exported its stdout buffer; provide
     a local `static char _sobuf[BUFSIZ];`.
   - **`BUFSIZ` forced to 512.** cpp's side-buffer bounds checks use
     `SBSIZE-BUFSIZ`. glibc's `BUFSIZ` is 8192, which makes that threshold
     tiny and trips immediately ("too much defining"). cpp was written for
     the PDP-11 stdio `BUFSIZ` of 512; `#undef`/redefine it.

## Verified behaviour

`tests/cpp/*` (golden tests; line markers normalised to basenames):

- object- and function-like macros, nested expansion (`DBL(21)` →
  `((21)+(21))`)
- `#if`/`#ifdef`/`#ifndef` + arithmetic, `defined()`
- comment removal and comment-as-token-break
- quote and `<>` `#include` resolution (the `libucbpath` search)
- the `pdp11` predefined macro
- preprocessing the real 2.8BSD `<stdio.h>`

## Known limitation (authentic 2.8BSD behaviour, preserved)

`#if defined(M)` where `M` is a **defined** macro pre-expands to
`defined(10)`; the lexer then takes its numeric path, which skips the
`flslvl` reset the identifier path performs. The `#if` itself is correct,
but the leak suppresses the **next** `#if`. Harmless in 1981 code (which
used `#ifdef`, not the then-new `defined()` operator). This is genuine
2.8BSD behaviour and is preserved; `tests/cpp/defined_quirk.c` documents
it. See `NOTES.md` → Known limitations.
