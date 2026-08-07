# pdp11-cpp — user guide

`pdp11-cpp` is the C preprocessor the PDP-11 pcc was built against — John
Reiser's fast `cpp` of the 2.9BSD line, the pre-ANSI one. It handles
`#include`, `#define`/`#undef`, macro expansion, `#line`, and the `#if` family,
and it is a **cross** preprocessor: it looks for headers in the PDP-11 target
tree, not the host's `/usr/include`, and it predefines the target's macros. It
runs on your host and emits `# line "file"` markers for the compiler.

For why it is pre-ANSI on purpose, how the header search is resolved, and the
LP64 detail, see the [design document](design.md).

---

## 1. Synopsis

```
pdp11-cpp [-P] [-E] [-R] [-C] [-Dname[=def]] ... [-Uname] ... [-Idir] ...
          [infile [outfile]]
```

With two filenames the first is the input and the second the **output** file;
with one, the input, output to stdout; with none, stdin to stdout. A third name
is an error (`extraneous name`).

---

## Options

Read directly from `main()`; there is no `--std`, `-nostdinc`, or `-M` — the
PDP-11 cpp is the plain Reiser preprocessor.

| option | meaning |
|---|---|
| `-Dname` / `-Dname=def` | define `name` as `1`, or as `def` (bare `-D` with no name is ignored) |
| `-Uname` | undefine `name`, including a predefined one (an `=…` tail is stripped) |
| `-Idir` | add `dir` to the header search path, before the defaults (up to eight) |
| `-C` | keep comments in the output |
| `-P` | no `# line` directives (also implies `-E`) |
| `-E` | accepted, no-op |
| `-R` | allow recursive macros (disables the recursion guard) |
| `-` | a bare `-` is accepted and ignored |

Up to 20 `-D` and 20 `-U` options are honoured; beyond that cpp warns and
ignores the excess. An unrecognised `-x` draws `unknown flag`.

---

## What is predefined

`pdp11-cpp` predefines **`unix`** and **`pdp11`** (both to `1`) and the standard
**`__LINE__`** and **`__FILE__`**. That is the whole set — the system and the
machine; everything else (`BSD`, feature macros, and so on) comes from the
target headers or from `-D`. The `pdp11-cc` driver passes `-Dunix -Dpdp11` too,
which is redundant with what cpp already defines.

---

## Include search

The search order for `#include` is:

1. **`dirs[0]`** — the directory of the file doing the including (the source
   file's directory, or `.` for stdin). A `#include "…"` starts here; a
   `#include <…>` skips it.
2. **each `-Idir`**, in command-line order.
3. **the install include directory**, resolved relative to the cpp binary:
   `.../bin/<prefix>-cpp` → `.../include/<universe>`, where `<universe>` is
   `$PDP11_UNIVERSE` (default `bsd29`). If that era subdirectory does not exist,
   a flat `.../include` is used instead. This is the same binary-relative scheme
   cc and ld use — no installed `/usr` tree is assumed.
4. **`include`** — a relative path, for build-tree headers before install.
5. **`/usr/include`**.

`$PDP11_UNIVERSE` therefore selects *which era's headers* an unqualified
`#include` resolves against, without any flag:

```
PDP11_UNIVERSE=bsd211 pdp11-cpp prog.c    # resolve <> headers from include/bsd211
```

---

## Exit status

- **0** — success.
- **8** — a source or output file could not be opened.
- **non-zero** — the count of preprocessing errors (a missing header, an
  unterminated macro call, a bad `#include`, an unknown flag). Warnings — a
  macro redefinition, a formal/actual mismatch — go to stderr but do not change
  the status.

All diagnostics are written to standard error (file descriptor 2), prefixed with
the file and line, exactly as the historic cpp did.

---

## Examples

```
pdp11-cpp foo.c foo.i                 # preprocess to a file
pdp11-cpp -DDEBUG -I. foo.c           # define a macro, add a search dir
pdp11-cpp -C -P foo.c                 # keep comments, drop line directives
pdp11-cpp -Uunix foo.c                # drop a predefined macro
PDP11_UNIVERSE=bsd211 pdp11-cpp x.c   # resolve <> headers from the 2.11 tree
```

Continue to the [design document](design.md) for the pre-ANSI contract, the
`BUFSIZ`-512 buffering, the superimposed-code fast path, and the `#if` parser.
