# Porting guide: c2 (peephole optimizer)

> **Status: done.** `cc -O` runs c2 and produces correct, smaller code;
> loops, recursion, calls, and the buffered-stdio path are covered by
> `tests/cc/optimizer.sh`. It is opt-in via `-O` (the default pipeline does
> not run it, matching `cc` without `-O`).
>
> The porting bugs were the usual LP64/host classes: `copy()` walked the
> stack for its second string (`(&ap)[1]`) — K&R varargs, rewritten with
> `<stdarg.h>`; `alloc()` grew its node arena with raw `sbrk`, which corrupts
> the host heap c2's own stdio uses — replaced with malloc chunks; and c2
> choked on the NUL bytes c1 emits on the host (glibc's `printf("%c",0)`
> writes a NUL where the PDP-11 `_doprnt` emitted nothing) — c2 now skips
> them in `getlin`, as `as` already did.

The object-code improver. It reads c1's assembly, builds a doubly-linked
list of instruction nodes, and applies peephole optimizations (jump
threading, dead-code removal, code motion, redundant-move elimination,
register tracking, …), emitting optimized assembly.

## Sources

`c2/c20.c`, `c2/c21.c`, `c2/c2.h`. Invoked as `c2 [in] [out]` (defaults to
stdin/stdout). `+` enables a debug dump, `-` prints optimization statistics.

## Porting fixes

c2 is newer code than c0/c1 (no `=op` operators), so the source changes are
small:

1. **`getline` namespace clash.** c2 has its own `getline()`; glibc also
   declares `getline(char**, size_t*, FILE*)`. Renamed to `getlin`.
2. **Old-style initialisers** without `=` (`struct optab optab[] {…}`,
   `char revbr[] {…}`).
3. **Uninitialised `struct node data` in `movedat` (real bug, fixed).**
   `movedat` declares a local `struct node data;` and, if it finds no DATA
   segment, reads `data.forw` — which is uninitialised stack. On the PDP-11
   the stack happened to be zero there; on the host it is garbage, so the
   instruction list gets relinked through a bogus pointer and corrupted.
   Fix: `data.forw = 0;` at entry. (Found with valgrind.)

## Status: reliable, in production

`-O` builds the libc (Makefile), matching 2.8's `compall` (`cc -c -O` on every
C file).  Output is **byte-identical to the linked rogue3.4 binary** across the
library — strlen, fread/fwrite, strcmp, index, … (33 objects to the byte),
prologue included — so c2's optimization decisions match authentic 2.8 c2.

Two bugs that once made `-O` unusable are fixed:

- **Function prologue (fixed).** The forward `jbr L1 … L1: sub $n,sp … jbr L2`
  prologue now optimizes correctly; `main(){return 42;}` keeps its `mov $52,r0`
  / `jmp cret`.
- **dualop null operand (fixed).** A jump to a numeric label leaves `code==0`
  (c20.c); the sob optimizer's length walk (`ilen`→`dualop`) dereferenced it.
  The PDP-11 read address 0 as an empty operand; on an LP64 host `*0` faults, so
  c2 crashed under `-O` on `do/while(--s)` loops (e.g. stdio/rdwr.c).  `dualop`
  now guards the deref, matching the output path's own `if (t->code)`.

## What works

Linear code and jump structures optimize correctly (redundant-jump removal,
`jbr`-to-next-label, the sob loop transform); `as` then resolves the span-
dependent branches.  The result reassembles to the same bytes authentic 2.8
produced.

## Build

```make
C2FLAGS = ${O} ${COMPAT} -Ic2
```

Built by `make tools` (so it is available for debugging), but not wired into
`cc`.
