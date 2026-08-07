# pdp11-ar — design

This document describes how `pdp11-ar` is built: the two archive layouts it
reads, the middle-endian on-disk longs it byte-swaps, how it recognises and
walks an archive, the seven operations on one copy-and-rewrite path, positioning,
and the split with `pdp11-ranlib` for symbol tables. For how to *use* it, see the
[user guide](user-guide.md).

The whole tool is one file, `ar.c` (~820 lines), plus the shared format header
`../../common/cross/ar.h`, shared because `pdp11-ranlib` and `pdp11-ld` read the
same libraries and must agree byte for byte on the header layout and the
middle-endian long encoding. This is the archaic V7 binary `ar`, not the later
portable `!<arch>` format: exactly one layout to write (the 26-byte struct
header, magic `0177545`) and one older layout it reads but never writes (First
Edition, `0177555`). It builds `libc.a` for every PDP-11 universe.

---

## 1. The V7 binary format — a struct, not a byte layout

What is on disk is *whatever a PDP-11 compiler laid out* for `struct ar_hdr`,
so the field widths and packing are part of the ABI. `cross/ar.h` pins them for
an LP64 host: magic is the 2-byte word `ARMAG` (`0177545`), and each member
header is exactly **26 bytes**, `__attribute__((packed))` so the host compiler
inserts none of its own alignment:

```
 0  char    ar_name[14]
14  int32_t ar_date
18  char    ar_uid
19  char    ar_gid
20  int16_t ar_mode
22  int32_t ar_size   -- 26 bytes
```

The two `int32_t` fields are the hard part. The PDP-11 stores a 32-bit `long`
**high-16-bit-word first** — its two little-endian halves swapped relative to a
host long. `ar.h`'s `PDPL()` macro swaps them and is its own inverse, so read
and write share it: `getdir()` applies it to `ar_date`/`ar_size` on the way in,
`copyfil()` to a *copy* of the header on the way out (leaving `arbuf` in host
order for the size-driven copy loop). Getting this right is what makes the
archives byte-identical in layout to authentic 2BSD `ar`.

---

## 2. The First Edition format — read-only

The reader also decodes the older First Edition layout (`OARMAG`, `0177555`;
Research V1–V4), which predates V7: a 16-byte per-member header with an 8-char
name, a middle-endian `long` date, one uid byte, one mode byte, and the member
size as a **little-endian word** (members are even-padded on disk). `getdir()`
unpacks that into the same `arbuf` the V7 path fills, so `t`/`p`/`x` work
unchanged. Two deliberate simplifications: the V1 mode byte is a two-class
(owner/non-owner) model, not POSIX `rwxrwxrwx`, so extraction forces a sane
`0644`; and there is no `__.SYMDEF`.

We never *write* this layout — our archives are modern build-time containers, so
any modifying op on an old-format archive hits `cantmod()`, which refuses it
rather than rewriting it as V7.

---

## 3. Detection and the member walk

Detection is one read of the 2-byte magic. `getaf()` opens the archive, reads
the magic into an `unsigned short` (so the high-bit-set `!= ARMAG` compare is
unsigned), accepts `ARMAG` or `OARMAG` (setting `oldfmt` for the latter), and
refuses anything else as "not in archive format". `getqf()` is the `q` variant,
opening read-write and creating the archive if absent.

`getdir()` is the iterator every command drives: one call reads and normalises
the next header into `arbuf` (swapping the PDP longs, or unpacking the 16-byte
old header) and returns 0, or 1 at end of file. `match()` compares a wanted name
to the member by **basename** (`trim()` strips path and trailing slashes) and
clears it from the request list once matched, so `notfound()` can report the
leftovers as the exit status.

---

## 4. The operations — one copy-and-rewrite path

The key letters are fixed in two strings: `man = "mrxtdpq"` (the operations) and
`opt = "uvnbail"` (the modifiers, plus `c` handled but not advertised). `main()`
scans the first word letter by letter, calling `setcom()` for an operation
(exactly one allowed) and setting `flg[]` for a modifier.

Read-only ops walk with `getdir()` and act per member:

| op | body |
|---|---|
| `t` | print the trimmed name; `tv` calls `longt()` for an `ls -l`-style line |
| `p` | copy the member's bytes to stdout (fd 1); `pv` prints a `<name>` banner |
| `x` | `creat()` the file with `ar_mode & 0777` and copy the bytes out |

Modifying ops (`r`/`d`/`m`) share the *stream-through-a-temp* shape: `init()`
`mkstemp()`s a temp file and writes the magic, then the walk copies each member
either **into the temp** (`copyfil(af, tf, …)`) or **skips it** (`copyfil(af,
-1, … + SKIP)`), and finally `install()` (or `cleanup()` for `r`) rewrites the
real archive from the temp(s):

