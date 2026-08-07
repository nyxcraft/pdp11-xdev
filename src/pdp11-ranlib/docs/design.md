# pdp11-ranlib — design

This document describes how `pdp11-ranlib` builds an archive's `__.SYMDEF`
symbol directory: the on-disk table it writes, how it reads symbols out of the
a.out members, the offset arithmetic that keeps the directory pointing at the
right members after insertion, how it resolves and safely invokes this
toolchain's own `ar`, and the date it stamps. For how to *use* it, see the
[user guide](user-guide.md).

The tool is one file, `ranlib.c` (~250 lines), sharing the archive-format model
with [`pdp11-ar`](../../pdp11-ar/) through `../../common/cross/ar.h` (the packed
26-byte header and the `PDPL()` middle-endian swap) and reading object symbols
through `../../common/cross/a.out.h`, so each kind of knowledge lives in one
place and all three tools agree on the bytes.

---

## 1. The job

`pdp11-ld` resolving a library either scans every member's symbol table in turn
(which only works if the members are dependency-ordered) or consults a **symbol
directory** at the front mapping each exported symbol to the member that defines
it. The directory — a member named `__.SYMDEF`, placed first — is what lets `ld`
pull members regardless of order. `pdp11-ranlib` builds it; `pdp11-ar` does not.

---

## 2. The `__.SYMDEF` layout (byte-exact)

The directory is an array of fixed-width entries, one per exported symbol,
written to a temp file that is then spliced into the archive as its first
member. Each entry is a `struct tab`:

```c
struct tab {
    char    cname[8];   /* symbol name, inline, blank/NUL-filled */
    int32_t cloc;       /* byte offset of the DEFINING member's header */
} __attribute__((packed));      /* exactly 12 bytes */
```

Two details are load-bearing and were the porting fixes:

- **12 bytes, not 16.** On an LP64 host a bare `long` would make the entry 16
  bytes; it is pinned to `int32_t` and packed to 12. `ld`'s own reader computes
  `tnum = symdef_size / 12` and `step(offset >> 1)` (byte → word), so the width
  must match to the byte.
- **`cloc` is a PDP-11 middle-endian long**, written through the same `PDPL()`
  half-swap `ar` and `ld` use (high 16-bit word first on disk); the whole table
  is swapped in a final pass before it is written.

The name is carried **inline** in 8 bytes — no separate string table — and names
longer than 8 characters are truncated, exactly as V7/2BSD `ranlib` did.

---

## 3. Reading a member's symbols

`main()` walks the archive with `nextel()`, which reads each 26-byte header
(swapping `ar_size`/`ar_date` to host order via `PDPL()`), rounds an odd size up
to the even on-disk boundary, and records `oldoff` — the byte offset of *this*
member's header, which the directory will point at. For each member it reads the
a.out `struct exec`, skips anything whose magic is not `A_MAGIC1..A_MAGIC4`
(`BADMAG` — a non-object member), seeks past text and data (`o = a_text +
a_data`, doubled when reloc info is present, i.e. `(a_flag & 01) == 0`), and
reads the `n = a_syms / sizeof(struct nlist)` symbols that follow.

---

## 4. What counts — the qualification

```c
if ((sym.n_type & N_EXT) == 0)           continue;   /* not external */
switch (sym.n_type & N_TYPE) {
case N_UNDF:                             continue;   /* undefined: a reference */
default:                                 stash(&sym);
}
```

A symbol is indexed only if it is **external and defined** — `N_EXT` set and its
type not `N_UNDF`. A plain undefined entry is a *reference*; indexing it would
make `ld` pull a member that satisfies nothing. This is the V7/2BSD `ranlib`
rule verbatim. `stash()` copies the 8-byte name and the member's header offset
(`oldoff`) into `tab[]` (capacity `TABSZ` = 700; overflow is fatal).

---

## 5. The offset that moves when the directory is inserted

Every `cloc` names a member by its offset **in the finished archive** — but
inserting `__.SYMDEF` first is what shifts every following member forward.
`fixsize()` closes the loop: because every entry is fixed width, the directory's
length depends only on the symbol *count*, so it can be computed before
placement:

```
offdelta = tnum * sizeof(struct tab) + sizeof(arp)   /* directory + its 26-byte header */
```

