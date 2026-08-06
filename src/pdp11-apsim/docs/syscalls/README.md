# apsim syscall numbers

apsim stands in for the kernel at the `sys` trap boundary, so a syscall
number has no meaning until you say *whose*: 2.9's `vfork` is 57, System
III's `utssys` is also 57; 2.11's `stat` is 38, but 38 in every earlier
era is the obsolete `switch` register call. This directory keeps the
ground truth for that question and the tools that hold apsim's dispatch to
it.

```
collect.py    read each era's own table, emit numbers.tsv + MANIFEST.tsv
numbers.tsv   universe <tab> number <tab> name   (generated)
MANIFEST.tsv  per-source provenance: format, count, sha256 (generated)
gaps.py       which of each era's calls does apsim leave silent?
sweep.py      run real era binaries, count ones that hit an unimplemented call
coverage.md   the recorded sweep results per universe, with the misses classified
```

## Where each number comes from

The number is recorded in a different shape in each era, so `collect.py`
carries two readers (the VAX sibling needs `set` and `define` as well; the
PDP-11 line only needs these):

| universe | source | format |
|---|---|---|
| v5, v6 | `~/unix/v{5,6}/usr/sys/ken/sysent.c` | `sysent` (`/* N = name */`) |
| v7 | `~/unix/v7/usr/sys/sys/sysent.c` | `sysent` |
| bsd28, bsd29 | `pdp11-libc/bsd2{8,9}/libc/include/sys.s` | `equ` (`name = N.`) |
| bsd210 | `~/bsd/2.10/root/usr/src/sys/sys/init_sysent.c` | `sysent` |
| bsd211 | `~/bsd/2.11/root/usr/sys/sys/init_sysent.c` | `sysent` |

In every kernel table the leading integers on a row are *argument counts*,
not the number — the number is always in the `/* N = name */` comment, so
one `sysent` reader parses V5 through 2.11. Vendor trees are not committed;
`collect.py` regenerates the derived tables in place and reports (does not
fail on) a missing source.

## The dispatch model gaps.py checks

apsim's dispatch (`../../apsim.c`) is a **canonical V7-numbered** switch.
The renumbering eras reach it through remap tables (`Bsd210Remap`,
`Bsd211Remap`, `Sys3Remap`) that rewrite the guest number to a canonical
number or a `C_*` extension id; V5/V6 gate era-absent numbers with
`v56_nosys()`. `gaps.py` parses those tables and the switch, and reports
any era number that would fall through to `default:`.

"Answered" and "implemented" are kept apart: a number remapped to
`C_NOSYS` is a **deliberate** ENOSYS (networking with no host stack past
what's wired, privileged calls a user-mode sim can't honor), and `C_OK` a
benign no-op. Those are counted separately from true gaps. Current state:
v5/v6/v7/bsd28 fully answered, bsd29 one obscure call short (`rtp`),
bsd210/bsd211 zero unanswered.

Caveat: the indirect call (`sys 0`) is decoded before the switch and First
Edition binaries dispatch through `do_v1syscall`; case-counting reports
those missing when they are not. Re-check a suspect number with a probe.

## Running

```sh
python3 collect.py                 # regenerate numbers.tsv + MANIFEST.tsv
python3 gaps.py                    # coverage per era
python3 sweep.py <tree> <universe> # run a distribution's binaries, count misses
```
