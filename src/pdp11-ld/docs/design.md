# pdp11-ld — design

This document describes how `pdp11-ld` links classic 2BSD a.out objects into a
runnable image: the two passes it makes over its inputs, the word-at-a-time I/O
it inherited from the PDP-11 and how that survives on an LP64 host, how it
resolves symbols across objects and archive members, how it lays out and
relocates the segments, and the overlay machinery behind its `0430`/`0431`
auto-overlay executables. For how to *use* it, see the [user guide](user-guide.md).

The tool is one file, `ld.c` (~1750 lines), descended from the 2.9BSD MENLO link
editor — the header still credits wnj's 6/79 segmentation-register loader and
Bill Jolitz's 5/80 EMT-thunk work. It runs on your host and emits a PDP-11 a.out;
the LP64 port is concentrated at the file-I/O boundary (§2) and written up in
[porting.md](porting.md).

---

## 1. Two passes and the a.out header

`ld` reads its whole argument list twice. **Pass 1** (`main` → `load1arg` →
`load1`) scans every object and archive to build the global symbol table and the
cumulative segment sizes, pulling in an archive member only if it defines a
currently-undefined symbol — so the symbol table at the end of pass 1 is the
transitive closure of what the program references. `middle()` then assigns final
addresses (§4), and **pass 2** (`endload` → `load2arg` → `load2`) re-reads the
same inputs in the same order, relocating each text and data word against those
addresses and writing the segments out (§5).

The on-disk a.out header is the classic **8 words / 16 bytes** — `struct filhdr`
is eight `uint16_t`: magic, the three segment sizes, symbol-table size, entry
point, a pad word, and the relocation-stripped flag. Its four magics are `0405`
(OMAGIC), `0407` (FMAGIC, the default), `0410` (NMAGIC, pure text) and `0411`
(IMAGIC, split I/D).

---

## 2. Reading and writing on-disk words

Everything on disk — the a.out header, each symbol, the archive member header,
the `__.SYMDEF` table — is a stream of 16-bit PDP-11 words, and `ld` reads and
writes it word-by-word through two routines:

```c
mget(&filhdr, sizeof filhdr);      /* pull sizeof/2 words into a struct  */
mput(&toutb, &filhdr, sizeof filhdr); /* push them to an output buffer   */
```

Both take a `void *`, so callers hand them a packed struct address **uncast**
(`&archdr`, `&cursym`, `&filhdr`) and `mget`/`mput` walk it as `uint16_t`. That
is the whole LP64 port: on the PDP-11 `int` was two bytes and these copies used
`int` buffers; here every buffer and on-disk field is pinned to `uint16_t` so a
`read(fd, buf, 512)` still fills exactly 256 words and `sizeof(struct)>>1` still
counts words. The structs (`filhdr`, the 12-byte `symbol`, the 26-byte V7
`archdr`) are `__attribute__((packed))` where host alignment would otherwise
pad. Zero-extension matters too: `get()` returns a `uint16_t` promoted to `int`,
so `ARCMAGIC` (`0177545`) compares equal after a read where a signed `short`
would sign-extend and break the magic test. On-disk archive `long`s are
**PDP-11 middle-endian**: member size and time (`archdr.asize`, `archdr.atime`)
and each `__.SYMDEF` offset (`tab[i].cloc`) are passed through `PDPL()` (from
`<ar.h>`) the moment they are read, so the values `ld` computes with are exactly
what the native tools wrote.

---

## 3. Pass 1 — resolving symbols and pulling in libraries

`getfile()` opens each argument and classifies it by its first word: a plain
object (return 0), a plain archive (1), an archive with an up-to-date
`__.SYMDEF` directory (2), or one whose directory is stale (3). `load1arg`
dispatches on that:

- **object** — `load1(0, …)` enters its externals.
- **plain archive** — `step()` walks every member, entering the ones that
  resolve something.
- **`__.SYMDEF` archive** — the fast path: the directory is read into `tab[]` and
  `ldrand()` sweeps it repeatedly, pulling in (`step`) any member that defines a
  still-`EXTERN+UNDEF` symbol until a sweep adds nothing new — which is why
  archive *order* rarely matters for a ranlib'd library.
- **stale directory** — a warning, then the plain-archive walk.

Each referenced member's file offset is recorded in `liblist[]` so pass 2 can
find it again. Two bounds are hardened against a malformed archive. The
fast-load count `tnum = archdr.asize / sizeof(struct tab)` is now rejected when
**`< 0 || >= TABSZ`**: a crafted `__.SYMDEF` with bit 31 set yields a negative
`tnum` that the original signed `>= TABSZ` guard let through, after which
`read()` overran `tab[TABSZ]`. And every append to `liblist[NROUT]` first checks
`libp >= &liblist[NROUT]`, so a library referencing more than `NROUT` members can
no longer walk off the array.

`load1` enters each external through the hash table (`lookup`/`enter`), promoting
`EXTERN+UNDEF` to a definition when it meets one, keeping the larger of two common
sizes, and flagging a real multiply-defined case; a member that defines nothing
new has its tentative entries rolled straight back out.

---

## 4. The middle pass — placement

