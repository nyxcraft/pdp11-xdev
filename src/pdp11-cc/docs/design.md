# pdp11-cc — design

This document explains what `pdp11-cc` is, how it was turned into a *cross*
driver without changing what it does, and how it fits the toolchain's universe
model. For how to *use* it, see the [user guide](user-guide.md).

The source is one file, `cc.c` — the authentic 2.9BSD `cc` (SCCS id `cc.c 2.6`),
the MENLO_OVLY-capable driver with the overlay pipeline built in — plus the
generated `universe.h` it validates target eras against. It folds in the
[porting guide](porting.md); nothing here contradicts it.

---

## 1. What it is

`pdp11-cc` is the C compiler **driver**. It compiles nothing itself: it parses
the options, then forks and execs the passes in order, threading the scratch
files between them —

```
cpp  →  c0  →  c1  [→ c2 with -O]  →  as  →  ld
```

— stopping early per the flags (`-E`/`-P` after cpp, `-S` after c1, `-c` after
as; `-O` inserts c2). The compiler proper is the two-pass `c0`/`c1` of the
period PDP-11 compiler, not a single `ccom`. The C it accepts is what that
compiler is itself written in: K&R / pre-1977 C — old-style function
definitions, implicit `int`, and so on.

The port is faithful: the vintage option handling, the pipeline sequencing and
the `ld -X crt0 … -lc` link line are unchanged. Every delta is about making a
compiler that *is* the system's compiler into a cross driver that lives wherever
it was unpacked, and about choosing the target era at link time.

---

## 2. Finding the passes: no compiled-in paths

The vintage driver named each pass as a relative path (`../lib/c0`, …), right
for a native compiler and wrong for a relocatable cross toolchain.
`setup_tools(argv[0])` (called first thing in `main`) replaces that:

- If `cc` was reached by an explicit path, that path is used; if it was reached
  by a **bare name**, its real location is read from `/proc/self/exe`, so a
  symlink or a `PATH` lookup still resolves to the actual binary.
- The **pass prefix** is `cc`'s own name up to and including its last `-`. So
  `pdp11-cc` runs its siblings `pdp11-{cpp,c0,c1,c2,as,ld}`; a copy installed as
  `foo-cc` runs `foo-*`, and two toolchains side by side can never be crossed.
- The passes are those siblings in `cc`'s own directory (`base`). The
  **library root** is derived by rewriting a trailing `bin/` in that directory
  to `lib/` (`libroot`), which is where `crt0.o`, `libc.a` and the headers live.

There is deliberately **no fallback path** — a fallback is just a hardcoded path
that waits for the interesting case to be wrong. `pass0`/`pass1`/`pass2`/`passp`
and the `as`/`ld` name buffers were enlarged from the vintage 20 bytes to 1–2 KB
to hold absolute paths.

Two vintage substitution knobs survive and still use *relative* pass names, so
they are outside the relocatable scheme: `-B<dir>` names an alternative pass
directory and `-t[012p]` selects which of `c0`/`c1`/`c2`/`cpp` to take from it;
`-f` substitutes `c1` with the floating-point code generator `fc1`. Ordinary
builds touch none of these.

---

## 3. The pipeline in detail

Each stage is one `callsys()` — `fork` + `cc_execvp` + `wait` — and a non-zero
status drops the current file and moves on (a fatal signal, or a pass that can't
be found, ends the run). The arguments cc builds per stage:

- **cpp** `<src> <out> -Dunix -Dpdp11 [-D/-I/-U/-C …]`. The output is the
  private scratch file `tmp4`, or `-` (stdout) under `-E`. cc predefines
  **`-Dunix -Dpdp11`** because the native PDP-11 cpp did so and ours does not,
  so `#ifdef pdp11` still fires (the driver test checks this). `-P` writes the
  preprocessed text to a `.i` file and stops; `-E` writes to stdout and stops.
- **c0** `<cpp-out> tmp1 tmp2 [-P if profiling]` — produces the two
  intermediate files the second pass consumes.
- **c1** `tmp1 tmp2 <asm-out>` — writes assembly. Under `-S` the output is
  `<src>.s` and cc stops here; otherwise it is a scratch file (`tmp5` when `-O`
  is on, so c2 can rewrite it into `tmp3`).
- **c2** (only with `-O`) `tmp5 tmp3` — the peephole optimiser; if it fails cc
  falls back to the un-optimised `tmp5`.
- **as** `-u -o <src>.o <asm>` — assembles; only `> 1` counts as failure. cc
  stops here under `-c`.
- **ld** `-X <crt0> [-o out] <objects/libs…> -lc` — the link. `<crt0>` and
  `-lc` come from `libroot` (see §2/§4). `-p` swaps the startup to `mcrt0.o`;
  the separate-I&D `-2`/`-20` selects `crt2.o`/`crt20.o` (and `-l2` for `-20`);
  `-f` links `-lfpsim` with the `fcrt0.o` startup. A single compile-and-link of
  one source tidies its intermediate `.o` away afterward.

Files are dispatched by suffix: `.c`/`.s` are sources to compile/assemble, `.o`
is handed straight to `ld`, and anything unrecognised (e.g. `-lm`, `-L<dir>`) is
passed through to the link line unchanged.

---

## 4. The universe: chosen at link, stamped into the image

