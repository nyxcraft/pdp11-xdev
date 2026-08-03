# pdp11-apsim — a 2BSD PDP-11 user-mode runtime for Linux

apsim runs classic 2.8BSD PDP-11 binaries (a.out 0407/0410/0411, including
separate I&D) directly on a modern Linux host, in the spirit of Apout and of
user-mode personalities like qemu-user: PDP-11 instructions are emulated,
2.8BSD `sys` traps become host syscalls, and guest paths resolve inside a
bundled 2.8-style root directory.

It also runs **First Edition (1971-72) binaries**: a 0405 magic selects the
V1 personality automatically — the whole file loads at core 040000 (the
magic word is `br .+14`, hopping its own header), syscalls follow the 1971
convention (inline argument words, C-bit errors, `time` in AC/MQ), a KE11-A
extended arithmetic element appears at 0177300, and `emt n` performs the
V1 kernel's validated-`rts` service.  The three surviving First Edition
binaries (`ar`, `mv`, `chown`) run correctly.

Grown from (and still verifying) the pdp11-bsd28 cross-toolchain project
and now the execution engine of this merged tree: the [oracle
suites](../../oracle/README.md) run the original 1981/1983 compiler,
assembler, ar/ranlib, and ld binaries under it as ground truth, and boot
the byte-exact reconstructed rogue.

## Features

- **CPU**: full general instruction set, all addressing modes, EIS
  (MUL/DIV/ASH/ASHC), condition codes; **separate I&D (0411)** with split
  64 KB instruction/data spaces.
- **FP11 floating point, bit-faithful**: true 56-bit D-format (24-bit F)
  integer-mantissa arithmetic with the FP11 rounding rule (round-half-up on
  the first discarded bit) — not host doubles.  Verified against the 1981
  compiler's constant conversion to the last bit.
- **Syscalls**: the ~50-call 2.8BSD set (Apout v7trap parity) — files,
  processes (fork/wait/pipe via real host fork), ids (with a settable
  uid/gid model), time, signals with 2.8 `sendsig` frame semantics and
  RTI/RTT, kill/alarm/pause.
- **Terminal**: gtty/stty/ioctl `sgttyb` emulation mapped onto host
  termios (raw/cbreak/echo), enough for curses programs — rogue plays.
- **Runtime root**: `root/` provides `/etc/passwd`, `/etc/termcap`, etc.;
  guest absolute paths are resolved under `$APSIM_ROOT`.

## Quick start

    make                    # (repo root) builds ../../bin/pdp11-apsim
    make check              # this dir: the CIS/late-hardware golden probe
    sh mkroot.sh --rogue    # populate the runtime root (+ rogue demo)
    APSIM_ROOT=root ../../bin/pdp11-apsim root/usr/games/rogue

## Environment

    APSIM_ROOT   guest-root directory for absolute guest paths
    APSIM_ENV    guest environment (default provides TERM, HOME, PATH)
    APSIM_UID / APSIM_GID   guest credentials (default 1/1)
    APSIM_PID    deterministic getpid (also: -p flag)
    -s           trace syscalls

## Roadmap

- populate `/bin` with 2.8 userland built by the cross-toolchain; run `sh`
- job-control signal set (SIGCHLD etc.) for csh
- the toolchain's self-hosted bootstrap runs under apsim (stage gates)

## Provenance / license

`apsim.c` was developed in the pdp11-bsd28-toolchain repository (split out
at toolchain commit 9d30f45); history prior to the split lives there.  The
2.8BSD-derived runtime content falls under the Caldera ancient-Unix license.
