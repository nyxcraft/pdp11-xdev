# pdp11-c0 — user guide

`pdp11-c0` is **pass 1** of the classic Ritchie PDP-11 C compiler: it reads
already-preprocessed C and writes the intermediate token/operator stream that
`pdp11-c1` turns into PDP-11 assembly. It is a **compiler pass, not a user
command** — normally `pdp11-cc` runs it for you, between the preprocessor and the
code generator. It emits **no assembly** itself. For how it works inside — the
parser, the symbol table, the stream format — see the [design document](design.md).

---

## 1. Synopsis

```
pdp11-c0 source.i temp1 temp2 [-P] [-V]
```

Unlike a filter, `c0` takes three **file** arguments and does not read stdin:

- **`source.i`** — the input, C that has **already been preprocessed** (run
  `pdp11-cpp` first; `c0` does not do `#include` or macros);
- **`temp1`** — receives the intermediate **code** stream;
- **`temp2`** — receives the **strings and data**; `c1` reads it after `temp1`.

Options (each is normally supplied by `cc`, not by hand):

| flag | meaning |
|------|---------|
| `-P` | emit profiling counters (`cc -p`) |
| `-V` | compile for text overlays (`cc -V`) |

Giving fewer than three filenames is an error (`Arg count`).

---

## 2. Invocation (normally via cc)

`c0` is one stage of a five-stage pipeline that `pdp11-cc` drives in order:

```
pdp11-cpp  ->  pdp11-c0  ->  pdp11-c1  [ ->  pdp11-c2 ]  ->  pdp11-as  ->  pdp11-ld
```

- **`pdp11-cpp`** — the preprocessor; produces the `.i` `c0` consumes.
- **`pdp11-c0`** — this pass: preprocessed C → the intermediate stream.
- **`pdp11-c1`** — the code generator: the stream → PDP-11 assembly.
- **`pdp11-c2`** — the optional peephole optimiser (`cc -O`).
- **`pdp11-as` / `pdp11-ld`** — assemble and link.

`cc` invokes `c0` exactly as `c0 <input.i> tmp1 tmp2 [-P] [-V]`, forwarding `-P`
when you compiled with `-p` and `-V` when you compiled with `-V`. So for an
ordinary compile you never name `c0` at all:

```sh
pdp11-cc -c foo.c            # cc runs cpp, c0, c1, as for you
```

Reach for `c0` directly only to inspect the intermediate stream a translation
unit produces, or to work on the compiler itself — see the examples below.

---

## 3. Output

`c0` writes **two files, not assembly**:

- **`temp1`** — the intermediate code stream: a flat sequence of operator bytes,
  16-bit words, symbol names and constants (the `c0 → c1` interface, documented
  in [design §4](design.md)). It is host-independent: the exact same bytes on the
  PDP-11 and on your host.
- **`temp2`** — string literals and initialised data, which `c1` folds in after
  the code.

Neither file is human-readable text; both are meant for `c1`. To eyeball the
stream, dump `temp1` with `od` (see §5).

**Diagnostics** go to **stderr**, prefixed `file:line:` (the file name comes from
the `#line` directives `cpp` left in the input), e.g.

```
foo.c:12: x undefined; func. main
```

---

## 4. Exit status

- **0** — the translation unit parsed with no errors.
- **1** — one or more source errors were diagnosed (`exit(nerror != 0)`), or a
  fatal startup/resource condition (`Arg count`, `Can't find …`, `Out of space`,
  a table overflow), each reported before exit.

A **signal** exit (a segfault) is a compiler/port bug, not a source error. This
port specifically hardened the syntax-error paths that used to follow a NULL tree
pointer — harmless on the PDP-11, a fault on a 64-bit host — so that even
malformed input now yields a diagnostic and a clean status 1 (see
[design §6](design.md)).

---

## 5. Examples

```sh
# The normal way: let cc drive the pass.
pdp11-cc -c foo.c

# Run c0 by hand on one preprocessed unit (what the test suite does).
pdp11-cpp -Dpdp11=1 foo.c > foo.i     # preprocess first
pdp11-c0 foo.i foo.1 foo.2            # foo.1 = code stream, foo.2 = strings/data
pdp11-c1 foo.1 foo.2 foo.s            # then c1 turns it into assembly

# Inspect the intermediate stream c0 produced.
od -An -tx1 foo.1 | less

# Profiling counters (equivalently, pdp11-cc -p foo.c).
pdp11-c0 foo.i foo.1 foo.2 -P
```

Continue to the [design document](design.md) for the parser, the symbol table,
and the byte-for-byte layout of the stream in `foo.1`/`foo.2`.
