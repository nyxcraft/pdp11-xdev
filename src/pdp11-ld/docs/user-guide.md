# pdp11-ld — user guide

`pdp11-ld` is the link editor: it combines classic 2BSD a.out objects and
archives into a single PDP-11 executable (or a relinkable partial object),
resolving references between them, laying out the text, data and bss segments,
and relocating every address to its final place. It is the authentic 2.9BSD
`ld` running on your host, so it also produces the `0430`/`0431` auto-overlay
images the MENLO overlay machinery was built for.

For how it works inside — the two passes, the 16-bit word I/O, the relocation
and overlay internals — see the [design document](design.md).

---

## 1. Synopsis

```
pdp11-ld [-o output] [-e entry] [-u sym] [-r] [-d] [-s] [-n|-i] [-O]
         [-x|-X|-S] [-D size] [-t] [-Z obj ... -L] [-v name] file ... [-lx]
```

Each `file` is an object or an archive, linked in the order given. The output
is written to `a.out` unless `-o` names another file. If every external
reference is resolved the output is made executable; if not, it is left as a
relinkable object (see [Exit status](#5-exit-status)).

---

## 2. Options

| option | meaning |
|---|---|
| `-o output` | name the output file (default `a.out`) |
| `-e sym` | set the entry point to `sym` |
| `-u sym` | enter `sym` as an undefined reference, to force in the library member that defines it |
| `-l x` | link the library `lib<x>.a` (resolved against the universe lib dir — §3) |
| `-r` | **relinkable** output: keep relocation bits, leave undefined symbols undefined, do not allocate common |
| `-d` | allocate common (bss) even under `-r` |
| `-s` | strip **all** symbols from the output |
| `-x` | discard local symbols |
| `-X` | discard local symbols whose names begin with `L` (compiler temporaries) |
| `-S` | keep only ordinary locals and globals; drop other symbol types |
| `-n` | pure, read-only shared text (`0410`) |
| `-i` | separate instruction and data space (`0411`) |
| `-O` | force the `0405` overlay magic (OMAGIC) |
| `-D size` | pad the data segment out to `size` bytes |
| `-t` | trace: print each file and symbol as it is processed |
| `-Z` | begin a text-overlay segment (§4) |
| `-L` | end a text-overlay segment (§4) |
| `-v name` | name an overlay node in the overlay tree (§4) |

The default output is `0407` (FMAGIC) — impure, relocated, executable. Flags may
be glued to the dash (`-rd`, `-sn`). `-o`, `-e`, `-u`, `-v` and `-D` each take
the following argument.

---

## 3. Libraries and the universe

`-lx` names `lib<x>.a` and does **not** search `/usr/lib`. The library directory
is found relative to the `pdp11-ld` binary (via `/proc/self/exe`): the tool
strips the trailing `bin/` and looks in the sibling `lib/`. Within it, an era
subdirectory is preferred:

```
.../lib/<universe>/libx.a      # tried first
.../lib/libx.a                 # flat fallback
```

The universe is taken from `$PDP11_UNIVERSE` (default from `universe.h`, e.g.
`bsd29`), so the same command links against the right era's C library just by
changing that variable. On a final link `ld` also stamps the chosen era into the
output as the absolute symbol `__univ`, letting one universal `libc.a` pick its
behaviour at run time.

An archive carrying a `__.SYMDEF` directory (built by `ranlib`) is fast-loaded:
only the members that resolve a reference are pulled in, regardless of their
order in the archive. If the directory is out of date, `ld` warns and scans the
members in order instead.

---

## 4. Overlays

For programs larger than the address space, `ld` produces overlay images.

- **`-Z obj … -L`** brackets a run of objects into one text overlay. Their code
  is placed at the shared overlay base and reached through 8-byte JSR thunks in
  the resident text; the output magic becomes `0430` (with `-n`) or `0431` (with
  `-i`). An overlay link therefore **requires `-n` or `-i`** — plain `0407`
  overlays are rejected. Up to 7 overlays.
- **`-v name`** names a node in an overlay *tree*: overlays sharing a name share
  everything defined before the node and are laid down as siblings after it.

---

## 5. Exit status

- **0** — a clean link with no errors; the output is made executable.
- **1** — the output was written but with non-fatal errors: undefined symbols
  left, a multiply-defined symbol, or an entry point not in text. The output is
  left non-executable (relink or fix, then rerun).
- **4** — a fatal error: no input files, a file that cannot be opened or
  created, a bad object format, a table overflow, too many overlays, or an
  interrupt. No usable output is produced.

An undefined-symbol list is printed (unless `-r`), so a partial link tells you
exactly what is still missing.

---

## 6. Examples

```
pdp11-ld -o prog crt0.o prog.o -lc              # link a program against libc
pdp11-ld -r -o part.o a.o b.o                   # partial, relinkable object
pdp11-ld -n -o sh crt0.o sh.o -lc               # pure shared text (0410)
pdp11-ld -i -o big crt0.o big.o -lc             # split I/D space (0411)
pdp11-ld -n -Z ov1.o -Z ov2.o -L base.o -lc     # auto-overlay image (0430)
PDP11_UNIVERSE=v7 pdp11-ld -o p crt0.o p.o -lc  # link for the V7 universe
pdp11-ld -t -o prog crt0.o prog.o -lc           # trace what gets pulled in
```

Continue to the [design document](design.md) for the passes, the word-at-a-time
I/O, and the relocation and overlay internals.
