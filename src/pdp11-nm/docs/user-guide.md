# pdp11-nm — user guide

`pdp11-nm` lists the symbol table of a PDP-11 a.out object, executable, or
archive — the 2.9BSD `nm`. It reads the little-endian 16-bit objects this
toolchain produces and meets (2.9BSD objects, First Edition `a.out(V)`, and the
old binary `ar` archive), and prints one line per symbol with the value in
**octal**, in the classic `nm(1)` style. It runs on your host.

For how it works inside — the magic numbers, the symbol-table dialects, the type
letters — see the [design document](design.md).

---

## 1. Synopsis

```
pdp11-nm [-gnopru] [file ...]
```

Each `file` is a PDP-11 object or an archive of them. With no file argument,
`nm` reads `a.out`. Options must be given as **one** combined first argument
(`pdp11-nm -gn foo.o`), the BSD convention — they are not accepted as separate
words or after a file name.

---

## Output

One symbol per line, `value type name`:

```
000172 t L10000
000000 T _main
       U _printf
000010 D _errno
```

- **value** — 6-digit zero-padded **octal** (the symbol's address or offset), or
  **six blanks** for an undefined symbol, which has no value to show;
- **type letter** — what kind of symbol it is (below), upper case for a global
  (external), lower for a local;
- **name** — up to 8 characters.

With more than one file, or for an archive, each listing is headed by the file
(or member) name:

```
libc.a:

printf.o:
000000 T _printf
       U __doprnt
```

The archive's `__.SYMDEF` directory is not a symbol listing of its own — it is
`ranlib`'s index and is skipped automatically.

### Type letters

| letter | meaning |
|---|---|
| `T` / `t` | text (code) |
| `D` / `d` | data |
| `B` / `b` | bss (zero-initialised) |
| `A` / `a` | absolute |
| `R` / `r` | register name |
| `C` | common (uninitialised global) |
| `U` | undefined — a reference to be satisfied elsewhere |
| `F` / `f` | file-name symbol |

Upper case marks a **global** (external) symbol; lower case a local. `C` and `U`
are two faces of the same on-disk type: an undefined entry with a value is a
common block, one without is a plain undefined reference.

---

## Options

| option | meaning |
|---|---|
| `-g` | only **global** (external) symbols |
| `-u` | only **undefined** symbols — prints just their names, no value or letter |
| `-n` | sort by **value** (numeric), not by name |
| `-p` | do **not** sort — symbol-table order |
| `-r` | **reverse** the sort |
| `-o` | prepend the file (and, for an archive, the member) name to every line |

The default sort is alphabetical by name. Flags are combined into the single
leading option word: `pdp11-nm -gn`, `pdp11-nm -ur`. `-r` reverses whichever sort
is in effect (name, or value under `-n`). There is no `-a`, no `-x`, and no octal
switch: values are **always** octal, which is the PDP-11 convention. An unknown
option letter is a usage error.

---

## What it reads

The object format is detected from the header, never declared:

- **2.9BSD a.out**, all six magics — `0407` (normal), `0410` (read-only text),
  `0411` (separated I&D), and the `0430`/`0431` auto-overlay images — with the
  12-byte, fixed-8-character-name symbol table and a single 16-bit type per
  symbol.
- **First Edition `a.out(V)`** (`0405`) — the Research V1 layout, whose header is
  counted inside the text size and whose symbols use the older type encoding.
  `nm` translates that encoding into the same type letters (see
  [design §3.2](design.md)).
- **archives** — the old binary `ar` format (magic `0177545`). Every object
  member is listed and tagged with its member name; the `__.SYMDEF` index is
  skipped.

A file whose first word is none of the a.out magics and is not `ARMAG` is
reported as `bad format` and skipped; the run continues to the next file.

---

## Exit status

| code | when |
|---|---|
| `0` | normal completion — even if some files could not be opened or were not objects (those are per-file diagnostics on stderr, and the run continues) |
| `1` | a usage error — an unknown option letter |
| `2` | out of memory building the symbol array |

Note the direction: a bad or missing *file* does not change the exit status
(`nm` still exits `0`); only a bad *option* is fatal.

---

## Examples

```
pdp11-nm prog                     # symbols of prog, sorted by name, octal values
pdp11-nm -g libc.a                # every member, globals only, member-tagged
pdp11-nm -u main.o                # just the unresolved reference names
pdp11-nm -n prog                  # sort by address instead of name
pdp11-nm -p a.out                 # symbol-table order, unsorted
pdp11-nm -o *.o | grep ' T '      # which object defines each text symbol
```

Continue to the [design document](design.md) for the magic-number handling, the
First Edition path, and the type-letter internals.