`middle()` turns sizes into addresses. Text starts at `0` (or at `040014` for
the First Edition universes, whose `0405` image loads whole at core `040000`
behind a 12-byte header). Data follows text, bss follows data, and common blocks
are assigned into bss; under `-n`/`-i` the text is rounded up to a `0100` page
first. On a final link (`rflag == 0`) `ld` stamps the active universe as an absolute
symbol **`__univ`** via `ldrsym(…, EXTERN+ABS)` — but only if it is referenced
(by `crt0`), and *before* the undefined-symbol scan, so a referenced `__univ` is
already defined and not mistaken for a straggler. `p11_univ_id()` maps
`$PDP11_UNIVERSE` (or the default) through `universe.h` to the id stamped,
letting one universal `libc.a` branch on the era chosen at link time. That
undefined-symbol scan then decides the output's fate: if any external is still
undefined (and this is not an overlay link), `ld` forces `-r` so the result
stays relocatable rather than pretending to be runnable.

---

## 5. Pass 2 — relocation and output

`load2` re-reads each input's symbol table to number its locals (for external
references), then streams its text and data through `load2td`, which reads a text
word and its relocation word in lockstep. The relocation word's low bits select
what to add:

```
r & 016:  RTEXT -> += ctrel   RDATA -> += cdrel   RBSS -> += cbrel
          REXT  -> resolve the external symbol, add its final value
r & 01:   pc-relative — subtract the current segment origin
```

An external still undefined is re-emitted as a relocation against the output's
symbol table (for a `-r` relink); a resolved one is folded into the word. Under
`-r` the (possibly rewritten) relocation word is written alongside the datum.

The segments are built in **temporary files**, one per stream. Each is created
by a fresh `mkstemp()` on `/tmp/ldaXXXXXX` — created and opened `0600`
atomically, then immediately **`unlink`ed**, so the file lives on only through
its fd: no predictable-name `creat`/`symlink` race, nothing to clean up on exit.
`finishout()` then copies the temp segments into the output in header order and
appends the symbol table.

---

## 6. Overlays

`ld` carries two overlay mechanisms, both live and unconditional in the 2.9
source. **`-v name` — the overlay tree.** Each `-v` names a node; `record()` snapshots the
symbol-table index, `liblist` cursor, and segment sizes there, and if a name
recurs `restore()` rewinds to it — so sibling overlays share everything before
the node and are laid down independently after it (up to `NOVLY` = 16).

**`-Z … -L` — segmentation-register text overlays.** Objects bracketed by a
`-Z`/`-L` pair form one text overlay. Their text is relocated to a common
overlay base `ovbase` (rounded above the resident text), and for every overlaid
entry point `finishout()` plants an **8-byte JSR thunk** (`THUNKSIZ`) in the
resident text — `mov $addr+4,r1; jsr r5,ovhndlrN` — that calls the kernel
overlay handler for that overlay number. Up to `NOVL` (7) overlays; each is
padded to a `0100` boundary and their sizes written as an 8-word table right
after the main header. The output magic gets `|= 020`, turning `0410`→**`0430`**
(pure) and `0411`→**`0431`** (split I/D); an overlay link therefore *requires*
`-n` or `-i`.

`roundov()` does the per-overlay padding, advancing `torigin` by one PDP-11 word
(**2**) per fill word — the LP64 fix that matters: the original increment stepped
by a host `int`, and at `sizeof(int)==4` skipped the `& 077` alignment point so
the loop never terminated. Pinning the step to a 16-bit word makes overlay links
finish.

---

## 7. Finding libraries

`-lx` becomes `lib<x>.a`, resolved **relative to the `ld` binary** rather than a
hard-coded `/usr/lib`. `getfile()` reads `/proc/self/exe`, strips a trailing
`bin/` to find the install root, and looks first in the era subdirectory
`lib/<universe>/` (the universe from `$PDP11_UNIVERSE`, default from
`universe.h`), falling back to a flat `lib/`. The path is assembled with
`strcpy`/`strcat` into fixed buffers rather than `sprintf`, whose size bound
cannot be proved for a cyclic buffer chain fed by the unbounded `-l` name.

---

## 8. Testing

`tests/ld/link.sh` assembles and links with `pdp11-as`/`pdp11-ld` and inspects
the a.out by reading its bytes (`od`/`dd`, no external tools) — a two-object case
asserts an external `jsr` operand is relocated to the defining symbol's final
offset, cross-checked against the GNU `pdp11-aout-objdump` oracle. The two
standing oracles are stronger: **kernel-link** links the GENERIC 2.9 kernel and
requires the resulting `unix` to be **byte-identical** to the native link —
116854 bytes, a `0430` overlay image — exercising the whole overlay path
(`-Z`/`-L`, thunks, `roundov`, the size table); **ld-sweep** links 200+ programs
and requires `ld` to match native `ld` on every one (213 clean links, 0 differ).
Verifying cross-object relocation this way also caught two *assembler* bugs that
`ld` was right to reject — see [porting.md](porting.md).

---

## 9. For a maintainer

- **The I/O boundary is the whole port.** Every on-disk word is a `uint16_t` and
  every raw copy goes through `mget`/`mput` (`void *`, no cast at the call site);
  a new on-disk field must be fixed-width and keep the struct a whole number of
  words (`packed` if the host would pad it).
- **`putw` is renamed `ld_putw`** by a `#define` at the top: `ld` has its own
  `putw(word, struct buf *)` and the host `<stdio.h>` declares `putw(int,
  FILE *)`. Do not remove the shim.
- **Overlays are load-bearing** — `-Z`/`-L`, the thunks, `roundov` (its step must
  stay `+= 2`), and the `0430`/`0431` magic are what the kernel-link oracle needs.
- **Hold the output to `cmp`** against native `ld` (ld-sweep) and a byte-identical
  kernel (kernel-link) — that catches a wrong relocation or a misplaced segment.
- **Bounds are deliberate.** `tnum < 0`, the `liblist[NROUT]` checks, and the
  `mkstemp`+`unlink` temp files guard a hostile or oversized archive; keep them.
