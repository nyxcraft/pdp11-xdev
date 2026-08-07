# pdp11-objcopy — user guide

`pdp11-objcopy` copies a PDP-11 `a.out`, or extracts its loadable image (text plus
initialized data, with no header or symbols) or a single segment. PDP-11 is an
`a.out`-only world — there is no ELF target — so, unlike the VAX tree's `objcopy`,
there is no format conversion; the useful operations are the raw-image and
segment **extractions**. Removing the symbol table and relocation is
[`pdp11-strip`](../../pdp11-strip/docs/user-guide.md)'s job. It runs on your host.

For how each operation works inside — the identity copy, the contiguous-layout
slicing, the refused formats, and the bounds check — see the
[design document](design.md).

---

## 1. Synopsis

```
pdp11-objcopy [-O binary] [-j text|data] infile outfile
```

Both `infile` and `outfile` are required; there is no in-place default. The input
format is detected. `-O` accepts only `binary`; `-j` accepts only `text` or
`data`.

---

## 2. What it does

| invocation | result |
|---|---|
| *(no options)* | identity copy: `infile` → `outfile`, byte for byte (any magic) |
| `-O binary` | the **loadable image**: text + initialized data, no header, no symbols |
| `-j text` | the text segment only, raw |
| `-j data` | the initialized-data segment only, raw |

The plain copy is format-agnostic and works for every magic, including the First
Edition (`0405`) and auto-overlay (`0430`/`0431`) forms. The extractions
(`-O binary`, `-j`) need the contiguous on-disk layout of the ordinary magics
`0407`/`0410`/`0411`; `0405` and `0430`/`0431` are **refused** rather than
mis-sliced. See [design §3–§4](design.md).

---

## 3. Options

- **`-O binary`** — the only `-O` value accepted. Writes the loadable text+data
  image with no header and no symbol table: the bytes the loader maps.
- **`-j text` / `-j data`** — extract a single segment, raw. If both `-O binary`
  and `-j` are given, `-j` selects the slice.

An extraction validates the header's text and data sizes against the actual file
length before writing anything, so a corrupt or hostile header is refused, not
read out of bounds.

There is no option to convert between object formats and no option to strip
symbols: `objcopy` here copies or extracts bytes only. The whole input is read
into memory before the output is written, so naming the same path for `infile`
and `outfile` is safe — but the write is a plain create, not an atomic
rename, so a crash mid-write can still leave a partial output file.

---

## 4. Exit status

- **0** — success.
- **1** — any error: a bad option, the wrong number of file arguments, an input
  that is not a PDP-11 `a.out` (too short, or bad magic), a format that cannot be
  extracted (`0405`/`0430`/`0431`), header sizes that exceed the file, or an I/O
  error.

There is no separate usage exit code — every error, usage included, exits 1.

---

## 5. Examples

```
pdp11-objcopy prog prog.copy             # identity copy, byte for byte
pdp11-objcopy -O binary prog prog.bin    # the loadable text+data image
pdp11-objcopy -j text prog text.bin      # just the text segment, raw
pdp11-objcopy -j data prog data.bin      # just the initialized data, raw
```

To drop the symbol table and relocation instead of extracting bytes, use
[`pdp11-strip`](../../pdp11-strip/docs/user-guide.md).

Continue to the [design document](design.md) for the identity copy, the slicing,
and the hostile-header bounds check.
