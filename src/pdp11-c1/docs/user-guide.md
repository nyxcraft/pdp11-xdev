# pdp11-c1 — user guide

`pdp11-c1` is the **code generator** of Ritchie's PDP-11 C compiler — the second
pass. It reads the intermediate form `pdp11-c0` produced and writes PDP-11
assembler text for `pdp11-as`. It is **not a command you run directly**: it is a
compiler pass, driven by `pdp11-cc`, with a fixed three-argument calling
convention and temp files `cc` creates and cleans up. This guide documents that
convention so you can recognise it in `cc -v` output or drive it by hand when
debugging.

For how it works inside — the code table, the table build pipeline, the soft
floating point — see the [design document](design.md).

---

## 1. Synopsis

```
pdp11-c1 intfile strfile out.s [-V]
```

- `intfile` — c0's first temp file, the intermediate expression/statement stream
  (read as stdin);
- `strfile` — c0's second temp file, the strings and initialised data, appended
  in `.data`; pass `"-"` to skip it;
- `out.s` — the assembler output (written as stdout);
- `-V` — enable Menlo overlay mode (only in a `MENLO_OVLY` build); `cc` passes it
  when overlays are requested.

All three positional arguments are required — c1 exits with `Arg count` if fewer
are given. It reads no environment and no other options.

---

## 2. What it emits

Assembler text for `pdp11-as`: `.text`/`.data`/`.bss` segments, `.globl`
directives, function labels with the `jsr r5,csv` / `jmp cret` prologue and
epilogue, and the instruction stream chosen by the code table. Integer constants
are printed in **octal** (the 2BSD `as` convention), so `return 42` emits
`mov $52,r0`. Floating-point constants are emitted as DEC F/D-format `.data`
words computed by the soft floating point (see [design §5](design.md)), and any
FP use emits a `.globl fltused` reference.

---

## 3. Invocation (via cc)

`cc` runs c1 as the `c1` stage of `cpp → c0 → c1 [→ c2 with -O] → as → ld`. It is
invoked as

```
c1 tmp1 tmp2 tmp3          # or  c1 tmp1 tmp2 tmp5   under -O
```

where `tmp1`/`tmp2` are c0's two temp files and the third argument is the
assembler source: `tmp3` normally, or `tmp5` under `-O` so that `pdp11-c2` can
rewrite it into `tmp3` afterwards. `cc -S` names the output `file.s` and stops
after this pass. You do not choose these paths; `cc` allocates and removes them.
`cc -v` prints `Pass 1` when it reaches c1.

---

## 4. Exit status

- **0** — success (assembly written).
- **non-zero** — one or more errors were emitted (`nerror != 0`): a missing temp
  file, an unwritable output, a malformed intermediate stream, an illegal
  operation, or register/table exhaustion. Each error is printed to stderr as
  `<line>: <message>`.

`cc` treats a non-zero c1 status as a failed compile for that file and moves on.

---

## 5. Examples

Normally you never type c1; you drive the pipeline through `cc`:

```
pdp11-cc -S prog.c            # stop after c1: leaves prog.s
pdp11-cc -O -S prog.c         # same, but c2-optimised assembly
pdp11-cc -v -c prog.c         # see each pass, including "Pass 1" (c1)
```

To run the pass by hand for debugging (reproducing what `cc` does between the
temp files):

```
pdp11-cpp prog.c > prog.i
pdp11-c0  prog.i t1 t2        # c0 writes its two temp files
pdp11-c1  t1 t2 prog.s        # c1: intermediate + strings -> assembly
pdp11-c1  t1 -  prog.s        # skip the string/data file
```

Continue to the [design document](design.md) for the code-table interpreter and
how `table.s` becomes compiled-in C.
