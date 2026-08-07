# pdp11-apsim — design

This document describes how `pdp11-apsim` runs a PDP-11 `a.out` on a modern
host: the emulated machine, the instruction interpreter, the bit-faithful
FP11, the loader, and the per-era syscall layer that turns guest `sys` traps
into host system calls. For how to *use* it, see the [user guide](user-guide.md).

apsim is a **user-mode** simulator, in the spirit of Apout and `qemu-user`: it
loads one `a.out`, executes its instructions, and services the `sys` traps its
libc stubs make — it does **not** boot a kernel or model the MMU, the Unibus,
or any device. There is one guest process image, no supervisor mode, no page
tables; "the kernel" is a C switch statement. The tool is one file, `apsim.c`
(~6,100 lines), plus `../common/universe.h` (generated) for the universe list —
kept self-contained because it is the execution engine the tree's
[oracle suites](../../../oracle/README.md) run their ground-truth binaries under,
and it builds with nothing but a host C compiler.

---

## 1. The emulated machine

Core is two fixed 64 KB byte arrays: `M[]` (data, and the shared I&D space) and
`MI[]` (the separate instruction space, used only by 0411/0431 binaries).
`Isp` points at whichever array instruction fetches read. **Every guest memory
access masks its address to 16 bits** (`a &= 0xffff`) in `ld2`/`st2`/`ld1`/
`st1`/`ifetch`, so a guest can never index outside its own 64 KB — a wild
pointer wraps, exactly as the hardware would.

The register file is `unsigned short R[8]` with `SP=R[6]`, `PC=R[7]`, and the
four condition codes `FN/FZ/FV/FC` as separate ints. There is no PSW word kept
as such; it is assembled only where one is observed (a signal frame, `RTI`).

`ifetch` reads I-space; the data loads/stores read D-space. On shared-I&D
binaries the two arrays are the same pointer, so the distinction is free; on
separate-I&D it is what lets a 0411 program address a full 64 KB of code *and*
64 KB of data.

---

## 2. The instruction interpreter

`step()` fetches one word at `PC`, advances `PC`, and dispatches in an order
chosen so overlapping encodings resolve correctly:

| order | class | note |
|---|---|---|
| 1 | conditional branches | high bits overlap other groups, so tested first |
| 2 | `sys`/`trap` (`0104400\|n`) | into `do_sys` |
| 3 | `EMT` (`0104000\|n`) | overlay switch, V1 validated-`rts`, else SIGEMT |
| 4 | late hardware | `late_insn`: MFPT/SPL/TSTSET/WRTLCK, FIS, CIS |
| 5 | FP11 group (`017xxxx`) | into `do_fp` |
| 6 | double-operand | MOV/CMP/BIT/BIC/BIS/ADD/SUB, `+010` = byte forms |
| 7 | single-operand & the rest | CLR/INC/JSR/JMP/… |

Addressing is resolved once, by `operand(spec, byte)`, which returns a
"location" — either `ISREG|n`, an `ISIMM` literal, or a 16-bit memory address —
and performs auto-increment/decrement and index/absolute word fetches as a side
effect. Index and absolute words come from **I-space** (`ifetch`), the pointers
they yield are dereferenced in **D-space** (`ld2`). `getv`/`putv` then read or
write through the location. The one deliberate special case is `MOVB` into a
register, which sign-extends the byte into the high half (every other byte
instruction leaves it alone) — without it a `movb` of a NUL leaves stale high
bits and a `printf %d` format loop never terminates.

---

## 3. FP11 floating point (bit-faithful, no host FP)

The FP11 is simulated with **true soft D/F arithmetic**, not host `double`s.
An accumulator is a `struct fpv {sign, exp, frac}` carrying a 56-bit mantissa
in D mode (24 in F). Every operation normalizes a 128-bit intermediate and
rounds by adding one at the first discarded bit — the FP11's round-half-up
rule, with no sticky bit and no round-to-even. A host `double` has only 53
mantissa bits and rounds differently, so it silently mis-rounds the last bits
(caught against `ecvt.c`'s constant conversion: `...c3` on the real machine,
`...c0` under host-double emulation). The KEV11 **FIS** instructions share the
exact same engine at F width, so **no host floating point is used anywhere** in
apsim and there is no `libm` dependency.

