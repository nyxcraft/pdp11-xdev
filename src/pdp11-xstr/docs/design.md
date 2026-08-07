# pdp11-xstr — design

This document describes how `pdp11-xstr` pulls the string literals out of a C
program into one shared, read-only pool and rewrites the program to reference
them by offset — the classic 2BSD text-space saver. For how to *use* it, see the
[user guide](user-guide.md).

The tool is one file, `xstr.c` — Bill Joy's `xstr`, UCB, November 1978 — using
nothing but the C library plus a thin shim. The port's changes are two era-fidelity
fixes (§6, §7) and the LP64-safe cast macros (§7), not a rewrite.

---

## 1. The problem: a string literal costs space in every object

On the PDP-11 a program's address space is tight, and duplicated string literals
waste it twice over — once in each object's data, and again when two objects hold
the same bytes. `xstr` fixes both by moving every literal into one shared data
file, `strings`, and rewriting each source so the literal becomes a reference
into that pool. Identical strings are then stored **once**, and a string that is
a trailing substring of another is not stored at all (§4). The pool is compiled
once, as `xstr[]`, and linked with everything that references it: `"hello"` in
the source becomes `(&xstr[N])`, where `N` is the byte offset of `hello\0` in the
pool, and each rewritten file carries a `char\txstr[];` reference at its top.

---

## 2. What each invocation does (the argv handling)

`main` walks the leading `-` arguments itself. A bare `-` sets `readstd` (read
the program from standard input); `-c` sets `cflg`; `-v` sets `vflg`; anything
else prints the usage line to stderr **but does not exit** — an unknown flag is
only a warning. What remains in `argv` are source file names.

Two decisions follow from the flags, and they are what make `xstr` a two-phase
tool. **Which pool file:** `if (cflg || (argc == 0 && !readstd))` the persistent
`strings` file in the current directory is used and seeded from (§5); otherwise
the pool is a throwaway `/tmp/xstrXXXXXX` (§7), unlinked on exit. **Whether to
emit `xs.c`:** only when `cflg == 0`.

So the standard build is `xstr -c foo.c` per source (append to `strings`, emit
`x.c`, compile that), then a bare `xstr` at the end (seed from the finished
`strings`, process nothing, emit `xs.c`). A name **without** `-c` is the one-shot
form: a private `/tmp` pool, both `x.c` and `xs.c`, pool removed afterward.

---

## 3. The scan and the rewrite (`process`, `yankstr`)

For each input, `stdout` is reopened onto `x.c` and the source is read a line at a
time. `process` prints the `char\txstr[];` forward reference, then copies the
source through character by character with three things it must recognise so it
does not mistake them for string literals: `/* … */` comments (tracked by
`incomm`), `'c'` character constants (copied verbatim, escape and all), and
`#`-lines (a `# <digit>` cpp marker is rewritten to `#line …`, any other passes
through untouched).

