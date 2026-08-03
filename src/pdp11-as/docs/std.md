# The --std= era dialects, practically

`--std=` tokens are PRESETS over three independent axes, each also
settable directly:

- `--aout=v1|v2|v2+` -- the OBJECT FORMAT: `v1` is the First Edition
  12-byte format (magic 0405, runnable on 1971 Unix); `v2` the
  16-byte 0407 used from V2 through 2.10; `v2+` the same 0407 with
  2.11's string-table symbols and 32-character names -- the `+`
  because it has no magic of its own, only bigger symbols.
- `--isa=v1|v4|bsd211|extended` -- which INSTRUCTIONS exist: the
  original V1-V3 set (`v1`: no jbr/jxx, no .ascii, no
  ldfps/stfps/movie/movei -- attested by the surviving 1972 source
  and presumed back to the lost 1971 assembler, whose manual matches
  it point for point; `v2`/`v3` are accepted aliases), the set born
  in V4 and unchanged through 2.10 (`v4`, the default), 2.11's set
  (`bsd211`: adds the mfpi/spl/mfpt group -- exactly completing the
  J-11 processor's instruction set -- and drops the EAE keywords;
  `newbsd`/`211` are accepted aliases), or everything the hardware
  line had (`extended`).
- `--sys=none|v1|v6` -- SYSCALL-NAME keywords: none (V7 onward),
  the 1972 list (quit/intr/cemt/ilgins era), or the V4-V6 list
  (signal era).  Either list makes `wait' mean the system call.
  These are the only two keyword lists that ever existed: V7 moved
  the names to /usr/include/sys.s (plain assignments prepended at
  assembly time), and 2.11 moved them again into <syscall.h> for
  cpp.  The names marched steadily OUT of the assembler and INTO
  the source tree, which is why the axis needs no more rungs.
  das additionally takes `--sys=bsd211` (see below).

Mix them freely: `--isa=211 --sys=v6` describes a machine that never
existed, and the assembler will happily be it.  A --std token is just
a sanctioned combination plus strict undefined handling.

Both `as` and `das` take `--std=<token>`.  Tokens compose with commas
(`--std=newbsd,extended`).  No token = the toolchain default, which is
what every oracle and harness uses.

## What each token does for you

### (no --std) — the default
Undefined names silently become externals for the linker to resolve
(this is what `cc` always asked for, so it is right for compiler-style
work).  Output is a normal 16-byte `.o`.  All the everyday mnemonics
work.  If you are not sure what you want, you want this.

### --std=v1 — produce First Edition (1971) binaries
The output file itself changes: you get the 12-byte-header a.out that
First Edition Unix exec()s, complete with its bit-stream relocation
and old-style symbol table.  Use it to build things a simulated 1971
system (or apout) can actually run, or to round-trip the surviving
V1-era binaries (`das --std=v1 chown | as --std=v1` reproduces them
byte-for-byte).

Language changes you will notice:
- You can name system calls: `sys write` instead of `sys 4`, and the
  old trap-catching calls exist (`sys quit`, `sys intr`, `sys cemt`,
  `sys ilgins`).
- `wait` is the SYSTEM CALL (the number 7), not the instruction.  To
  emit the WAIT instruction, write the word `1`.
- No `jbr`/`jne`-style pseudo-branches -- write real `br`/`jmp`.
- No `.ascii` -- use `<...>` strings under `.byte`, as the era did.
- Undefined symbols stay local (see "strictness" below).

### --std=v2, --std=v3 — assemble 1972-73 source
Exactly the v1 language, but the output is a normal object file.  v2
is the assembler from the surviving 1972 source; v3 is the lost Third
Edition assembler, for which this is the closest documented match.

### --std=v4, v5, v6 — assemble V4-V6 (1973-75) source
System calls by name again, now the trimmed list of that generation:
`sys signal` works, `sys quit`/`sys intr` are gone.  `wait` is still
the syscall, not the instruction.  You get `jbr`/`jxx` and `.ascii`.
One dialect covers all three versions because their assemblers were
literally identical.

### --std=v7, --std=bsd — assemble V7/2.8/2.9/2.10 source strictly
The same language as the default, minus the syscall names (write
`sys 4`), plus strictness: an undefined symbol you never declared
`.globl` stays local (type 0) instead of quietly becoming an external.
That is how every real assembler behaved when invoked without flags,
and it is what you want when assembling hand-written kernel or
standalone code that relies on it.

### --std=newbsd — target 2.11BSD
Three things change:
- Symbol names carry 32 characters and the object uses the 2.11
  string-table format -- required if 2.11's own ld/nm will consume it.
- You can write the mnemonics 2.11's assembler really had: `mfpi`,
  `mtpi`, `mfpd`, `mtpd`, `mfps`, `mtps`, `mfpt`, `spl`, `csm`,
  `tstset`, `wrtlck`, `stst`, `nop`, `ccc`, `scc`.  (Without this,
  era code spells them with `^`-casts like `mfpi = 6500^tst`.)
- The ancient EAE keywords disappear (`mul`/`div` as device addresses,
  `ac`, `mq`, `csw`, ...), exactly as 2.11 dropped them.

### --std=extended — write for the full hardware line
Everything real PDP-11 hardware could execute but no Unix assembler
ever spelled: the FIS floating group (`fadd`..`fdiv`), the complete
Commercial Instruction Set (`movc`, `locc`, `cmpc`, `addn`, `addp`,
`cvtlp`, ... register and inline forms), the 11/60's `med`/`xfc`, and
FP11 DEC-style names (`addd`, `ldf`, `stcfd`, `ldexp`, `stexp`,
`stst`, ...) as aliases of the era f-idiom.  apsim executes all of it,
and `das --std=extended` disassembles it back symbolically.

Warning: do NOT assemble historical sources with this on.  Era code
defines symbols named `mfpt`, `spl`, `ldexp` and so on; if those are
keywords, the assembler silently drops the symbol definitions
(authentic behavior, wrong outcome).

## Strictness and -u

Every `--std` is strict about undefined symbols, because every real
assembler was when invoked bare.  Adding `-u` restores the automatic
externals, in any position: `as --std=v6 -u file.s` behaves like a V6
`as -u`.  Historically cc always passed `-u`, which is why the
toolchain default has it baked in.

## What --std does in das

- `--std=v1|v2|v3` -- decode 1972-era binaries correctly: the old
  system-call traps with their TRUE inline argument counts (from the
  V1 kernel's sysent -- trap 0 is `rele', not indir), the 040000
  user-load bias, the First Edition relocation stream, and `wait`
  printed as a number so the output re-assembles under the same std.