`bits2fpv`/`fpv2bits` convert to and from the on-disk DEC layout exactly both
ways; `rdfloat`/`wrfloat` move 2 or 4 guest words. The FPS word tracks D/L/T
mode; `FT` (chop) truncates instead of rounding, exercised by the `factor`
regression.

`late_insn` covers the instructions later PDP-11s added over the base set:
`MFPT`, `SPL`, `TSTSET`/`WRTLCK`, the FIS group, and the **Commercial
Instruction Set** — CIS being packed/zoned **decimal** arithmetic on 128-bit
intermediates (`dec_read`/`dec_write`/`dec_cc`), enough of the ADDN/SUBN/CMPN/CVT
family for the corpus. The whole group is the subject of the `cistest.s` golden
probe (§Testing).

---

## 4. The loader

`load_aout_env` is shared by the initial exec and by guest `exec(2)`. It reads
the eight-word header a word at a time, **masking each to 16 bits** so a
short or hostile file cannot turn a size into a negative int and thence a huge
`size_t`. It recognises:

| magic | form | load |
|---|---|---|
| `0405` | First Edition | whole file image at core `040000`, entry `040000` |
| `0407` | shared I&D, unsplit | text then data at 0 (V7 lineage) / at `040000` under a V1/V2 universe |
| `0410` | shared I&D, read-only text | data on the next 8 KB boundary |
| `0411` | **separate I&D** | text into `MI[]`, data into `M[]`, both from 0 |
| `0430` | auto-overlay, shared I&D | overlay window above text, data above it |
| `0431` | auto-overlay, **separate I&D** | base text *and* the window in I-space, data at D:0 |

Segments are read by `loadseg`, which **clamps** the count so `off+count` can
never exceed `0x10000` — a short file just leaves the tail zero (core was
`memset` first).

**Overlays.** A 0430/0431 header carries `max_ovl` and an `ov_siz[]` array (7
slots through 2.9, 15 from 2.10). Each image reads into a fixed 16 KB `ov_img[]`
slot, and the loader **bounds every `ov_siz` to the slot and to core** because
the run-time `EMT` handler `memcpy`s `ov_siz` bytes into the window. That handler
(`ovno` in `r0`, the 2.9 `csv.s` protocol) zeroes the `ov_max` window and copies
the image in; for 0431 the window lives in I-space (`MI[]`), which is how 2.11
csh fits (55 K base + window + data) into 64 K.

**Scripts.** A `#!` file execs the named interpreter with the classic argv
rewrite (one optional argument, one level). A shebang-less *text* file on
apsim's **own** command line runs through the guest `/bin/sh` — the `execvp`
courtesy, since there is no calling shell to do the `ENOEXEC` fallback — while a
*guest* exec of such a file fails authentically.

