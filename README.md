# pdp11-xdev

[![license: BSD-3-Clause](https://img.shields.io/badge/license-BSD--3--Clause-blue.svg?style=flat-square)](LICENSE)
[![docs](https://img.shields.io/badge/docs-nyxcraft.github.io-ffb64d.svg?style=flat-square)](https://nyxcraft.github.io/pdp11-xdev/)
[![host: LP64 Linux](https://img.shields.io/badge/host-LP64%20Linux-informational.svg?style=flat-square)](#)
[![target: PDP-11 a.out](https://img.shields.io/badge/target-PDP--11%20a.out-orange.svg?style=flat-square)](#)
[![universes: 1e → 2.11BSD](https://img.shields.io/badge/universes-First%20Edition%20%E2%86%92%202.11BSD-8a2be2.svg?style=flat-square)](#the-one-idea-universes)

📖 **Documentation site: <https://nyxcraft.github.io/pdp11-xdev/>**

A host-side, **multi-universe PDP-11 cross-development toolchain**.  On a
modern LP64 Linux host it builds PDP-11 programs — preprocess, compile,
optimize, assemble, link — into real 2BSD `a.out` binaries, and runs them,
unmodified, under a user-mode PDP-11 UNIX simulator.  The compiler is
**Ritchie's `cc`** (the authentic `c0`/`c1`/`c2` multi-pass compiler, not
PCC), ported from the 2.9BSD sources; the whole pipeline is proven against
the original PDP-11 binaries down to the byte.

```sh
make && make libc                          # tools -> ./bin, era libs -> ./lib
echo 'int main(){ printf("hello, pdp-11!\n"); return 0; }' > hi.c
bin/pdp11-cc --universe=bsd29 hi.c -o hi
bin/pdp11-apsim ./hi
#  -> hello, pdp-11!
```

## The one idea: universes

"Which UNIX" is a parameter, not a fork of the toolchain.  One set of host
tools serves every supported era; `--universe=NAME` (or the exported
`PDP11_UNIVERSE`) selects the target runtime — which system headers `cpp`
reads (`include/<universe>/`), which `crt0.o` and libraries `cc`/`ld` link
(`lib/<universe>/`), and which personality `apsim` provides.  The universe
names live in one table, `src/common/universes.tsv`, so a name means the
same thing everywhere.

| universe | status | what it is |
|---|---|---|
| `bsd29` | full (default) | 2.9BSD (1983): overlays, split I&D, the most-verified era |
| `bsd28` | full | 2.8BSD (1981): the Ritchie-cc era rogue was built in |
| `v1` | simulator | First Edition UNIX (1971-72): apsim runs 0405 binaries |
| `v7` | simulator | Seventh Edition: apsim's syscall layer is the V7 set |
| `v5` `v6` `bsd210` `bsd211` `sys3` | planned | sources staged in `~/unix` and `~/bsd` |

Independently of the universe, the **assembler** is historically
parameterized: `--isa=`, `--sys=`, and `--aout=` select the instruction
set, syscall keyword table, and object format from First Edition (1972)
through 2.11BSD string-table objects — see
[src/pdp11-as/docs/std.md](src/pdp11-as/docs/std.md).

## The tools

The full Berkeley pipeline — `cpp → c0 → c1 [→ c2] → as → ld`, driven by
`cc` — plus the matching binutils, the per-era C libraries, the simulator,
and disk-image tooling:

| role | tools |
|---|---|
| compiler chain | `cc` `cpp` `c0` `c1` `c2` `as` `ld` |
| C libraries | per-universe `libc.a` + `crt0.o` (+ curses, termlib, the 2.9 era-exact `libc-era.a`) |
| object tools | `nm` `size` `strip` `das` (disassembler, V1→2.11BSD) `dcc` (decompile driver) |
| archives | `ar` `ranlib` (byte-identical 2BSD archive format) |
| sources | `xstr` (shared-strings extractor) |
| media | `s5fs` (V7/2.8/2.9/2.10 disk images: mkfs, fsck, tar, restore, …) |
| execution | `apsim` (user-mode simulator: EIS, FIS, CIS, bit-faithful FP11, overlays, V1 personality) |

Each tool lives in `src/pdp11-<tool>/` with its own Makefile and tests.

## Build and test

```sh
make            # every host tool -> ./bin/pdp11-*
make libc       # era headers -> ./include/<u>/, target libs -> ./lib/<u>/
make check      # per-tool suites + the end-to-end pipeline suite
                # (compile/link/run under apsim, both universes, plain and -O)
```

Requires a host C compiler (`cc`/`gcc`/`clang`), `make`, `yacc`, and
`python3`.  Nothing else — no PDP-11 hardware, no external trees.

The opt-in [oracle suites](oracle/README.md) go further: they run the
**original 1981/1983 PDP-11 tool binaries** under `apsim` and byte-compare
our output against them (compiler passes, `as` over whole source trees,
`ar`/`ranlib` archive layout, `ld` down to a byte-identical overlay
kernel).  Fixtures regenerate locally from the vintage distribution trees.

## Layout

```
src/
  common/        single source of truth: the universe table (universes.tsv ->
                 universe.h), host-side a.out/ar structs (cross/), the
                 relocatable-path helpers (ucbpath/), shared config.mk
  pdp11-*/       one directory per tool: Makefile, sources, docs/, tests/
  pdp11-libc/    per-universe target code: bsd28/ and bsd29/ each carry the
                 era's authentic libc, csu, headers, curses, termlib, fpsim
bin/ include/ lib/   build products (git-ignored)
tests/           the end-to-end pipeline suite (make check runs it last)
oracle/          opt-in native-binary verification (fixtures not committed)
docs/            cross-cutting design and user guide
```

## Provenance

Merged from three sibling projects — `pdp11-bsd28-toolchain`,
`pdp11-bsd29-toolchain` (its direct descendant, the merge base), and
`pdp11-bsd28-apsim` — taking the most capable version of every tool; see
[NOTES.md](NOTES.md) for the per-tool decisions.  2.11BSD
(`~/bsd/2.11`, patch level 431) is the designated porting base for tools we
add from here on.  Sources are BSD-licensed (see [LICENSE](LICENSE)); the
vintage UNIX content the tools operate on is under the Caldera
ancient-UNIX license.