It then rewinds to the first real member to decide the mode: with **no existing
`__.SYMDEF`** (`new = 1`) the members all shift by `offdelta` and the first
member's name is saved in `firstname` so the directory can be inserted *before*
it; when an **`__.SYMDEF` is already first** (`new = 0`, a rebuild) `offdelta` is
reduced by the old directory's total size so the members do not move, only
re-index. Every `cloc` is then adjusted by `offdelta` and the table written.

---

## 6. Handing the splice to `pdp11-ar` — safely

`pdp11-ranlib` does not edit the archive itself; it writes `__.SYMDEF` to a temp
file and asks `ar` to insert it — the historical PDP-11 division of labour, in
which `ranlib` drives `ar` (the reverse of the VAX toolchain). Two things make
it robust.

**Resolving the matching `ar`.** `setup_ar()` reads `/proc/self/exe` and rewrites
the trailing `ranlib` in its own path to `ar`, preserving any toolchain prefix —
so `.../usr/bin/pdp11-bsd29-ranlib` invokes `.../usr/bin/pdp11-bsd29-ar` (the
same `/proc/self/exe` scheme `cc` and `ld` use), never a stray system `ar`; it
falls back to a bare `ar` on `PATH` if the readlink fails.

**fork/exec, not `system()`.**

```c
av[n++] = arcmd;
av[n++] = first ? "rlb" : "rl";   /* rlb <firstname> to insert before it; rl to replace */
if (first) av[n++] = first;
av[n++] = archive;  av[n++] = temp;
if ((pid = fork()) == 0) { execvp(arcmd, av); _exit(127); }
```

`run_ar()` builds an **argv array and `fork`/`execvp`s** it — it does **not** go
through `system()`/`/bin/sh`. This is a deliberate security fix: the `firstname`
argument comes straight from an on-disk `ar_name`, so a crafted member name in
an untrusted archive would otherwise be read as shell text and could inject
commands; a single argv element makes that impossible. (The *native* 2.9BSD
`ranlib` did `system("ar rlb …")` via `/bin/sh`; this port keeps the
byte-identical output while closing the hole.) The `rlb`/`rl` choice mirrors §5:
`ar rlb <firstname>` inserts `__.SYMDEF` *before* the old first member for a new
index; `ar rl` replaces the existing one for a rebuild.

---

## 7. The date it stamps

After `ar` inserts the member, `fixdate()` reopens the archive and overwrites
the `__.SYMDEF` header's `ar_date` directly. `ld` treats the table as stale —
falling back to a single-pass scan — unless that date is at least the archive's
mtime. The native tool used `time()+5`; on a host, where clock skew is possible,
this writes a **far-future** date (`PDPL(0x7fffffff)`) so the table is always
honoured, and exactly **4 bytes** (the on-disk `ar_date`, not the host's 8-byte
long).

---

## 8. Testing

Two layers. `tests/ld/ranlib.sh` (via `make check`) builds a deliberately
**mis-ordered** archive — `_helper` defined in a member placed *before* the one
that references it — runs `pdp11-ranlib`, checks `__.SYMDEF` is now first, and
requires `ld` to resolve `_helper` through it.

The weight is the **oracle**: `oracle/lib-sweep.sh` runs the native 2.9BSD
`ranlib` **end-to-end under apsim** (it forks `/bin/sh` to `system("ar rlb")`,
so the shell and native `ar` run under the simulator too) over every `.a` in the
tree, and byte-compares the `__.SYMDEF` **member content** against ours. Only
the header date differs (native stamps `time()+5`), so the comparison drops the
header and checks names, offsets, and layout. Proven: **26/26** ranlib'd
libraries match to the byte.

---

## 9. For a maintainer

- **Formats live in `cross/ar.h`** (packed header + `PDPL()`, shared with
  `ar`/`ld`) and object symbols in `cross/a.out.h` (shared with `nm`/`size`).
  Don't grow either here.
- **Walk → qualify → index → splice:** `nextel()` → the external-and-defined
  filter → `stash()` → `fixsize()` (offset delta) → `run_ar()`.
- **The entry is 12 bytes and its offset is `PDPL()`-swapped** — `ld` divides by
  12 and shifts byte→word, so both are pinned by the on-disk contract.
- **`run_ar()` must stay `fork`/`execvp`, never `system()`** — the member name it
  passes is untrusted; do not reintroduce a shell.
- **The date must be `>=` the archive mtime**, written as 4 bytes; the
  far-future value guards against host clock skew.
- Hold byte-layout changes against `oracle/lib-sweep.sh` and `tests/ld/ranlib.sh`.
