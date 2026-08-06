# Syscall coverage sweeps

Honest, corpus-driven coverage: `sweep.py <tree> <universe>` runs every
PDP-11 command binary in a real distribution tree under apsim (rooted at
the tree via `APSIM_ROOT`) and counts the ones that finish without hitting
an unimplemented syscall.  A timeout counts as a pass (a program blocked on
input is not a miss); destructive/interactive commands are skipped; a
binary that can't be read as extracted is *reported*, never silently
dropped, so the denominator stays honest.

**The number is a floor, not a grade.**  As the VAX project's note puts it:
answering a call with `ENOSYS` moves the number exactly as much as
implementing it.  So the misses below are classified, not just counted —
what remains is device-specific, kernel-internal, or networking-without-a-
stack, the same deliberate-refusal category as `adjtime`/`quota`/`fetchi`.

## Results (2026-08-06)

| universe | tree | ran clean / total | remaining misses |
|---|---|---|---|
| `v5` | `~/unix/v5` | 70 / 70 | — |
| `v6` | `~/unix/v6` | 88 / 88 | — |
| `v7` | `~/unix/v7` | 114 / 114 | — |
| `sys3` | `~/unix/sys3` | 174 / 174 | — |
| `bsd210` | `~/bsd/2.10/root` | 262 / 262 | — |
| `bsd211` | `~/bsd/2.11/root` | 303 / 303 | — |
| `ultrix1` | `~/unix/ultrix11/1.0` | 70 / 71 | `elp`: sys 55 |
| `ultrix2` | `~/unix/ultrix11/2.0` | 183 / 184 | `elp`: sys 67 |
| `ultrix3` | `~/unix/ultrix11/3.0` | 184 / 188 | `elc`,`elp`: sys 67 |
| `ultrix31` | `~/unix/ultrix11/3.1` | 185 / 189 | `elc`,`elp`: sys 67 |

The research eras (V5–V7), System III, and both modern 2BSD releases run
their entire shipped command set with no unimplemented call — including the
uucp set (`uucp`, `uux`, `uuname`, `uulog`, `uupoll`, `uustat`, `uuq`,
`uusnap`, …), which ships execute-only (mode 0111, the historical
convention) and so was previously reported as "unreadable / untested".
The sweep now read-enables such binaries just long enough to run them and
restores the original mode, so the denominator is complete: every 2.10 and
2.11 command binary is accounted for (262/262 and 303/303).

## The Ultrix misses, classified

Every remaining Ultrix miss is a DEC error-logger control tool hitting a
call that is privileged or kernel-internal — legitimately refused, exactly
as native Ultrix would refuse it to an unprivileged process:

- **sys 67 = `errlog`** (error-log status & control, `errlog(2)`): the
  `elc`/`elp` DEC error-logger utilities.  A kernel-internal logging
  channel; apsim answers `ENOSYS`, so the tools exit cleanly rather than
  operate on a log device that does not exist.
- **sys 55 = "readwrite (in abeyance)"** under `ultrix1`: a `nosys` slot
  *in the native 1.0 sysent itself* — `elp` probes a call that never
  existed on real Ultrix-11 1.0.  Refusing it is the authentic behavior.

These are the PDP-11 counterpart of the four calls the VAX apsim also
refuses (`adjtime`, `quota`, `fetchi`, `ucall`): not coverage gaps, but
the deliberate boundary of a user-mode runtime.

## What the sweep found and fixed

The first run exposed a real gap the tables had hidden: `ultrix2`'s
networking utilities (`hostname`, `Mail`, `lpr`, `uupoll`, `uustat`) call
`gethostname` (sys 99) *directly*, proving the Ultrix "Berkeley 2.9
compatable" block (the 2.9 local calls flattened onto numbers 80+) is the
whole DEC Ultrix vector — shared across 1.0–3.1, not a 3.x addition.  apsim
now applies that block to every Ultrix universe (`ultrix2` went 177→183 of
184), and the personality gate moved to the real boundary: an Ultrix
binary's `lstat` (sys 82) stays `ENOSYS` under a non-Ultrix V7-family
universe, not under an earlier Ultrix.

## Reproducing

    cd src/pdp11-apsim/docs/syscalls
    python3 sweep.py ~/bsd/2.11/root bsd211
    python3 sweep.py ~/unix/ultrix11/3.1 ultrix31
    # ... one per (tree, universe); see the table above

Re-run after any change to the syscall layer; a drop in a "clean / total"
number is a regression, and a new entry in a "remaining misses" column is
either a new gap or a call to classify.
