# pdp11-ar — user guide

`pdp11-ar` is the archive tool (`ar`) for this PDP-11 cross-toolchain. It builds
and edits `.a` libraries of PDP-11 objects in the authentic V7 binary format
(magic `0177545`, 26-byte member headers) that this toolchain's `pdp11-ld`
reads — the format `libc.a` is built in for every PDP-11 universe. It is a host
program: it runs on your Linux box.

If you have used the classic `ar(1)` before it will feel identical — the
key-plus-modifiers command line is preserved exactly. It also *reads* (but never
writes) the older First Edition archive format.

For how it works inside, see the [design document](design.md).

---

## 1. Synopsis

```
pdp11-ar key [posname] archive [member ...]
```

The **key** is the first word: exactly one operation letter, with any modifiers
glued onto it the way `ar` has always taken them — `tv`, `ru`, `rc`, `ma` (the
tool's own usage line is `ar [uvnbail][mrxtdpq] archive files ...`). A leading
dash is tolerated but not required. `posname` is present only with the
`a`/`b`/`i` positioning modifiers, and names the member to position relative to.

---

## 2. Key letters

Exactly one is required (`man = "mrxtdpq"`):

| key | does |
|---|---|
| `r` | **replace / merge** — add each file, replacing any same-named member in place; creates the archive if it does not exist |
| `d` | **delete** the named members |
| `q` | **quick-append** the files to the end, no duplicate check (creates the archive if absent) |
| `t` | **table** of contents — list member names, one per line |
| `p` | **print** the named members' contents to standard output |
| `m` | **move** the named members within the archive (to the `a`/`b`/`i` position, else the end) |
| `x` | **extract** the named members as files in the current directory |

For `t`/`p`/`x`, naming no members means *all of them*. Names are matched by
**basename**, so `x lib.a printf.o` finds `printf.o` however its path was stored.

`r` merges rather than starting over: a file whose basename matches an existing
member replaces it in place (keeping its position); a file with no match is
added at the `a`/`b`/`i` position if given, else at the end; with `u` a member
is replaced only if the incoming file is newer. `q` instead appends to the end
with no name check — so it can leave duplicates — and is the only op that does
not rewrite the whole file (`a`/`b`/`i` are not allowed with it).

---

## 3. Modifiers

Glued onto the key (`opt = "uvnbail"`, plus `c`):

| mod | with | meaning |
|---|---|---|
| `v` | any | **verbose** — name each member as it is added/deleted/extracted; `tv` gives an `ls -l`-style long listing |
| `u` | `r` | **update** — replace a member only if the incoming file is *newer* than the stored member |
| `c` | `r`/`q` | **create quietly** — suppress the "creating archive" notice when the archive does not yet exist |
| `a` | `r`/`m` | position **after** `posname` |
| `b`, `i` | `r`/`m` | position **before** `posname` (`i` is a synonym for `b`) |
| `l` | any | put temp files in the **current directory** (`arXXXXXX`) instead of `/tmp`; this is the flag `pdp11-ranlib` passes as `ar rlb` |
| `n` | any | accepted for compatibility with the vintage `ar`; no effect |

`a`/`b`/`i` take the **position name** as the first argument after the archive:
`pdp11-ar rb strlen.o lib.a strcpy.o` inserts `strcpy.o` just before `strlen.o`.

---

## 4. Symbol tables and `pdp11-ranlib`

`pdp11-ar` does not build a symbol table — that is
[`pdp11-ranlib`](../../pdp11-ranlib/)'s job, as on the PDP-11 UNIX line. After
building or editing a library, run `pdp11-ranlib libc.a` to add the `__.SYMDEF`
table of contents so `pdp11-ld` resolves members regardless of order; the
call-out runs `ranlib` → `ar rlb …` (which is why `ar` honours the `l`
local-temp modifier). Without an index a library still links, but `ld` scans it
front to back, so **member order is load-bearing** for an un-`ranlib`'d archive
(`m` exists for that reason).

---

## 5. Reading a First Edition archive

`pdp11-ar` transparently reads the older First Edition format (magic `0177555`,
16-byte headers) for `t`/`p`/`x`, but will **not modify** one: `r`/`d`/`q`/`m`
on such an archive is refused (`ar: … is a First Edition (V1/V2) archive;
read-only (t/p/x)`), because this toolchain's own archives are the modern V7
container, not that layout.

---

## 6. Exit status

- **0** — success, all named members found.
- **N** — for a modifying op, the number of named members that were **not
  found** in the archive (each reported as `ar: NAME not found`), so a script
  can rely on the count.
- **1** — an error: a bad key/modifier, a non-archive or unreadable input, a
  modify attempt on a First Edition archive, a write error, or a temp-file
  failure. A one-line `ar: …` message says what.
- **100** — interrupted by a signal (temp files are cleaned up first).

---

## 7. Examples

```
# Build a library, then index it (two steps: ar does not index)
pdp11-ar rc libc.a *.o
pdp11-ranlib libc.a

# Quick-append while building from scratch (fast, no dup check)
pdp11-ar cq libc.a printf.o scanf.o

# Replace one object; update only if newer; long listing
pdp11-ar r  libc.a printf.o
pdp11-ar ru libc.a *.o
pdp11-ar tv libc.a

# Insert before / move to just after a named member
pdp11-ar rb strlen.o libc.a strcpy.o
pdp11-ar ma flsbuf.o libc.a exit.o cleanup.o

# Delete a member (verbose), then re-index
pdp11-ar dv libc.a stale.o && pdp11-ranlib libc.a

# Extract everything into a fresh directory
mkdir unpack && cd unpack && pdp11-ar x ../libc.a
```

Continue to the [design document](design.md) for the on-disk layout and the
internals.
