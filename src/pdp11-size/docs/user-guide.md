# pdp11-size — user guide

`pdp11-size` prints the segment sizes — text, data, and bss — of a PDP-11 a.out
file, together with their total in both decimal and octal. It is the 2.9BSD
`size` (revision 2.5), reading the little-endian 16-bit objects this toolchain
produces and meets. It runs on your host.

For how it works inside — the header it reads, the First Edition and overlay
special cases — see the [design document](design.md).

---

## 1. Synopsis

```
pdp11-size [object ...]
```

Each `object` is a PDP-11 a.out file. With no argument, `size` reads `a.out`.
There are **no options** — `size` reads only header fields, and every one of them
is always shown.

---

## Output

A single header line, then one line per object:

```
text	data	bss	dec	oct
158 +	0 +	0 =	158 =	236	dsort.o
```

The columns are:

| column | meaning |
|---|---|
| `text` | size of the text (code) segment |
| `data` | size of the initialised data segment |
| `bss`  | size of the uninitialised data segment |
| `dec`  | `text + data + bss`, in **decimal** |
| `oct`  | the same total, in **octal** |

The `+` and `=` signs are literal separators, and the object's name ends the
line. Sizes are reported in bytes.

For an **auto-overlay** image (`0430`/`0431`), a second line reports the true
core footprint and the size of each overlay:

```
1024 +	256 +	64 =	1344 =	2500	ovprog
2048 total text, overlays: (512,512)
```

Here `total text` is the base text plus every non-zero overlay, and the
parenthesised list is the individual overlay sizes.

---

## What it reads

The object format is taken from the header's magic number:

- **2.9BSD a.out**, all six magics — `0407` (normal), `0410` (read-only text),
  `0411` (separated I&D), and the `0430`/`0431` auto-overlay images. The three
  segment words are reported directly, and overlay images get the extra
  overlay line.
- **First Edition `a.out(V)`** (`0405`) — the Research V1 layout, whose header is
  counted inside the text size and which has no data segment. `size` corrects for
  the counted-in header (real text = `a_text - 12`) and reports the era's data
  area as the bss (see [design §4](design.md)).

A file whose first word is none of the recognised magics — or that is too short
to hold a header — is reported as `not in object file format` and skipped.

---

## Exit status

`size` always exits **0**. There is no failure exit code: a file that cannot be
opened produces a `perror` diagnostic on stderr, and a file that is not an object
produces `<name>:  not in object file format` — in both cases `size` moves on to
the next argument and still exits `0`.

---

## Examples

```
pdp11-size prog                   # text/data/bss and totals for one object
pdp11-size                        # same, for a.out
pdp11-size *.o                    # a table, one line per object
pdp11-size ovprog                 # an overlay image: base sizes + overlay line
pdp11-size /usr/lib/lib*.a        # archives report "not in object file format"
```

`size` looks only at individual object/executable headers; point it at an
archive and each `.a` is reported as `not in object file format` (use
[`pdp11-nm`](../../pdp11-nm/docs/user-guide.md) to look inside an archive).

Continue to the [design document](design.md) for the header layout, the First
Edition correction, and the overlay reporting.
