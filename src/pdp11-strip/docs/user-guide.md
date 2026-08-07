# pdp11-strip — user guide

`pdp11-strip` removes the symbol table and relocation from a PDP-11 `a.out`, in
place — the 2.9BSD `strip`. It reads the header, detects which `a.out` form it is
looking at, drops the symbol table and relocation, and sets the no-relocation
flag. It runs on your host.

For how it works inside — the header poke, the overlay-aware copy, the First
Edition special case, and the hardening — see the [design document](design.md).

---

## 1. Synopsis

```
pdp11-strip [-n] file ...
```

Each `file` is edited in place; several may be given. `-n`, when present, must be
the first argument.

---

## 2. What it removes

The default removes **everything a linker or debugger added and a finished program
does not need to run** — the symbol table and the relocation — and sets the
header's no-relocation flag. What is left is the header (with `a_syms` zeroed), the
text, and the initialized data: smaller, no longer relinkable, but still runnable.

An object that is already stripped — its symbol table gone and its no-relocation
flag already set — is left alone and reported (and counts toward the exit status;
see [§5](#5-exit-status)).

---

## 3. Options

| option | meaning |
|---|---|
| *(none)* | full strip — drop the symbol table and relocation, set the no-relocation flag |
| `-n` | keep **only the name list** (the symbol table); discard text and data |

`-n` is the complement of a plain strip: it writes a header with the text, data,
and bss sizes zeroed and copies out just the symbol table, so you are left with
the name list on its own.

---

## 4. What it reads

The `a.out` form is **detected**, never declared:

- the ordinary magics **`0407`** (normal), **`0410`** (read-only text), and
  **`0411`** (separated I&D) — the common case, stripped by one format-agnostic
  code path that never reads a symbol (see [design §1](design.md));
- the **auto-overlay** magics **`0430`**/**`0431`** — the overlay text segments
  named by the overlay header are preserved; only the trailing symbols and
  relocation are dropped ([design §2](design.md));
- **First Edition** **`0405`**, whose text size includes its 12-byte header
  ([design §3](design.md)).

A file that is not a PDP-11 `a.out` is reported and skipped.

---

## 5. Exit status

- **0** — every file was stripped successfully.
- **nonzero** — the number of files that failed. `strip` returns the count, and a
  file counts as failed if it is not a PDP-11 `a.out`, is **already stripped**, or
  hits an I/O error. (Note that already-stripped is a failure here, not a silent
  success.)
- **75** (`EX_TEMPFAIL`) — a temporary file could not be created.

---

## 6. Examples

```
pdp11-strip prog                  # drop symbols + relocation, in place
pdp11-strip *.o                   # strip many files in place
pdp11-strip -n prog               # keep only the symbol table (the name list)
```

The companion [`pdp11-objcopy`](../../pdp11-objcopy/docs/user-guide.md) does the
raw-image and single-segment extraction that is the other half of an `a.out`
toolkit; stripping symbols is this tool's job, not its.

Continue to the [design document](design.md) for the header rewrite, the
overlay-aware copy, and the hostile-header guards.
