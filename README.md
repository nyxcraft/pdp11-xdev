# pdp11-xdev

[![license: BSD-3-Clause](https://img.shields.io/badge/license-BSD--3--Clause-blue.svg?style=flat-square)](LICENSE)
[![docs](https://img.shields.io/badge/docs-nyxcraft.github.io-ffb64d.svg?style=flat-square)](https://nyxcraft.github.io/pdp11-xdev/)
[![host: LP64 Linux](https://img.shields.io/badge/host-LP64%20Linux-informational.svg?style=flat-square)](#)
[![target: PDP-11 a.out](https://img.shields.io/badge/target-PDP--11%20a.out-orange.svg?style=flat-square)](#)
[![universes: 1e → 2.11BSD](https://img.shields.io/badge/universes-First%20Edition%20%E2%86%92%202.11BSD-8a2be2.svg?style=flat-square)](#the-one-idea-universes)

📖 **Documentation site: <https://nyxcraft.github.io/pdp11-xdev/>**

*Software architecture, design & engineering by Nicholas J. Kisseberth.*

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
`PDP11_UNIVERSE`) selects the target runtime — and one **universal**
`libc.a` + `crt0` serve every era from `lib/`, with the universe chosen at
*link* time: `ld` stamps an absolute `__univ` = the era id into the
executable, `crt0` records it, and any libc routine whose ABI moved branches
on it.  So `--universe` picks which system headers `cpp` reads, the `__univ`
value `ld` stamps, and which personality `apsim` provides.  The universe
names live in one table, `src/common/universes.tsv`, so a name means the
same thing everywhere.

| universe | status | what it is |
|---|---|---|
| `bsd29` | full (default) | 2.9BSD (1983): overlays, split I&D, the most-verified era |
| `bsd28` | full | 2.8BSD (1981): the Ritchie-cc era rogue was built in |
| `v7` `v6` `v5` | full | Fifth/Sixth/Seventh Edition: the rest of the V7-syscall-convention family the one universal libc compiles and runs |
| `v4` | full | Fourth Edition (1973): the first C kernel; 0407/0410 load-at-0 (V5/V6 personality). apsim runs the rediscovered native binaries, and our compiler + libc target it |
| `v3` | full | Third Edition (Feb 1973): no native binaries survive, but the manual confirms the 8-word 0407 (load-at-0, entry 0) + FP11 — matching our ld output and the V5/V6 personality; core syscalls match (its mid-range names are a First-Edition/modern hybrid) |
| `bsd1` `bsd2` | full | 1BSD (1978) & 2BSD (1979): the first two Berkeley distributions — userland layered on Sixth/Seventh Edition (no kernel of their own), so their personalities *are* V6/V7. Our compiler + libc target them; a native V6/V7 binary is byte-identical under `bsd1`/`bsd2` and `v6`/`v7`, and native `ex` (1BSD) / `csh` (2BSD) run under apsim |
| `bsd279` | full | 2.79BSD (1980): the last pre-kernel Berkeley release — userland for *both* V6 and V7, still the V7 personality. Our compiler + libc target it; a native V7 binary is byte-identical under `bsd279` and `v7`, and 2.79's own updated `csh` runs under apsim |
| `bsd211` | full | 2.11BSD (porting base): the 4BSD stack-arg convention + 4.x numbers, dispatched on `__univ` by the same libc |
| `bsd210` | full | 2.10BSD: the same 4BSD-convention personality |
| `v1` `v2` | full | First & Second Edition UNIX (1971-72): the third convention — inline-arg traps; `ld` emits the 0405 format at 040014 and the same libc's `printf` runs on it |
| `sys3` | full | System III (1980): the PWB/V7-derived commercial line — the V7 inline/indirect syscall convention (its `utssys`/`fcntl`/`ulimit`/`nap` additions remapped in apsim), `0407`/`0410`/`0411` a.out. Our compiler + libc target it; native System III binaries run under apsim |
| `sys5v2` | full | System V Release 2 (1984): the last System V with PDP-11 support — System III plus the SysV IPC suite, same V7 convention. No PDP-11 SVR2 binaries survive (only the VAX source kit), so best-guess like `v3`: our compiler + libc target it, and apsim serves it with the System III personality (SVR2's direct ancestor) |
| `ultrix11v1` `ultrix11v2` `ultrix11` | full | Ultrix-11 1.0 (= V7M-11 1.0, 1982/83), 2.0 (1984) and 3.x (3.0 1986, which merged 4.2BSD TCP/IP as an Executive-mode driver; 3.1 1987, the last release): DEC's productized Version 7 for the PDP-11. The local 3.1 `sysent.c` is authoritative — V7 core numbers (1–48) plus DEC/SysV additions (`msgsys`/`utssys`/`fcntl`/`ulimit`/`nap`… at 49+, no user-visible sockets), `0407`/`0410`/`0411` a.out — served by apsim's Ultrix personality. Compiler-validated like `v3` (native disk/tape images survive but aren't carved yet) |

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
| C library | one universal `libc.a` + `crt0.o`, universe selected at link via `__univ` (+ curses, termlib) |
| object tools | `nm` `size` `strip` `das` (disassembler, V1→2.11BSD) `dcc` (decompile driver) |
| archives | `ar` `ranlib` (byte-identical 2BSD archive format) |
| sources | `xstr` (shared-strings extractor) |
| media | `s5fs` (V7/2.8/2.9/2.10 disk images: mkfs, fsck, tar, restore, …) |
| execution | `apsim` (user-mode simulator: EIS, FIS, CIS, bit-faithful FP11, overlays, V1 personality) |

Each tool lives in `src/pdp11-<tool>/` with its own Makefile and tests.

## Build and test

```sh
make            # every host tool -> ./bin/pdp11-*
make libc       # matched headers -> ./include/, one universal libc.a -> ./lib/
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
  pdp11-libc/    one universal target C library (common/{gen,stdio,sys,csu,
                 nonfpcrt}, headers, curses, termlib, fpsim); __univ-selected
bin/ include/ lib/   build products (git-ignored)
tests/           the end-to-end pipeline suite (make check runs it last)
oracle/          opt-in native-binary verification (fixtures not committed)
docs/            cross-cutting design and user guide
```

## Provenance

Merged from three sibling projects — `pdp11-bsd28-toolchain`,
`pdp11-bsd29-toolchain` (its direct descendant, the merge base), and
`pdp11-bsd28-apsim` — taking the most capable version of every tool.  2.11BSD
(`~/bsd/2.11`, patch level 431) is the designated porting base for tools we
add from here on.  Sources are BSD-licensed (see [LICENSE](LICENSE)); the
vintage UNIX content the tools operate on is under the Caldera
ancient-UNIX license.

## Credits

Software architecture, design & engineering by **Nicholas J. Kisseberth**.