An unescaped `"` outside a comment hands the rest of the line to `yankstr`, which
consumes the literal up to its closing quote, **decoding escapes as the compiler
would** — `\b \t \r \n \f \\ \"`, a dropped `\`-newline continuation, and one- to
three-digit octal `\ooo` — into a NUL-terminated buffer. That decoded byte string,
not the source text, enters the pool via `hashit`, and `process` prints
`(&xstr[N])` with the returned offset in its place — so `"\n"` and a literal
newline collapse to the same pool byte.

---

## 4. Hashing, and the suffix-overlap trick (`hashit`)

The pool is addressed by byte offset, so the hash exists only to answer "is this
exact byte string, or a string that ends the same way, already stored?" The
bucket is chosen by the **last character** of the string (`lastchr(str) & 0177`,
128 buckets, chained) — deliberately, because every candidate for reuse must
share that final byte.

Walking the chain, `istail(str, of)` asks whether `str` is a **trailing
substring** of an already-stored `of`: if so it returns the index `d` at which
`str` begins inside `of`, and `hashit` returns `hp->hpt + d` — an offset pointing
*into the middle* of the existing entry. Because C strings are NUL-terminated,
the suffix shares the earlier string's terminator, so `"ing"` needs no storage of
its own once `"string"` is in the pool; `&xstr[N]` simply lands three bytes in.
Exact reuse is just the `d == 0` case. A genuinely new string is `calloc`'d,
recorded at `mesgpt` (the next free offset), and `mesgpt` is advanced by its
length plus the NUL.

---

## 5. Seeding from an existing pool (`inithash`, `fgetNUL`)

Accumulation across many `xstr -c` runs works because `inithash` reads the
current `strings` file back in before anything new is scanned. It walks the file
one NUL-terminated record at a time with `fgetNUL`, recording each with
`hashit(buf, 0)` — `new == 0` — at the offset it already occupies (`mesgpt` is
snapped to `tellpt`, the running byte position `xgetc` maintains). The hash
therefore starts each run holding every offset already handed out — a repeated
literal resolves to its old offset, and only new strings are appended and written
back (§6).

---

## 6. Writing the pool and `xs.c` (`flushsh`, `xsdotc`)

`flushsh` walks all buckets, tallying `new` versus `old` entries. If `new == 0`
and some `old` exist it returns without touching the file — the fully-seeded case,
where the pool on disk is already correct. Otherwise it opens `strings` and, for
each new entry, `fseek`s to that entry's offset and writes its bytes plus the NUL,
so each string lands exactly where its offset promised.

The open mode is the port's first fidelity fix: `fopen(strings, old ? "r+" : "w")`.
The original used `"a"`, which under 2.9 stdio merely seeked to EOF once at open
so the later `fseek`/`fwrite` still landed at the seeked offset; modern `"a"`
sets `O_APPEND`, forcing every write to EOF and scrambling an offset-addressed
file. `"r+"` restores the era semantics for the accumulation case (fresh runs
still use `"w"`).

`xsdotc` (skipped under `-c`) then turns the finished `strings` file into C:
`char\txstr[] = {` followed by the pool's bytes as `0x??,` hex, eight per line,
and a closing `};` — the single definition of `xstr[]` every rewritten `x.c`
refers to.

---

## 7. Temp files and interrupt cleanup (`mkstemp`, `onintr`)

The throwaway pool is created with `mkstemp(strings)` (the port's second fix — the
original `mktemp` + open). When `strings` is the literal `"strings"` its template
has no `XXXXXX`, so `mkstemp` fails harmlessly; only the `/tmp/xstrXXXXXX` case
creates a temp, and only it is unlinked on a normal exit (`strings[0] == '/'`).

`onintr` is installed for `SIGINT` only if the signal was at its default
disposition (the standard "don't override an inheriting shell's ignore" idiom). On
interrupt — and on a write error mid-run — it unlinks the temp pool, `x.c`, and
`xs.c` and exits 7, so a killed run leaves nothing half-written behind.

---

## Testing

`xstr` has no in-tree unit harness yet; `make check` reports "no tests yet" until
a `tests/run.sh` exists. It is validated end-to-end instead, the stricter bar: the
tool's reason to exist here is byte-fidelity, and a wrong pool layout shows up at
once as a binary that no longer matches. 2.9BSD shipped binaries built through
`xstr` — `awk` links a pre-extracted `/usr/lib/awk_strings` pool, `/usr/70/rogue`
was built the same way — so the oracle's `cross-universe.sh` runs them under the
simulator (e.g. `awk40 '{ print $1 * $2 }'` on the odd-entry 0430 overlay build),
and any drift in offset assignment or pool layout breaks them.

---

## For a maintainer

- **The layout contract is the offset assignment.** `hashit`/`inithash`/`mesgpt`
  decide where each string lands; `flushsh` writes it there and `xsdotc` reads it
  back. Change any one and the other two must still agree, or a rebuilt binary
  stops matching. The suffix-overlap in `istail` is part of that contract, not an
  optimisation you can drop.
- **Two era fixes are load-bearing** — the `"r+"` accumulation mode (§6) and
  `mkstemp` (§7). The `ignore`/`ignorf` macros only discard return values and
  function pointers without an LP64 cast warning; they are shim, not logic. Offsets
  are `off_t` but printed through `(int)` — an era assumption that a pool fits an int.
- **The Makefile lists `a.out.h`/`ar.h` as deps; `xstr.c` includes neither** — it
  needs only libc, so those inherited template lines can be trimmed harmlessly.
