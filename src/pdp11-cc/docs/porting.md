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
- `-c`/`-o` (assemble/link) run as far as invoking `as`, which is not yet
  ported, so they stop with a clear "Can't find …-as".
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
3. **Pointer-returning libc functions** (`strchr`, `strstr`, `strncpy`)
   declared explicitly — `<string.h>`/`<unistd.h>` could not be included
   because they clash with cc's own `exec*` wrappers, and without
   declarations LP64 truncates the returned pointers.
4. **`-Dunix -Dpdp11` passed to cpp.** The native PDP-11 cpp predefined
   these; ours does not, so cc supplies them (so `#ifdef pdp11` etc. work).
5. **Temp files.** `mktemp` template grown to six `X`s (modern requirement);
   `creat(tmp0)` given its missing mode argument; the temp-fd variable
   changed from `char` to `int`.
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
