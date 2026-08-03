# Porting guide: ar (archiver)

The archiver that builds `libc.a` in the classic V7 format this toolchain's
`ld` reads (magic word `0177545`, 26-byte member headers).

## Which ar — provenance

The `ar`/`ranlib` pair is **byte-identical across V7, 2BSD, and 2.9BSD** (the
PDP-11 Unix line); 2.8BSD inherited that canonical pair, and its `ld` consumes
the `__.SYMDEF` table `ranlib` writes (`usr/kernel/src/cmd/ld.c`).  The
`opt="uvnbai"` ar in 2.8's `usr/misc/ar.c` is a V6-era variant lacking the
`l` flag that `ranlib`'s `ar rlb` invocation needs, so it is **not** the
system ar.  This port therefore uses the canonical ar (`opt="uvnbail"`,
`man="mrxtdpq"`), paired with [ranlib.md](ranlib.md).

## Porting fixes (LP64 / K&R — the usual classes, see README.md)

- **2-byte magic**: the archive magic is written/read with `write(fd,&mbuf,
  sizeof(int))` — 4 bytes on the host, but the on-disk magic is one 16-bit
  word.  Pinned to 2 bytes; the read variable is `unsigned short` so the
  `!= ARMAG` compare (high bit set) is unsigned.
- **26-byte member header**: `struct ar_hdr` comes from `cross/ar.h` (packed
  `int32 ar_date; int16 ar_mode; int32 ar_size` → exactly 26 bytes).
- **`tmpnam` clash**: the global temp-name variable shadowed `<stdio.h>`'s
  `tmpnam()` — renamed; templates made writable arrays with 6 trailing X
  (glibc `mktemp`).
- **`select` clash**: the local mode-printing helper renamed (POSIX `select`).
- **`struct stat`**: `stats()` uses the host `fstat`/`stat`; member date/uid/
  gid/mode/size copied from `st_*` (truncated to the 16/32-bit on-disk fields).

`make ar-tool`.