The exec stack is laid out where crt0 expects it (`setup_stack`: `argc`, argv,
NULL, envp, NULL, strings near the top of D-space; env from the caller, else
`$APSIM_ENV`, else a default). First Edition uses `setup_stack_v1` (a
`-1`-terminated argv near the top of 8 K core — pointers must be positive 16-bit
values or `ar`'s member walk breaks). The entry PC is masked with `~1`: the V7
lineage treats an odd `a_entry` as a marker, not an address.

---

## 5. Universes and kernel personalities

apsim serves 20 universes (V1–V7, 1BSD/2BSD/2.79/2.8/2.9/2.10/2.11, System III,
SVR2, Ultrix-11 1.0–3.1), selected by `--universe`/`-u` or `$PDP11_UNIVERSE`;
the default is **bsd29**. The list lives in the generated `universe.h`, which
maps each universe to one of eight **era-ordered** kernel personalities:

```
K_V1 < K_V56 < K_V7 < K_SYS3 < K_ULTRIX < K_BSD2X < K_BSD210 < K_BSD211
```

Because the enum is era-ordered, lineage checks read as ranges — `Kern >=
PDP11_K_BSD210` means "the 4.3-numbered eras" — and one comparison decides, for
instance, whether `stackargs` (arguments passed on the C stack rather than
inline after the trap) is in force. Selecting a universe sets `Kern`, `Univ`,
and `stackargs`; a `0405` magic still forces the First Edition personality at
load time regardless of the universe named.

---

## 6. The syscall layer

`do_sys` decodes the trap's argument-passing convention, then hands a canonical
number to `do_syscall`:

- **indirect** (`sys 0; .word blk`) — the real number and inline args live at
  `blk`;
- **inline** (V1..2.9) — argument words follow the trap; `do_sys` steps `PC`
  past `sysnargs(num)` of them (counts taken verbatim from Apout's `v7arg[]`);
- **stack** (2.10/2.11) — a bare `sys N` has no inline words; args are read from
  `2(sp)…`, and a per-call table decides whether the first arg is an fd (which
  still rides in `r0`);
- **Ultrix `0200`** — the 2.0 FP-less `/bin` sets bit `0200` to mean "args on
  the C stack" with the call number in the low 7 bits (`sys 204` = `write|0200`).

`do_syscall` is a single **canonical switch in V7 numbering** — the lineage
trunk, which 2.8/2.9 extend in place — plus synthetic `C_*` extension ids for
calls with no V7 ancestor (sockets, `sigvec`, `statfs`, …). Each renumbering
era supplies one **data remap table** (`Bsd210Remap`, `Bsd211Remap`,
`Sys3Remap`, `Ultrix3Remap`) mapping its guest numbers onto canonical ones;
`sremap_apply` moves a number and, via the `SR_STAT` flag, records when the era
wants the newer `stat` shape. A V5/V6 call that did not yet exist is gated to
`EINVAL` by `v56_nosys`.

Around that switch:

- **struct stat** has three era writers — `put_stat_v6` (packed 36-byte, 16-bit
  size), `put_stat_v7` (32-bit size), `put_stat_211` (52-byte, 4.3 layout) —
  chosen by universe and the `SR_STAT` flag.
- **errno** is mapped host→guest by `errno_h2g`: the first 34 are the shared V7
  inheritance and pass through; past that the eras diverge (the 4.3 eras moved
  `EAGAIN` to 35 and gained the socket errno block at 35..68), so an unmappable
  Linux number collapses to a sane in-range meaning rather than leaking through.
  An unknown syscall number returns `ENOSYS`, never halts.
- **directories** — classic Unix reads them with `read(2)`, which Linux refuses,
  so apsim snapshots an opened directory into the era's on-disk record format
  (V1 10-byte, V5..2.9 16-byte, 2.10/2.11 4.3 variable) and serves reads from
  the snapshot; the real `ls` works. **Sockets** are real host sockets — 2.11's
  `AF_`/`SOCK_` constants and `sockaddr_in` already match the host, so only the
  option level/name and the `AF_UNIX` path need translating.
- **signals** use the full 4.3 reliable frame: delivery builds the 26-byte
  `sendsig` frame (including `sc_ovno`, the overlay mapped when the signal hit,
  so `sigreturn` restores it), enters the libc `sigtramp`, and blocks
  `sig|hmask` until `sigreturn`. Guest numbers are translated to/from host
  numbers (they agree for most of 1–15 but diverge in the 16–31 job-control
  set). `fork`/`wait`/`pipe` use the **real host** calls, so a child inherits a
  full copy of the guest image for free; host process groups back the guest's,
  so a default-stop signal really stops and `SIGCONT` resumes it — the genuine
  2.11 csh runs jobs and pipelines.
- **ptrace** (opt-in via `$APSIM_PTRACE`) can't reach a forked tracee's `M[]`/
  `R[]` directly, so a traced guest parks on its trap and serves the classic
  ops (`PT_READ/WRITE_I/D/U`, `PT_CONTINUE`, `PT_STEP`, `PT_KILL`) over an
  abstract `AF_UNIX` socket named for its pid; the tracer's `wait` sees a
  synthetic `WIFSTOPPED`. This drives the real 2.11 `adb`.

Every apsim diagnostic goes to `Dbg`, a private high-numbered close-on-exec dup
of the startup stderr — **not** raw fd 2 — because the guest owns fds 0/1/2 and
a program that reassigns fd 2 (csh does) would otherwise mute the simulator.

---

## 7. The First Edition personality

A `0405` binary (or a `0407` under `-u v1`/`v2`) selects the 1971–72 machine.
The whole file image loads at `040000` and runs from there — the magic word
`0405` *is* `br .+14`, branching over its own 12-byte header. Traps follow the
First Edition convention: no indirect call, per-call inline argument words
(from `v1inl[]`, cross-checked against the Nov-1971 manual), fd-style first args
in `r0`, the **C bit** signalling error, and `time` returning in the KE11-A's
AC/MQ. The machine grows a **KE11-A** extended-arithmetic element at `0177300`
(intercepted in `ld2`/`st2`), because the 11/20 had no EIS and V1 userland
multiplies and divides through it. `emt n` is the V1 kernel's **validated
`rts`** (it checks the return address is in core, even, and non-null, then
bounces through it — `chown` returns from subroutines this way), and
`v1statout` writes the 34-byte stat. The three surviving First Edition binaries
(`ar`, `mv`, `chown`) run.

---

## 8. Memory-safety discipline

Because the loader parses vintage and dump-carved binaries and the syscall
layer copies guest-controlled lengths, hostile input is a first-class concern.
Three rules contain it: every `ld/st/ifetch` **masks the address to 16 bits**,
so no access escapes its 64 KB array; every bulk transfer between core and the
host goes through **`gclamp(addr, len)`**, which clamps the length to what fits
from `addr` to the end of core (`read`/`write`/`readv`/`writev`/`send`/`recv`/
`sendto`/`recvfrom`/`sendmsg`/`recvmsg` and the ptrace read/write paths); and
the **loader clamps** every segment read (`loadseg`), bounds each overlay size
to its slot and to core, keeps the `EMT` `memcpy` within `ov_max`, and reads the
header with per-word masking. Valid guests are unaffected by any of it.

---

## Testing

`make check` runs `tests/run.sh`, a set of golden probes assembled with this
tree's own `pdp11-as`:

| probe | asserts |
|---|---|
| `cistest.s` | MFPT/SPL/TSTSET/WRTLCK, FIS, and decimal CIS — each case jumps to `fail` with its number in `r5`, so the exit code names the first failure |
| `errno.s` | a failed `open` delivers the **era** errno (`ENOENT`=2), not a blanket 1 or a raw host number |
| `gate211.s` | the universe gate **both ways**: `sys 64` is `getpagesize` under `-u bsd211` and must **fail** under the default universe |
| `fpimm.s` | FP11 `$literal` operands and `FT` chop mode (the `factor` regression) |
| `timetext.s`, `ptrace.s`, `sockpair.s` | `time` writes no memory; the ptrace channel; socketpair |
| real era binaries | V5/V6 `ls`/`cat` and 2.11 `echo`/`cat`, skipped when the distribution trees are absent |

`make fuzz-smoke` runs `tests/fuzz-load.sh` over a **deterministic 800-case**
malformed-`a.out` corpus (`mkfuzz.py`: every exec- and overlay-header word
crossed with hostile values, plus a truncation ladder, over one seed per magic).
A case passes unless apsim dies of a sanitizer abort or its own SIGSEGV — a
rejected load, a guest fault, a clean exit, and a timeout are all legitimate
answers to garbage. `make check-san` rebuilds under **ASan+UBSan**
(non-recoverable) and runs both the suite and the fuzz smoke under them; leak
detection is deliberately off (the guest space and tables are never freed by
design), the value being memory-corruption detection on the hostile-input paths.

---

## For a maintainer

- **It is one file on purpose.** `apsim.c` builds with a host C compiler and
  `universe.h`; keep it self-contained so the oracle suites can run it anywhere.
- **`universe.h` is generated** from `universes.tsv` by `mkuniverse.py` — add or
  change a universe there, never by editing the header, and map it to one of the
  era-ordered `PDP11_K_*` personalities.
- **The syscall switch is canonical V7 numbering.** A new era gets a `sremap`
  data table, not a new copy of the switch; a call with no V7 ancestor gets a
  `C_*` extension id. Shape differences (stat, stack args) travel in flags, not
  forks.
- **Honour the memory-safety discipline.** Any new bulk copy between guest core
  and the host must go through `gclamp` (or an equivalent bound); the loader
  fuzzer and `check-san` are what keep that honest — run `make check-san` after
  touching the loader or a copy path.
- **No host floating point, ever.** FP11 and FIS share the soft engine; adding a
  host `double` reintroduces the rounding divergence this design exists to avoid.
- **Diagnostics go to `Dbg`, not stderr** — the guest owns fd 2.
