# pdp11-apsim — user guide

`pdp11-apsim` runs a classic PDP-11 Unix `a.out` directly on your host: it
emulates the PDP-11 instruction set (including EIS, the FP11 floating-point
unit, FIS, and CIS), loads the program, and turns its `sys` traps into host
system calls. It is a **user-mode** simulator — it runs one program, the way
`qemu-user` or Apout does; it does not boot or emulate a kernel.

It spans the whole PDP-11 Unix lineage, from First Edition (1971) through
2.11BSD, System III/V, and Ultrix-11, selecting the right syscall conventions
per era. For how it works inside — the interpreter, the FP11, the loader, the
syscall layer — see the [design document](design.md).

---

## 1. Synopsis

```
pdp11-apsim [-t] [-s] [-2] [-p pid] [-u universe] a.out [args ...]
```

`a.out` is a PDP-11 executable (`0405`/`0407`/`0410`/`0411`/`0430`/`0431`), a
`#!` script, or — when named directly on apsim's command line — a plain text
script (run through the guest `/bin/sh`). `args` become the guest's `argv[1..]`.

---

## Universes

A *universe* selects the era whose `a.out` details, syscall numbering, and
argument convention apsim assumes. Choose one with `-u NAME` /
`--universe NAME` / `--universe=NAME`, or `$PDP11_UNIVERSE`; a command-line flag
overrides the environment. The default is **`bsd29`**. A `0405` (First Edition)
binary always selects the V1 personality regardless of the universe named.

| universe | era | personality |
|---|---|---|
| `v1` `v2` | First/Second Edition (1971–72) | V1 traps + KE11-A |
| `v3` `v4` `v5` `v6` `bsd1` | 3rd–6th Edition, 1BSD | V5/V6 |
| `v7` `bsd2` `bsd279` | Seventh Edition, 2BSD/2.79 | V7 |
| `bsd28` `bsd29` | 2.8/2.9BSD | 2BSD (default = `bsd29`) |
| `bsd210` | 2.10BSD (1987) | 4.3 numbering, stack args |
| `bsd211` | 2.11BSD pl431 | 4.4 numbering, stack args |
| `sys3` `sys5v2` | System III, SVR2 | System III (utssys/fcntl/ulimit) |
| `ultrix1` `ultrix2` `ultrix3` `ultrix31` | DEC Ultrix-11 1.0–3.1 | Ultrix |

Common aliases are accepted (`2.11`, `2.11bsd`, `sysiii`, `svr2`, `ultrix`,
`v7m11`, …). An unknown name is a usage error that prints the valid list.

---

## Options

| option | meaning |
|---|---|
| `-u NAME`, `--universe NAME`, `--universe=NAME` | select the universe (see above) |
| `-t` | trace every instruction (`pc`/`sp`/`r0`/opcode) to the diagnostic channel |
| `-s` | trace every syscall (number and arguments) |
| `-2` | force the stack-argument syscall convention (as 2.10/2.11 use) |
| `-p pid` | force a fixed `getpid()`, so a program that seeds its RNG from the pid (rogue) replays deterministically |

Options may appear in any order before `a.out`. Diagnostics from `-t`/`-s` (and
every fault message) go to a private high-numbered descriptor duped from the
startup stderr, so a guest that closes or reuses fd 2 cannot mute them.

### Environment

| variable | effect |
|---|---|
| `PDP11_UNIVERSE` | default universe when `-u` is not given |
| `APSIM_ROOT` | guest root: guest **absolute** paths resolve under this directory (a faux Unix tree) |
| `APSIM_ENV` | guest environment, space-separated `VAR=VAL` (default supplies `TERM`, `HOME`, `PATH`) |
| `APSIM_UID` / `APSIM_GID` | guest credentials (default 1/1) |
| `APSIM_PID` | fixed `getpid()` (same as `-p`) |
| `APSIM_SYSARGS=stack` | force stack-argument syscalls (same as `-2`) |
| `APSIM_PTRACE` | enable the cooperative `ptrace` debug channel (needed for guest `adb`) |

(A handful of `APSIM_WATCH*` / `APSIM_DBASE` / `APSIM_RNDPC` variables exist for
debugging apsim itself; they are not part of normal use.)

---

## What it runs

- **Executables** — `0407`/`0410` (shared I&D), `0411` (separate I&D, two 64 KB
  spaces), `0430`/`0431` (auto-overlay, up to 7 overlays through 2.9 and 15 from
  2.10), and `0405` (First Edition, loaded whole at core `040000`).
- **`#!` scripts** — the named interpreter is exec'd with the classic argv
  rewrite (one optional argument, one level of interpretation).
- **Plain text on apsim's own command line** — run through the guest `/bin/sh`,
  the `execvp` courtesy for when there is no calling shell to fall back.
- **A runtime root** — with `$APSIM_ROOT` set, guest absolute paths (a hardcoded
  `/etc/passwd`, `/bin/sh`, `/tmp/...`) resolve inside that tree. The bundled
  `root/` supplies `/etc/passwd`, `/etc/group`, `/etc/termcap`, `/etc/motd` and
  empty `bin` dirs; `sh mkroot.sh --rogue` adds the rogue demo. Real vintage
  binaries run this way — `ls`, `cat`, `csh`, `adb`, and `rogue` (curses works,
  because gtty/stty/ioctl `sgttyb` maps onto host termios raw/cbreak/echo).

Real host `fork`/`wait`/`pipe` back the guest's, and host process groups back
the guest's job control, so shells run pipelines and background jobs.

---

## Exit status

apsim's own exit status reports how the guest ended:

| status | meaning |
|---|---|
| *0–255* | the guest's own `exit(code)` — passed straight through |
| `128+n` | the guest was terminated by uncaught signal *n* (e.g. `135` = SIGEMT 7, `132` = SIGILL 4) |
| `125` | instruction limit reached (the guest ran ~4 billion instructions without exiting) |
| `126` | a debug watch/guard trap halted the run |
| `127` | an illegal instruction or a bad First-Edition syscall halted the run |
| `2` | usage error — bad option, unknown universe, or the `a.out` could not be loaded |

---

## Examples

```
# play the reconstructed rogue from the bundled root, deterministic seed
sh mkroot.sh --rogue
APSIM_ROOT=root pdp11-apsim -p 12345 root/usr/games/rogue

# run a 2.11BSD binary (4.4 numbering, stack args, string-table a.out)
pdp11-apsim -u bsd211 /path/to/2.11/bin/echo hello world

# a Sixth Edition ls, resolving /etc and friends under a V6 root
APSIM_ROOT=~/unix/v6 pdp11-apsim -u v6 ~/unix/v6/bin/ls -l /

# one of the three surviving First Edition binaries (auto-selected by 0405)
pdp11-apsim ~/unix/v1/bin/chown root file

# trace syscalls while running a freshly cross-built program
pdp11-apsim -s ./a.out

# debug a guest with the real 2.11 adb over the cooperative ptrace channel
APSIM_PTRACE=1 APSIM_ROOT=root pdp11-apsim -u bsd211 root/bin/adb ./prog
```

For the interpreter, the bit-faithful FP11, the loader, and the per-era syscall
layer, continue to the [design document](design.md).