Every tool in this toolchain answers to a **universe** — a PDP-11 UNIX era.
`resolve_universe()` picks it from `--universe`/`-u`, else `$PDP11_UNIVERSE`,
else the default **`bsd29`** (2.9BSD). It then:

1. **normalises aliases** to the canonical name using the same
   `PDP11_UNIVERSE_ALIASES` table apsim uses (`2.9`→`bsd29`, `1bsd`→`bsd1`, …),
   so the name it exports is always canonical;
2. **validates** the name against the generated `PDP11_UNIVERSES` table,
   accepting only universes marked **`full`** — those the one universal `libc.a`
   and headers actually serve. An unknown or unbuildable name prints the list of
   valid universes and exits 8;
3. **exports** the resolved name back into the environment with `setenv`, so the
   children read the same era with no flags of their own: `cpp` selects
   `include/<universe>/`, `ld` selects the era and stamps it into the image.

The key design point is that there is **one universal libc**, not a library per
era. `crt0.o`, `libc.a` and the headers live *flat* in `lib/`; the universe
selects only the era id `ld` stamps into the executable as `__univ`, which crt0
records for the library to dispatch on at run time. So `resolve_universe`
composes the crt0 path as `libroot + crtname` — the same startup for every
universe — rather than reaching into a per-era directory.

This is why the object format does not move with the universe: the intermediate
objects and the universal libc stay one format so a link can mix them; only the
final executable carries the era, via `__univ`, and via whatever native a.out
shape `ld` writes for that era.

---

## 5. Scratch files: a private directory, no race

For a real compile (not `-E`/`-P`), cc makes **one private temp directory per
run** with `mkdtemp()` under `$TMPDIR` (else `/tmp`), mode 0700, and puts the
fixed-named scratch files `0`…`5` inside it. Because no other user can traverse
a 0700 directory, the passes may open those fixed paths directly with no
symlink/TOCTOU race and no reliance on a sticky `/tmp`. `dexit()` (normal exit)
and `idexit()` (SIGINT/SIGTERM) unlink the files and `rmdir` the directory.
Under `-E`/`-P` no directory is made and the scratch pointers stay `NULL` —
they are unused on that path, so nothing needs guarding.

Honouring `$TMPDIR` rather than a hardcoded `/tmp` lets the build sidestep a
flaky or namespaced host `/tmp` (WSL drvfs, systemd `PrivateTmp`); it never
affects the emitted object.

---

## 6. The private exec, and the header cleanup

cc ships its own path-searching exec, historically the external `execvp` and
here renamed **`cc_execvp`** and made `static`. It behaves like the standard
one — but with cc's rules: a name containing `/` is exec'd **directly with no
`$PATH` search** (cc now passes absolute pass paths, and a PATH walk would both
be wrong and overflow the name buffer), `ENOEXEC` re-runs the target under
`/bin/sh`, and `ETXTBSY` is retried.

Renaming it is what let cc `#include <unistd.h>` and `<string.h>` **directly**,
with no hand-written prototypes: the old `execvp` name shadowed libc's, which
blocked those headers, so the pointer-returning functions cc calls (`strchr`,
`strstr`, `strncpy`, …) had no real declarations and an LP64 host truncated the
returned pointers. With the standard headers in and the shadow gone, that whole
class of 64-bit bug is closed.

---

## Testing

There is no `make check` inside this directory; cc is exercised by the
end-to-end suite at the repo root (`tests/cc/*.sh`), which is where a driver bug
actually shows up — as a program that builds wrong or runs wrong:

- **`driver.sh`** — `cc -S` drives `cpp → c0 → c1` and writes real PDP-11
  assembly, with the passes resolved relative to the install; it also checks the
  `-Dpdp11` predefine reaches the source.
- **`programs.sh`** — `cc -c` compiles a range of K&R programs to objects and
  checks the expected globals with `pdp11-nm`, and recompiles repeatedly to
  guard against the LP64 pointer/uninitialised-memory regressions.
- **`endtoend.sh`, `printf.sh`, `libc.sh`** — full compile→link→run under
  `pdp11-apsim`: `main`'s return becomes the process exit code, `write(2)` and
  buffered `printf`/stdio reach the host, and libc string/numeric/`qsort`/long
  and file-I/O routines behave.
- **`optimizer.sh`** — for each program the `-O` build must give the *same*
  result as the plain build and a text segment no larger.

---

## For a maintainer

- **No compiled-in paths, ever.** The passes, `crt0.o`, `libc.a` and the
  headers are all found relative to cc's real location via `/proc/self/exe`; a
  fallback path is a latent bug. `-B`/`-t`/`-f` are the vintage relative-path
  substitution and are intentionally *not* part of that scheme.
- **The universe is chosen at link and validated.** cc accepts only `full`
  universes, normalises aliases, and exports the canonical name so `cpp` and
  `ld` agree without their own flags. Keep `crt0`/`libc` flat in `lib/`: the era
  is `__univ`, not a directory.
- **`-Dunix -Dpdp11` are cc's job**, not cpp's — do not remove them.
- **`cc_execvp`, not `execvp`.** The rename is load-bearing: it keeps
  `<unistd.h>`/`<string.h>` includable, which is what keeps the driver
  LP64-clean. Do not reintroduce a symbol that shadows libc.
- **The port is faithful to 2.9.** New behaviour belongs behind a flag with a
  reason, as the universe selection is.
