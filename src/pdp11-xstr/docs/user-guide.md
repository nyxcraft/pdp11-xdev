# pdp11-xstr — user guide

`pdp11-xstr` is Bill Joy's 1978 `xstr`: it reads a C program, pulls its string
literals out into one shared, read-only data file (`strings`), and rewrites the
program so each literal becomes a reference `(&xstr[N])` into that pool. Identical
strings are stored once — and a string that is a trailing substring of another
is not stored at all — which is how it saved text and data space on the memory-
constrained PDP-11. It runs on your host as part of the 2.9BSD build.

For how it works inside — the scan, the hashing, the pool layout — see the
[design document](design.md).

---

## 1. Synopsis

```
pdp11-xstr [ -v ] [ -c ] [ - ] [ name ... ]
```

(The tool's own usage line still spells itself `xstr`.) Each `name` is a C source
file; a bare `-` reads the program from standard input instead.

`xstr` is normally used in **two phases**, in place of the compile step:

```
xstr -c foo.c        # per source: append its strings to the pool, rewrite -> x.c
cc -c x.c            #             compile the rewritten source
mv x.o foo.o
...                  # repeat -c for every source
xstr                 # once at the end: emit xs.c from the finished pool
cc -c xs.c           #             compile the shared pool, link xs.o with the rest
```

Given a `name` **without** `-c`, it runs one-shot instead: a private pool in
`/tmp` (removed afterward) and both `x.c` and `xs.c` for that single file.

---

## 2. Options

| option | meaning |
|---|---|
| `-c` | compile mode: seed from and append to the persistent `strings` pool, rewrite the source to `x.c`, but do **not** emit `xs.c` (the final bare `xstr` does that) |
| `-` | read the program from **standard input** instead of a named file |
| `-v` | verbose: report each string on stderr as `new at N:` or `found at N:` (with the string, control bytes shown as `^X`, high bytes as `\ooo`) |

Flags may be glued together (`xstr -cv foo.c`). An unrecognised flag prints the
usage line to stderr but is otherwise ignored — the run continues.

With **no operands at all** (`xstr`), the tool seeds from the existing `strings`
pool, processes nothing, and writes `xs.c` — this is the closing phase of the
two-step build.

---

## 3. Files it produces

All paths are relative to the current directory unless noted.

- **`x.c`** — the rewritten source: the input C with every string literal
  replaced by `(&xstr[N])` and a `char xstr[];` reference added at the top.
  Reopened fresh for each input, so it holds the most recently processed file.
- **`strings`** — the shared string pool: the decoded literal bytes, each
  NUL-terminated, packed so identical and suffix-sharing strings coincide. It
  persists in the current directory and **accumulates** across `-c` runs. (When a
  `name` is given without `-c`, the pool is instead a temporary `/tmp/xstrXXXXXX`
  that is deleted on exit.)
- **`xs.c`** — the pool as C source: `char xstr[] = { 0x.., … };`. Compile and
  link this once with every object built from an `x.c`. Not written under `-c`.

An interrupt (or a write error) removes any temporary pool, `x.c`, and `xs.c`
before exiting, so a killed run leaves nothing half-written behind.

---

## 4. Exit status

- **0** — success.
- **1** — cannot create `x.c`.
- **2** — cannot open a named input file.
- **3** — read error on the input.
- **4** — write error on the `strings` pool.
- **5** — cannot reopen `strings` to build `xs.c`.
- **6** — cannot create `xs.c`.
- **7** — interrupted, or an output error mid-run (temporaries cleaned up first).
- **8** — cannot open `strings` for writing.

---

## 5. Examples

```
xstr -c foo.c                 # phase 1: extract foo.c's strings, rewrite to x.c
xstr                          # phase 2: build xs.c from the accumulated pool
xstr foo.c                    # one-shot: x.c and xs.c for foo.c, no persistent pool
cat foo.c | xstr -            # same, reading the program from standard input
xstr -v -c foo.c              # narrate each string as it is added to the pool
```

A full 2.9BSD-style build of several sources:

```
rm -f strings
for f in *.c; do xstr -c $f && cc -c x.c && mv x.o ${f%.c}.o; done
xstr && cc -c xs.c            # then link every .o together with xs.o
```

Continue to the [design document](design.md) for the pool layout and the
suffix-overlap hashing.
