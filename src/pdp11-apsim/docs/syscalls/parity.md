# Syscall parity with vax11-apsim

A category-by-category comparison of what `pdp11-apsim` implements against
the reference `vax11-apsim`. The two simulators target different machine
lines, so "parity" means: **everything the VAX apsim does that is
applicable to the PDP-11 UNIX era set (V5 → 2.11BSD) is done here**, and
the calls both refuse are refused for the same reasons. The VAX apsim's
extra machinery is, with one exception, for ABIs that never existed on the
PDP-11 (NetBSD, OpenBSD, 4.4, SysV threads).

Measured coverage (this tree): every syscall in every PDP-11 era's own
table is answered — `gaps.py` reports zero unanswered for v5, v6, v7,
bsd28, bsd29; bsd210 and bsd211 have only deliberate refusals. Empirically,
`sweep.py` runs **47/47 of 2.11 `/bin` and 41/41 of v6 `/bin`** with no
unimplemented syscall.

## Category by category

| category | vax11-apsim | pdp11-apsim | notes |
|---|---|---|---|
| **File I/O** (open/read/write/close/lseek/dup/dup2/fcntl/access/ioctl) | full | **full** | plus directory `read(2)` served from an era-format snapshot |
| **File metadata** (stat/fstat/lstat, chmod/chown/fchmod/fchdir, link/unlink/symlink/readlink, rename, mkdir/rmdir, truncate/ftruncate, umask, sync/fsync, utimes) | full | **full** | per-era `struct stat` shapes (V6 36-byte, V7 30-byte, 2.10/2.11 52-byte) |
| **Process** (fork/vfork/exec/execve/exit/wait/wait4, getpid/getppid, kill/killpg, nice, alarm/pause) | full (real host fork) | **full** (real host fork) | guest pid ↔ host pid map |
| **Signals** (signal/sigvec/sigaction/sigreturn, sigblock/sigsetmask/sigsuspend) | full 4.3 sigtramp frame | **full** 4.3 sigtramp frame | 32-bit masks; the frame `sendsig` leaves; drives real csh job control |
| **Job control** (setpgrp/getpgrp, TIOCSPGRP/GPGRP, stop signals, wait WUNTRACED) | full | **full** | real host process groups + `tcsetpgrp` |
| **IDs** (getuid/geteuid/getgid/getegid, setuid/setgid, setre[ug]id, getgroups, getlogin) | full | **full** | setre[ug]id accepted (identity model), as on the VAX |
| **Time** (gettimeofday, time, ftime, times) | full | **full** | `$APSIM_TIME` deterministic clock |
| **Sockets** (socket/socketpair/bind/connect/listen/accept, send/recv/sendto/recvfrom, sendmsg/recvmsg, get/setsockopt, getsockname/getpeername, shutdown) | full, real host sockets | **full**, real host sockets | verified AF_UNIX socketpair + AF_INET TCP loopback |
| **Filesystem stats** (statfs/fstatfs/getfsstat) | full (host statvfs) | **full** (host statvfs) | 232-byte 2.11 struct; `df` works |
| **select/poll** | select + poll | **select** | 2.11 has no `poll` (a 4.3+ call) |
| **ptrace** (debug channel) | cooperative channel (drives gdb) | **cooperative channel** (adb model) | opt-in `$APSIM_PTRACE`; PT_READ/WRITE_I/D/U, CONTINUE, STEP, KILL over an abstract socket; wait reports synthetic WIFSTOPPED |
| **sbrk/brk** | full | **full** | flat 64 KB space |
| **__sysctl** | rich node table + `/proc`-scan `ps` | **common nodes** (CTL_KERN/CTL_HW strings, hostname) | 2.11's sysctl is far smaller than 4.4's; the common facts are answered |
| **Overlays** (auto-overlay text) | n/a (VAX has no overlays) | **0430 + 0431** (2.10/2.11 15-overlay, separate-I&D window) | how csh fits 64 KB |
| **Floating point** | bit-exact VAX F/D/G/H softfloat | **bit-exact FP11 D/F softfloat + FIS** | no host FP on either side |
| **Interpreter scripts** (`#!`) | (gap on the VAX) | **implemented** | one interpreter + one arg, plus the command-line `/bin/sh` courtesy |

## Calls both simulators deliberately refuse

pdp11-apsim's entire refusal set (bsd210/bsd211) falls in the same
categories the VAX apsim refuses:

- **Privileged system administration** — `adjtime`, `settimeofday`,
  `quota`/`qquota`, `mount`/`umount`, `reboot`, `chroot`, `acct`: a
  user-mode simulator has no authority to honor them. (The VAX returns
  EPERM; pdp11-apsim currently returns ENOSYS for the same set.)
- **Kernel-internal pokes** — `fetchi` (fetch from user I-space), `ucall`
  (call a kernel subroutine): no user-mode equivalent, exactly like the
  VAX's `nfssvc`/`getkerninfo`/`audgen` refusals.

(`sigwait` — which the VAX apsim refuses — is instead implemented here:
it blocks for a signal in the given set and returns it.)

## The VAX features that don't apply to the PDP-11 line

The VAX apsim's remaining headline extras are, by category, inapplicable
to the PDP-11 UNIX era set:

- **LWP threads on real pthreads behind a GIL** — no PDP-11 UNIX had
  kernel threads. Not applicable.
- **kqueue/kevent** — a 4.4BSD/NetBSD facility; absent from 2.11. N/a.
- **mmap (anonymous + file-backed) and the `*at` family** — 4.3+/POSIX;
  2.11 has neither. N/a.
- **Multi-personality NetBSD/OpenBSD/SVR2 remap tables with shaped
  struct marshallers** — those ABIs don't run on a PDP-11.

The one applicable feature that *was* the outstanding parity item — the
**cooperative `ptrace` debug channel** — is now implemented (opt-in via
`$APSIM_PTRACE`). A traced guest, instead of host-stopping, parks inside
apsim serving an abstract AF_UNIX socket; the tracer's ptrace ops and its
`wait()` (which reports a synthetic `WIFSTOPPED`) go over that channel, so
one apsim process can read/write and single-step another across their
separate address spaces. This runs 2.11's classic `ptrace(2)`
(PT_READ/WRITE_I/D/U, CONTINUE, STEP, KILL) — the adb model — and is
verified by `tests/ptrace.s` (a parent reads a word out of a traced
child's D-space, single-steps it, and continues it to exit).

## Summary

For the universe range this project targets (First Edition through
2.11BSD), pdp11-apsim is at syscall parity with vax11-apsim: the same file,
process, signal, job-control, socket, filesystem-stat, and time coverage;
the same set of deliberate refusals; the cooperative ptrace debug channel;
plus PDP-11-specific work the VAX never needed (auto-overlays, the FP11/FIS
softfloat, the era `struct stat` and directory shapes). Every syscall in
every PDP-11 era's own table is answered.
