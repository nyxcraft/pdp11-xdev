# Porting guide: cc (compiler driver)

The user-facing command. It parses options, then forks/execs the pipeline
`cpp → c0 → c1 → [c2] → as → ld`, managing temp files between passes.

## Source

`cc/cc.c`. Usage as on 2.8BSD: `cc [-S] [-O] [-c] [-o out] [-Dx] [-Ix] …
file.c …`.

## What works now

- **`cc -S file.c`** runs cpp → c0 → c1 and writes `file.s` (PDP-11
  assembly). Verified end to end (`tests/cc/driver.sh`).
- Option handling, `-D/-I/-U/-C` pass-through to cpp, multiple source
  files, and relocatable pass resolution.
- `-c` (compile+assemble to `.o`) and a plain link (`cc file.c -o prog`)
  run the whole chain through `as` and `ld`; `--universe`/`-u` selects the
  target era.
- **`-O` runs the c2 peephole optimizer** (see [c2.md](c2.md)); it is
  opt-in. The default pipeline (no `-O`) does not run c2.

## Porting fixes

1. **Relocatable pass resolution.** The original hard-codes pass paths
   (`../lib/c0`, …). Added `setup_tools(argv[0])`: if `cc` is
   `…/usr/bin/<prefix>-cc`, the passes are
   `…/usr/bin/<prefix>-{cpp,c0,c1,c2,as,ld}` and `crt0.o` is in
   `…/usr/lib/`. A bare `argv[0]` is resolved through `/proc/self/exe`.
   Mirrors the VAX project. The `passN` buffers were enlarged from 20 to
   1024 bytes to hold absolute paths.
2. **cc's homegrown `execvp`.** cc ships its own `execvp`/`execat` that
   searches `$PATH`. It unconditionally prepended each `$PATH` element to
   the program name, overflowing the 128-byte `fname` once we pass absolute
   pass paths (caught by AddressSanitizer / the stack canary). Fixed to the
   standard rule: **a name containing `/` is exec'd directly, without a
   `$PATH` search** (and `fname` enlarged).
3. **`<string.h>`/`<unistd.h>` included directly.** cc now includes both
   headers with no hand-written prototypes, so the pointer-returning libc
   functions it calls (`strchr`, `strstr`, `strncpy`, …) get their real
   declarations and LP64 no longer truncates the returned pointers. This
   became possible once cc's private path-search exec was renamed
   `cc_execvp` (and made static): it no longer shadows libc's `execvp`, so
   the standard headers pull in without a clash.
4. **`-Dunix -Dpdp11` passed to cpp.** The native PDP-11 cpp predefined
   these; ours does not, so cc supplies them (so `#ifdef pdp11` etc. work).
5. **Temp files.** The active `UCB_UQTEMP` path builds a six-`X` template
   and calls `FD = mkstemp(tmp0)`, which both creates and opens the temp
   file 0600 and returns its fd (replacing the old `mktemp` + `creat`
   dance); the temp-fd variable is an `int`, not a `char`.
6. **`execat` made non-static** to match its earlier non-static
   declaration (modern C rejects the mismatch).

## Build

```make
${BIN}/${PREFIX}-cc: cc/cc.c
	${HOSTCC} ${O} ${COMPAT} -Icross -o $@ cc/cc.c
```

## Next

Once `as` and `ld` are ported, `cc -c` and `cc -o` will assemble and link;
no further driver changes are expected (the `as`/`ld` invocations already
resolve to `<prefix>-as`/`<prefix>-ld`).
