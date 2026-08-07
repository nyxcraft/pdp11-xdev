# pdp11-ranlib — user guide

`pdp11-ranlib` builds an archive's `__.SYMDEF` **symbol directory** — the table
of contents that lets `pdp11-ld` find which member of a `.a` library defines a
given symbol without scanning every member in order. It is the companion of
[`pdp11-ar`](../../pdp11-ar/), which deliberately does *not* build one: on the
PDP-11 UNIX line that has always been `ranlib`'s job. It runs on your host and
rewrites a library of PDP-11 objects in place.

For how it works inside — the `__.SYMDEF` layout, the a.out symbol reader, the
offset arithmetic, and the safe `ar` call-out — see the
[design document](design.md).

---

## 1. Synopsis

```
pdp11-ranlib archive ...
```

Point it at one or more `.a` files. For each, it reads the exported symbols from
the object members, writes a `__.SYMDEF` directory, and asks `pdp11-ar` to
splice that in as the archive's first member. Running it again is a rebuild, not
an accumulation: an existing `__.SYMDEF` is replaced in place.

---

## 2. Options

There are none — every argument is an archive name, exactly as the vintage
V7/2.9BSD `ranlib` behaved. It takes no flags, no format selector, and no
verbosity control; the format is always the authentic `__.SYMDEF` and there is
nothing to choose. (This differs from later `ranlib`s and from the sibling VAX
toolchain, which grew `-t`/`-v`.)

---

## 3. What it does, and when you need it

A library links faster — and, when its members are not dependency-ordered,
links *correctly* — with a symbol directory. `pdp11-ld` otherwise scans a plain
archive in a single front-to-back pass, so a member whose definition sits before
the member that references it is missed. `__.SYMDEF` removes that ordering
requirement.

You need to run it after **building or changing** a library:

```
pdp11-ar rc libc.a *.o        # build the archive (ar does NOT index)
pdp11-ranlib libc.a           # add __.SYMDEF so ld resolves in any order
```

`pdp11-ar` never maintains the index itself — there is no `s`-modifier shortcut
on this line — so a plain `ar` edit leaves the old `__.SYMDEF` stale (or absent)
and you re-run `pdp11-ranlib`. It finds the matching `ar` next to its own binary
(via `/proc/self/exe`), so the two always pair up.

---

## 4. What gets indexed

A symbol is entered in the directory if it is **external and defined in this
member**:

- an **external, defined** symbol (a function or global the member provides) —
  indexed;
- an **external undefined** symbol — *not* indexed: it is a *reference*, and
  indexing it would make `ld` pull in a member that cannot satisfy it;
- a **local / debug** symbol — not indexed.

This is the classic V7/2BSD `ranlib` rule. Symbol names are stored **inline in 8
bytes** and truncated past that, as the format has always done. `pdp11-ranlib`
reads the symbols from real PDP-11 **a.out** objects; a member whose magic is not
a recognised a.out magic (`A_MAGIC1..4`) contributes nothing and is skipped
harmlessly. A single archive may hold at most 700 indexed symbols.

---

## 5. Determinism and the stamped date

`__.SYMDEF` carries a date, and `ld` treats the table as stale — falling back to
a single-pass scan — unless that date is at least the archive's modification
time. `pdp11-ranlib` writes a fixed **far-future** date so the table is always
honoured regardless of host filesystem clock skew. Because the date is a
constant (not `time()`), the tool's output is **byte-identical run to run** on
the same input — no environment variable is needed to make it reproducible.

---

## 6. Exit status

- **0** — normal completion. This includes archives it skipped and reported: a
  file it could not open (`nm: cannot open …`) or one that is not an archive
  (`not archive: …`) is announced and the next argument is processed.
- **1** — a fatal error: it could not create the temporary `__.SYMDEF`, or the
  archive has more than 700 external symbols (`symbol table overflow`).

If `pdp11-ar` cannot be run to splice the member in, it prints `can't run …` and
moves on; the exit status still reflects only the fatal cases above.

---

## 7. Notes per universe

- **V7, 2BSD, 2.9BSD** all shipped this `ranlib` and the `__.SYMDEF` directory —
  the pair is byte-identical across the PDP-11 line, and this is its native
  ground. `libc.a` for each is built with `pdp11-ar` and indexed here.
- The pairing is the historical PDP-11 one: `ranlib` drives `ar` (`ar rlb …`),
  the reverse of later toolchains where `ar` drives `ranlib`.

---

## 8. Examples

```
# Index a freshly built library
pdp11-ranlib libc.a

# Full build: archive then index (two steps -- ar does not index)
pdp11-ar rc libc.a *.o && pdp11-ranlib libc.a

# Re-index after editing a library
pdp11-ar r libc.a printf.o && pdp11-ranlib libc.a

# Re-index several libraries at once
pdp11-ranlib lib*.a
```

Continue to the [design document](design.md) for the directory layout and the
internals.