- `r` (`rcmd`) — replace/merge: a file whose basename matches a member is
  skipped from the copy and re-inserted from disk in place (under `u`, only if
  the file's mtime is newer than the member's `ar_date`); unmatched request
  files are appended by `cleanup()` at the end (or at the `a`/`b`/`i` position);
  a nonexistent archive is created.
- `d` (`dcmd`) — delete: matched members are skipped, the rest copied through.
- `m` (`mcmd`) — move: matched members are diverted to a *second* temp (`tf2`),
  and `install()` concatenates `tf` + `tf2` + `tf1`, so the moved members land
  as a block at the reposition point.
- `q` (`qcmd`) — quick-append: `getqf()` opens the archive read-write, seeks to
  the end, and appends each file's header+data with no duplicate check; `abi`
  positioning is rejected. This is the only op that does not rebuild the file.

`copyfil()` is the one byte mover; its `flag` bits are `HEAD` (emit the 26-byte
header, `PDPL()` applied to `ar_date`/`ar_size`), `IODD`/`OODD` (round the odd
final byte up on input/output — members are even-padded on disk), and `SKIP`
(read past a member without writing). `movefil()` fills `arbuf` from a host
`fstat()` and calls it to lay a new member down. `install()` re-`creat()`s the
archive `0666` and concatenates `tf` + `tf2` + `tf1`; every modifying op
therefore rewrites the whole file.

---

## 5. Positioning — `a` / `b` / `i`

`a` (after), `b` (before), and `i` (a synonym for `b`) take a **position name**
as the first argument after the key (`main()` lifts it into `ponam`).
`bamatch()` runs a two-state splice: when the walk reaches `ponam` it opens a
*holding* temp (`tf1`) and redirects the copy into it, so newly added members
land before (or after, for `a`) the named one — `install()`'s `tf`+`tf2`+`tf1`
concatenation re-joins the halves in the right order.

---

## 6. Symbol tables belong to `pdp11-ranlib`

`pdp11-ar` does not compute symbol tables — that needs a.out/`nlist` knowledge,
which lives in [`pdp11-ranlib`](../../pdp11-ranlib/). The call-out runs the
historical PDP-11 direction: **`ranlib` drives `ar`**, invoking `ar rlb
<firstmember> <archive> __.SYMDEF` to splice `__.SYMDEF` in as the first member.
The `l` modifier it passes is why `ar` honours `l`: it makes the temp files
local (`arXXXXXX` in the working directory) instead of `/tmp/arXXXXXX`.

---

## 7. Safety and signals

`SIGHUP`/`SIGINT`/`SIGQUIT` are caught (`sigdone` → `done(100)`) so an
interrupted run still unlinks its temp files; `q` blocks them around the
in-place append. Temp files are `mkstemp()`'d (create-and-open, `0600`), never
`mktemp()`'d into a predictable name. A header that does not read as a full 26
(or 16) bytes ends the walk rather than being guessed at, and a non-archive
input is refused outright.

---

## Testing

Two layers. `tests/ld/ranlib.sh` (via `make check`) builds a deliberately
**mis-ordered** archive and confirms a single-pass `ld` needs `pdp11-ranlib`'s
`__.SYMDEF` to resolve it. The weight is the **oracle**: `oracle/lib-sweep.sh`
walks every `*.a` in a real 2.9BSD tree, extracts its members, zeroes their
mtimes (apsim's `stat(2)` reports mtime 0, the only field that would otherwise
differ), feeds the *identical* files to both `pdp11-ar` and the **native
`/bin/ar` under apsim**, and `cmp`s the two archives byte for byte. Proven:
**39/39** libraries match, zero diffs — the layout is authentic, not plausible.

---

## For a maintainer

- **One format model** (`cross/ar.h`), shared with `ranlib` and `ld`: the packed
  26-byte header and the `PDPL()` middle-endian swap. Change a width or the swap
  here, and rebuild all three.
- **Read path:** `getaf()` (magic → `oldfmt`) → `getdir()` (read + normalise one
  header) → `match()`/`trim()`. Add a read command as a walk body.
- **Write path:** everything funnels through `copyfil()` (`HEAD`/`IODD`/`OODD`/
  `SKIP`) into a `mkstemp()` temp, then `install()`/`cleanup()` rewrites the
  archive; `q` is the lone in-place append. Reproduce on-disk bytes only here.
- **First Edition is read-only** — `cantmod()` refuses every modifying op; do not
  add a writer for `0177555`.
- **Symbol tables belong to `pdp11-ranlib`**, which calls *this* tool (`ar rlb`),
  not the reverse; do not grow a symbol engine here.
- Hold any change to byte-for-byte parity with `oracle/lib-sweep.sh` (native
  `ar` under apsim) before trusting it.
