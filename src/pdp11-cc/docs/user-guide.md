# pdp11-cc — user guide

`pdp11-cc` is the C compiler **driver** — the authentic 2.9BSD `cc`, the program
that runs the preprocessor, the two compiler passes, the optional optimiser, the
assembler and the linker in order and hands each the right arguments. It
compiles nothing itself. It is a *cross* driver: it finds its passes relative to
itself and targets the PDP-11, while running on your host. The C it accepts is
K&R / pre-1977 C (old-style definitions, implicit `int`), the language the
period compiler is itself written in.

For why it is built the way it is — the pass discovery, the universe model — see
the [design document](design.md).

---

## 1. Synopsis

```
pdp11-cc [-c] [-S] [-E] [-P] [-O] [-v] [-V]
         [-Dname[=def]] [-Uname] [-Idir] [-C]
         [--universe=ERA | -u ERA] file ... [-lx ...]
```

Each `file.c` is preprocessed, compiled, assembled, and (unless a stage flag
stops earlier) all objects are linked with `crt0.o` and `-lc` into `a.out`, or
into `-o`'s target. `file.s` is assembled; `file.o` and any other unrecognised
argument (a `-lm`, a `-L<dir>`) go straight to the linker.

The pipeline is `cpp → c0 → c1 [→ c2 with -O] → as → ld`. cc carries **no
compiled-in paths**: it resolves its own real location through `/proc/self/exe`,
runs the sibling passes that share its prefix (`pdp11-cc` → `pdp11-cpp`,
`pdp11-c0`, …), and finds `crt0.o`, `libc.a` and the target headers in the
`lib/` and `include/` beside the `bin/` it lives in. So an installed `pdp11-cc`
compiles and links with nothing but a source file.

---

## Options

| option | meaning |
|---|---|
| `-c` | compile and assemble to `.o`; do not link |
| `-S` | stop after the compiler, leaving `.s` assembly |
| `-E` | preprocess only, to standard output |
| `-P` | preprocess only, to a `.i` file |
| `-O` | run the `c2` peephole optimiser |
| `-v` | verbose: name each stage as it runs |
| `-V` | compile for text overlays (overlay `cpp`/`as`/`ld` path, `-lovc`) |
| `-Dname[=def]`, `-Uname`, `-Idir`, `-C` | passed through to `cpp` |
| `-o outfile` | name the output (refuses to overwrite a `.c` or `.o`) |
| `-p` | profiling: link `mcrt0.o` |
| `-2` / `-20` | separate I&D startup/library (`crt2.o` / `crt20.o` + `-l2`) |
| `-f` | floating-point-interpreter build (`fc1`, `fcrt0.o`, `-lfpsim`) |
| `-B<dir>` / `-t[012p]` | substitute some passes from `<dir>` (vintage) |
| `--universe=ERA` / `-u ERA` | the target era (see below) |

`-Dunix` and `-Dpdp11` are always supplied to `cpp`, so `#ifdef pdp11` works
without your having to define it. `-O` is opt-in; the default pipeline does not
run `c2`. `-B`/`-t`/`-f` are inherited from the vintage driver and use relative
pass paths, so they sit outside the relocatable install — an ordinary build
needs none of them.

---

## The universe (target era)

The universe is the PDP-11 UNIX era the program is *for*. Select it with
`--universe=ERA` / `-u ERA`, or by exporting `PDP11_UNIVERSE`; an explicit flag
wins over the environment, and with neither the default is **`bsd29`** (2.9BSD).

cc validates the name against the toolchain's universe table and exports the
canonical spelling to its children, so:

- **`ld`** stamps the era id into the executable as `__univ` (which the one
  universal libc's crt0 records and dispatches on at run time) and writes the
  image in that era's native a.out shape;
- **`cpp`** takes its header directory (`include/<universe>/`) from it;
- **`pdp11-apsim`**, when you run the result, uses it as the emulated-kernel
  personality.

Only universes marked **buildable** are accepted — those the single universal
`libc.a` and headers serve. Give an unknown or unbuildable name and cc prints
the list of valid ones and exits. Names are the canonical eras and their
historical aliases: `v1`…`v7`, `bsd1`, `bsd2`, `bsd28`, `bsd29` (default),
`bsd210`, `bsd211`, `sys3`, `sys5v2`, `ultrix1`/`ultrix2`/`ultrix3`/`ultrix31`,
with spellings like `2.9`, `2bsd`, `sysiii` normalised for you.

```
export PDP11_UNIVERSE=bsd211      # every compile in this shell targets 2.11BSD
pdp11-cc foo.c -o foo             # foo built for 2.11BSD
pdp11-cc --universe=v7 foo.c -o foo   # or pick the era per compile
```

---

## Files it produces

What is left on disk depends on where the flags stop the pipeline:

| you run | you get |
|---|---|
| `pdp11-cc foo.c` | `a.out` (compiled, assembled, linked) |
| `pdp11-cc -o foo foo.c` | `foo` |
| `pdp11-cc -c foo.c` | `foo.o`, no link |
| `pdp11-cc -S foo.c` | `foo.s` (PDP-11 assembly) |
| `pdp11-cc -E foo.c` | preprocessed text on **stdout** |
| `pdp11-cc -P foo.c` | `foo.i` (preprocessed text) |

The scratch files each pass hands the next live in a private per-run directory
under `$TMPDIR` (else `/tmp`) and are removed on exit, even on interrupt — they
never appear in your working directory. A lone compile-and-link
(`cc foo.c -o foo`) also removes the intermediate `foo.o` for you.

---

## Exit status

- **0** — success.
- **8** — a fatal error: a pass died on a signal, `-o` would overwrite a source,
  or the requested universe is unknown/unbuildable.
- **other non-zero** — a pass exited non-zero; cc drops that file and reports
  (`Fatal error in <pass>`), or `Can't find <pass>` when a pass could not be
  exec'd — check the install layout or the pass's name.
- **100** — interrupted (SIGINT/SIGTERM), or a pass could not be found at all.

---

## Examples

```
pdp11-cc -o hello hello.c              # compile + link; headers and libc found beside cc
pdp11-cc -c foo.c bar.c                # compile each to an object, no link
pdp11-cc -O -S foo.c                   # optimised PDP-11 assembly, foo.s
pdp11-cc -E -DDEBUG -I. foo.c          # preprocess only, to stdout
pdp11-cc --universe=bsd211 -o foo foo.c bar.o -lm   # link for 2.11BSD
pdp11-cc -p -o foo foo.c               # profiled build (mcrt0.o)
pdp11-cc -v -o foo foo.c               # narrate each stage as it runs
```

Continue to the [design document](design.md) for pass discovery, the universe
model, and the link path.
