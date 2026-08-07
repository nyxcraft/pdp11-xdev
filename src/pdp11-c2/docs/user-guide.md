# pdp11-c2 — user guide

`pdp11-c2` is Ritchie's `c2`, the C **object-code improver** — a peephole
optimiser that reads PDP-11 assembly and writes tighter PDP-11 assembly. It is
the `-O` pass of `pdp11-cc`, run between the code generator and the assembler. It
is **not a command you run directly**: it is a compiler pass `cc` invokes only
when you pass `-O`. This guide documents its calling convention so you can
recognise it in `cc -v` output or drive it by hand when debugging.

It computes nothing about the target era — the same assembly in gives the same
assembly out. For how it works inside — the instruction list, the transforms, the
register tracking — see the [design document](design.md).

---

## 1. Synopsis

```
pdp11-c2 [+] [-] [infile [outfile]]
```

With no filenames it filters standard input to standard output, which is how
`cc -O` runs it. With one name it reads that file and writes stdout; with two,
the first is the input and the second the output. Two optional leading flags come
before the filenames:

| flag | meaning |
|---|---|
| `+` | emit a debug dump (developer diagnostic) |
| `-` | print optimisation statistics to stderr at exit (iterations, jumps threaded, redundant moves, `sob`s added, literals eliminated, …) |

There are no other options: c2 has no era/universe setting and no knob for the
loop-instruction distance guard, which is always on (see [design §6](design.md)).

---

## 2. What it does

It rewrites the assembly, it does not regenerate it: branch shortening and jump
threading, dead-code and redundant-instruction removal, cross-jumping and code
motion, redundant-move elimination with register tracking, and turning a counted
`dec`/`jne` loop into a single `sob`. The result reassembles to smaller,
equivalent code — the same answer, fewer bytes.

---

## 3. Invocation (via cc)

`cc -O` runs c2 as the `c2` stage of `cpp → c0 → c1 → c2 → as → ld`. `c1` writes
its assembly to a temp file (`tmp5`) instead of the usual output, and `cc`
invokes

```
c2 tmp5 tmp3
```

so c2 reads `tmp5` and writes `tmp3`, the assembler source `as` then consumes.
You do not choose these paths; `cc` allocates and removes them, and prints
`Pass 2` under `cc -v` when it reaches c2. Without `-O` the pass is not run at
all — the default pipeline matches `cc` without `-O`. If c2 fails, `cc` falls
back to the unoptimised c1 output, so a c2 problem is invisible through `cc -O`
alone (run c2 directly to see it).

---

## 4. Exit status

c2 always exits **0**. It does not fail on the *content* of well-formed assembly
— it either improves a construct or leaves it alone. A genuine failure is an I/O
or resource problem reported to stderr and exited non-zero before optimisation
begins: a file it cannot open (`C2: can't find …` / `can't create …`) or no
memory for its node arena (`C2: no memory` / `C Optimizer: out of space`).

---

## 5. Examples

Normally you never type c2; you turn it on through `cc`:

```
pdp11-cc -O prog.c -o prog          # compile with the peephole optimiser
pdp11-cc -O -S prog.c               # stop after c2: optimised prog.s
pdp11-cc -v -O -c prog.c            # see each pass, including "Pass 2" (c2)
```

To run the pass by hand for debugging (what `cc` does between its temp files):

```
pdp11-c2 < foo.s > foo.opt.s        # the filter form cc -O uses
pdp11-c2 foo.s foo.opt.s            # named input and output
pdp11-c2 - foo.s foo.opt.s          # same, and print stats to stderr
pdp11-c2 + foo.s foo.opt.s          # same, with the debug dump
```

Continue to the [design document](design.md) for the transformations c2 runs and
the register tracking that keeps them safe.