- `--std=v4|v5|v6` -- the V5/V6 sysent's argument counts (`sys seek`
  carries two inline words, not lseek's three; smdate one, not
  utime's two; nothing past signal).
- `--std=v7|bsd` -- accepted for symmetry; nothing to select
  (file formats are always sniffed from the file itself).
- `--std=newbsd` (also `--sys=bsd211`) -- 2.11's trap convention:
  the full low byte indexes the syscall table, arguments ALWAYS come
  off the user stack (no call carries inline words), and syscall 0
  is illegal -- indir died in 2.11.
- `--std=extended` -- show late-hardware instructions as mnemonics
  instead of raw numeric words, and emit `mfpi`-family instructions
  directly (paired with `as --std=extended` or `-j`).

Every `sys N` line is annotated with the era's name for N as a
comment -- `sys 4 / write`, `sys 171 / writev` -- chosen by the
personality (1972 list, V4-V6 sysent, V7/2.9 sys.s, or 2.11
syscall.h).  Comments survive re-assembly byte-exactly, so the
annotation is always on.

## Never gated (same in every mode)

The expression language: `\/` divide (plain `/` starts a comment),
`\%` OR, `\<` `\>` shifts, `%` modulo, `!` (a + ~b), `^` type-carry,
`[]` grouping, and writing two values side by side adds them
(`1 2 3` is 6).  String escapes `\n \t \e \0 \r \a \p \s` with their
1970s meanings (`\e` is EOT, `\a` is ACK, `\p` is ESC).  Numeric
local labels (`1f`/`1b`).  The `~` fresh-symbol marker.  These were
identical in every assembler from 1972 to 2.11, so no dialect changes
them.

## Old flags

The primitives remain for scripts and the oracle harness: `-7`
(strictness alone), `-n` (Newsym format alone), `-j` (= extended),
`-V` (overlay assembly), `-u`, `-o`, `-` and `--` (stdin, 2.11
style).  A `--std` token is just a bundle of these plus the keyword
table swaps.
