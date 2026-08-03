# pdp11-xdev — user guide

Build the toolchain, pick a universe, compile and run PDP-11 programs.
For how it is put together, see [design.md](design.md).

---

## 1. Build

    make            # every host tool -> ./bin/pdp11-*
    make libc       # era headers -> ./include/<u>/, target libs -> ./lib/<u>/
    make check      # full gate: per-tool suites + end-to-end pipeline suite

Prerequisites: a host C compiler, `make`, `yacc` (byacc/bison), `python3`.
Everything is vendored; no network, no external trees.

## 2. Quick start

    echo 'int main(){ printf("hello, pdp-11!\n"); return 0; }' > hi.c
    bin/pdp11-cc hi.c -o hi          # default universe: bsd29
    bin/pdp11-apsim ./hi

Choose the era per invocation or per shell:

    bin/pdp11-cc --universe=bsd28 hi.c -o hi     # flag form (also: -u bsd28)
    PDP11_UNIVERSE=bsd28 bin/pdp11-cc hi.c -o hi # environment form
    export PDP11_UNIVERSE=bsd28                  # ...or for the whole session

An unknown or not-yet-buildable universe is rejected with the valid list.
The universe selects `include/<u>/` for cpp, `lib/<u>/{crt0.o,libc.a,...}`
for cc/ld, so the same command line targets either era.

## 3. The pipeline by hand

    B=$PWD/bin
    $B/pdp11-cc  -S prog.c            # stop after codegen -> prog.s
    $B/pdp11-cc  -c prog.c            # compile -> prog.o
    $B/pdp11-cc  -O prog.c -o prog    # with the c2 peephole optimizer
    $B/pdp11-as  -o prog.o prog.s     # assemble 2BSD syntax
    $B/pdp11-ld  -X lib/bsd29/crt0.o prog.o -lc -o prog
    $B/pdp11-nm    prog               # symbols
    $B/pdp11-size  prog               # text/data/bss
    $B/pdp11-das   prog               # disassemble (any era, V1..2.11BSD)
    $B/pdp11-apsim prog               # run it

`cc` accepts K&R / pre-1977 C — the dialect the 2BSD system itself is
written in (old-style definitions, implicit int).  Compiler passes honor
`$TMPDIR` for scratch files.

## 4. Overlays

For programs past 64 KB of text, the MENLO auto-overlay scheme works end
to end:

    bin/pdp11-cc -V -c big1.c big2.c ...        # ovas-mode objects
    bin/pdp11-ld -X crt0.o -Z ov1.o ... -Z ov2.o ... -L base.o ... -lovc -o prog

produces a 0430 executable that `apsim` loads and switches automatically
(`-lovc` is built for bsd29).  `size`/`das` understand the overlay header;
the oracle suite links the 2.9 GENERIC kernel this way byte-identically.

## 5. The assembler's historical axes

Independent of the universe, `as` can speak every era's dialect:

    --isa=v1|v4|bsd211|extended      instruction set (1972 .. full DEC line)
    --sys=none|v1|v6                 syscall keyword tables
    --aout=v1|v2|v2+                 object format: 12-byte V1 header with
                                     bit-stream relocation / 16-byte 0407 /
                                     2.11 string-table symbols
    --std=v1,...,v7,bsd,newbsd,extended   composable presets

See [../src/pdp11-as/docs/std.md](../src/pdp11-as/docs/std.md).  `-j`
enables the late-hardware mnemonics (MFPT/CIS/FIS...) that the apsim test
probe uses.

## 6. Running vintage binaries

`apsim` runs originals, not just our output: 0407/0410/0411/0430 2BSD
a.outs, and First Edition (0405) images with the 1971 trap conventions
(the surviving V1 `ar`, `mv`, `chown` run correctly).  Useful knobs:

    APSIM_ROOT=path   guest filesystem root for absolute paths
    -s / -t           syscall / instruction trace
    -p N, APSIM_PID   pin getpid (rogue seeds), APSIM_TIME pins time(2)
    -2                2.9/2.10 stack-argument syscall convention

`src/pdp11-apsim/root/` is a minimal guest root skeleton; `mkroot.sh`
populates `/tmp` and can install rogue.

## 7. Disk images

`bin/pdp11-s5fs` manipulates V7/2.8/2.9/2.10 s5fs disk images without
mounting: `mkfs`, `mktree`, `tar`, `restore`, `dump`, `fsck`, `ls/cat/
get/put/shell`, `manifest`, `verify`, and VHD wrapping for SIMH.  See
`src/pdp11-s5fs/README.md`.

## 8. Verifying against the originals

The opt-in oracle suites compare the toolchain to the native 1981/1983
binaries executed under apsim — see [../oracle/README.md](../oracle/README.md)
for fixture setup and the full battery (compiler corpus, tree-wide as
sweeps, ar/ranlib byte-matching, ld/kernel links, das round-trips,
self-hosting).
