# pdp11-objcopy

Copy or extract a PDP-11 `a.out`.

```
pdp11-objcopy [-O binary] [-j text|data] infile outfile
```

| invocation        | result |
|-------------------|--------|
| *(no options)*    | copy `infile` → `outfile` unchanged (identity `a.out` copy) |
| `-O binary`       | the loadable image: text + initialized data, no header or symbols |
| `-j text`         | the text segment only, raw |
| `-j data`         | the initialized-data segment only, raw |

PDP-11 is an `a.out`-only world — there is no ELF target the modern binutils
understand — so, unlike the VAX tree's `objcopy`, the useful operations here
are loadable-image and segment **extraction**. Removing the symbol table and
relocation is [`pdp11-strip`](../pdp11-strip/)'s job; disassembly is
[`pdp11-das`](../pdp11-das/)'s.

Extraction supports the ordinary magics `0407`/`0410`/`0411`, whose text and
data are contiguous on disk after the 16-byte header. First Edition (`0405`)
and auto-overlay (`0430`/`0431`) images have a different on-disk layout, so
extraction from them is refused rather than producing the wrong bytes. The
header's segment sizes are validated against the actual file length before any
slice is written.

## Documentation

- **[Design](docs/design.md)** — the object read/rewrite pipeline and the format conversions.
- **[User guide](docs/user-guide.md)** — the options and conversion examples.
