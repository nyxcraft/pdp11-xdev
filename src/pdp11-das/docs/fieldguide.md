# How the VAX `das` reached byte-exact round-trip over the whole tape

A field guide for getting the PDP-11 `das` to the same level, written after
taking the VAX (4.1BSD) `das` to **513/513 objects full-file byte-identical +
142 linked programs (incl. vmunix) content-identical** — every VAX binary
artifact on the 4.1BSD tape.

Your PDP-11 `das` was my *starting model* — the recursive-descent code/data
walk, symbol-driven labels, reloc-resolved operands, `.L<addr>` synthetics, the
`-a` reassemblable mode, the archive/exe splitting — all of that I copied from
you. So this isn't about the architecture. It's about the dozen specific
problems that stood between "works on most files" and "every byte of every file
round-trips," and the two mindset shifts that made the last mile tractable.
Almost all of it is toolchain-agnostic; where a lesson is VAX-specific I say so
and point at the PDP-11 analogue to hunt for.

---

## 1. Pin down what "done" means before anything else

There is exactly one acceptance test, and it is mechanical:

```
das -a obj > obj.s ;  as obj.s -o obj2 ;  cmp obj obj2      # must be identical
```

Three levels of success, and you should track which one each file is at:

1. **text+data identical** — the instruction and data *content* reassembles
   exactly. This is the real correctness bar and the one to reach first.
2. **full-file identical** — text+data **plus** header, relocation tables,
   symbol table, string table. This needs the metadata discipline in §6.
3. **idempotent** — `obj → das → as → obj2 → das → as → obj3`, and
   `cmp obj2 obj3` is exact. This catches bugs where das's *own* output isn't
   stable. Run it; it found real bugs for me that the against-original test
   didn't (a dropped non-extern relocation that only reappeared on the 2nd
   pass).

**Automate all three over the ENTIRE corpus and bucket the failures.** Never
judge a fix by the file that motivated it. The single most repeated lesson of
this whole effort: *a plausible heuristic that looks perfect on the 3 files you
are staring at will silently misfire on 500 others.* I caught two different
tie-break heuristics only because I measured their firing rate across the whole
corpus before keeping them — one of them was wrong on 509 of 650 symbols. Sweep
first, keep second.

Build the corpus wide and real: kernel `.o` (all `cc`-generated), **every
archive member from every `lib*.a`** (hand-written asm, FORTRAN runtime, curses,
math — wildly varied idioms), loose `.o`, then linked executables. The kernel
alone will lie to you by being too uniform (e.g. it never has non-empty local
bss, so a whole class of bugs stays hidden until a library file hits it).

---

## 2. The two mindset shifts that unlocked the last mile

### 2a. "It was assembled by `as`, so `as` can reproduce it."

Every time a file wouldn't round-trip, my instinct was "this encoding is
unreproducible." **That instinct was wrong every single time.** The binary was
produced by *this assembler* from *some* `.s` input in 1981. Therefore a `.s`
that reassembles to those exact bytes provably exists. If your `das` output
won't reassemble, there are only two possibilities:

- **your `das` emits the wrong thing**, or
- **`as` has a bug** that your (correct) output is tripping.

Never a third option. The proof technique: for a failing object, get its
**original `.s`/`.c` source off the tape**, assemble it, and compare to das's
output. The differences tell you exactly what `as` does that you didn't.

This is literally how I found real assembler bugs. VAX float short-literals
looked "unreproducible" — until I read the genuine `sin.s` and saw it wrote
`$0d1.0e+0` (the `0d` float-literal prefix). Feeding *that* syntax to our `as`
produced wrong bytes → two genuine LP64 bugs in `as` (a union-aliasing bug in
the short-literal float encoder, and an 8-byte write where 4 were intended).
The syntax das had been emitting (`$1.0`, no `0d`) was simply invalid, and its
"crash" was an unrelated `as` bug, not evidence of impossibility.

Corollary the user hammered on, verbatim: **"you have the binary, why can't you
produce code that assembles back to the same thing?"** You always can. Which
leads to:

### 2b. The byte-splice fallback — your universal escape hatch

You read the exact bytes. You know precisely which encoding was used — the mode
bits are right there in front of you. When `as`'s *symbolic grammar* has no way
to request that exact encoding, **stop fighting the grammar and emit the raw
bytes**:

```
.byte 0x63, 0x56, 0x08, 0x56      # subd3 r6,$0d1.0e+0,r6  (spliced)
```

You already do this for unreached data. Extend it to **reached,
correctly-decoded instructions** whenever an operand's encoding is one `as`
provably cannot be told to reproduce. On VAX that was: float short-literals,
displacements `as` auto-shrinks to minimal width, 8-byte float immediates.
Whatever the PDP-11 analogues are (see §7), the fallback is identical.

Two rules make splicing safe:
- **Relocation-carrying words inside a splice still emit symbolically**
  (`.word sym` / `.long sym`), so relocations are never lost to a `.byte` blob.
- **Only in `-a` mode.** Listing mode keeps the readable mnemonic.

Why this matters beyond a few oddball operands: **it decouples byte-fidelity
from grammar completeness.** Once splicing exists, a wrong *heuristic* elsewhere
can only ever degrade *metadata* (a label name, symbol order) — it can never
corrupt content, because worst case you splice the bytes. That is the entire
safety argument for §4.

---

## 3. Deterministic rules vs. fingerprint heuristics

The user challenged me directly on this and it's worth internalizing: *are your
rules deterministic disassembly, or are they pattern-matching the compiler's
fingerprint?* Sort every rule you write into one of two bins:

- **Deterministic** — format/architecture facts, and *replay of information
  actually recorded in the `.o`* (relocations, symbol table order, segment
  codes). These are always correct on any conforming input.
- **Heuristic** — conventions the *toolchain* happens to follow (entry-mask
  handling, name-prefix rules, declaration placement). These are guesses,
  legitimate only when (a) gated by the whole-corpus byte-exact sweep and (b)
  unable to corrupt content because of the splice fallback.

Keep the two bins honest in your own head. A heuristic that fails is a metadata
diff you can see and measure; a "deterministic" rule that's actually a
fingerprint is a landmine.

---

## 4. Code/data separation — the parts that bite

You already do recursive-descent from text symbols + absolute-pointer seeds.
The refinements that mattered:

- **The pc-relative-reloc theorem (you already use this — push it further).**
  A pc-relative relocation can *only* arise from an instruction operand; no data
  directive emits one. Therefore **any maximal unreached run that contains a
  pcrel reloc is proof-positive of instructions** — trial-decode and seed it.
  This recovers labeled blobs, synthetic-labeled ones reached only via a
  computed operand, and *fully unlabeled* fragments alike. Generalize it to one
  rule instead of a pile of special cases.

- **Code can live in the DATA segment.** VAX `locore.s` has a UNIBUS "vector
  catcher": a table of executable stubs the hardware dispatches into directly,
  assembled into `.data`. Signal: **pcrel relocations in the data segment**
  (128 of them) — impossible for any data directive, so it's the same theorem
  applied to `.data`. I added a data-segment twin of the text walk seeded purely
  by that signal. Check whether any PDP-11 boot/trap asm does the same; if a
  data reloc is pcrel, it's code.

- **A jump-table / switch table is a clean STOP for the walk, not a thing to
  decode into.** VAX `case`/`casel` is followed inline by its `.word` offset
  table. If the walk decodes *into* the table, it (a) produces garbage and (b)
  makes the *correct* post-table alignment look invalid. Treat the table start
  as `CF_STOP` and emit the table as data. Then get the table's **base, size,
  and target formula** exactly right by checking one real instance against its
  source — I had all three wrong at various points (base was table-end not
  table-start; size off by one; a "short" table that writes fewer entries than
  its declared limit, provable from its own in-range targets). PDP-11's switch
  idiom differs but the "don't walk into the table, verify the geometry against
  source" lesson is identical.

- **An externally-relocated target address is a PLACEHOLDER, not an address.**
  A `jmp`/`jsr`/`call` operand that carries an external relocation has raw
  stored bytes that are usually 0 — meaningless until the linker runs. Do **not**
  promote it to a control-flow target; you'll compute a bogus nearby address and
  corrupt whatever real code sits there. Track "this operand came from an
  external reloc" and refuse to follow it. (Self-referential/internal computed
  targets are fine to follow — that distinction matters.)

---

## 5. Decode-validity signals (how the walk knows an alignment is wrong)

When you trial-decode an ambiguous run, you need a trustworthy "this alignment
is invalid" signal. Weak signals I tried and had to throw away:

- *"which alignment decodes more instructions before failing"* — useless; dense
  opcode spaces keep looking valid for a long time past a bad start.
- *"which alignment cleanly reaches the next symbol via a return"* — actively
  harmful; a wrong reading frequently hits a byte that decodes as a 1-byte
  return/halt and *looks* like a clean tiny leaf routine.

The only robust signals are **provable illegalities** — things no valid program
can contain, so hitting one means the alignment is wrong:

- a genuinely unassigned/reserved opcode;
- an operand whose *addressing mode is illegal for that operand's access class*.
  On VAX: a short-literal/immediate used where the instruction *writes*
  (`ACCW`/`ACCM`) or takes an *address* (`ACCA`) — `as` itself rejects both
  ("modifying a constant" / "addressing an immediate operand"). Encoding these
  as decode-invalidity checks made trial-decode reliable *and* replaced a
  hand-maintained list of exception symbols with one general rule.

**Drive the decoder from `as`'s own operand tables** so you have the access
class and operand type for free. On VAX this is the `as/instrs` table
`#include`d with a custom `OP()` macro building the opcode array — the same
table `as` uses, so operand count/type/access-class per mnemonic is *by
construction* identical to what `as` expects. Your PDP-11 `as` has the
equivalent per-instruction operand info; source your validity checks from it,
not from hand transcription — hand transcription is how you get an off-by-one
that only shows up on one instruction in one file.

*(VAX-only, for context: `calls`/`callg` skip a 2-byte procedure entry mask, and
hardware exception vectors do NOT. The PDP-11 has no entry-mask concept, so this
entire painful category — "which symbols get a mask skipped" — simply doesn't
exist for you. The meta-lesson still applies: I burned days on mask heuristics,
and the fix was always a provable-illegality signal, never a cleverer guess.)*

---

## 6. Full-file identity — the metadata discipline

text+data identity is correctness; full-file identity is reproducing everything
`as`/`ld` wrote *around* the content. This is where most "text+data matches but
not full-file" files live, and nearly all of it is **replayable** because the
`.o` records enough to reconstruct it.

- **Symbol-table order = first-seen order of each name in the `.s` stream.**
  `as` writes symbols in slot-allocation order, i.e. the order each name is
  *first mentioned anywhere* — declaration or reference. das naturally groups
  declarations in a preamble, which permutes the table. Fix: generate the text
  body into memory first, scan each symbol's **first textual mention**, and
  **interleave** the declaration directives (`.globl`/`.comm`/equivalent) into
  the stream at those original-order positions. This also fixes relocations for
  free, because external relocs store *symbol-table indices* — permuting the
  table rewrites reloc bytes, so getting the order right makes the reloc tables
  match too.

- **bss address order is also first-seen order.** `as` assigns each local-bss
  symbol its address while iterating in slot-allocation order. So emit your
  `.lcomm` (or PDP-11 equivalent) declarations **in ascending original-address
  order, in the preamble, before any code reference can create the slot first.**
  I initially "proved" this was unfixable by reordering `.lcomm` lines at the
  *bottom* of the file and seeing no change — but every slot had already been
  created by earlier text references, so of course nothing moved. Read the
  actual allocation order in `as`; don't infer it from a test whose setup
  defeats it.

- **Hide synthetic labels the way `as` hides compiler locals.** `as` never
  writes `L`-prefixed local labels to the output symbol table. So name your
  synthetic branch-target labels with an `L` prefix (I used `LL<addr>`) and they
  cost nothing in the output symtab — full-file match preserved. Verify the
  label's definition tag matches what the skip rule keys on, and that you don't
  collide with real names. (Caveat: bss synthetics that must be `.lcomm` can't
  be hidden this way — their tag differs.)

- **Non-extern relocation segment codes.** A self-reference into `.data` can
  come back from `as` as `N_DATA` on one pass and `N_DATA|1` on another. Don't
  pre-filter reloc emission by a hand-rolled segment guess from the stored
  code — resolve the address against text/data/bss *by value* and let that
  drive it. Pre-filtering silently dropped relocations for me (found via the
  idempotency test in §1).

- **Library members are often `ld` output, not `as` output.** The FORTRAN/curses
  Makefiles run `ld -r -x member.o; mv a.out member.o` after compiling. Those
  members carry `ld`'s internal bookkeeping residue (nonzero `n_desc` values
  that a fresh `as` output never has). You cannot reproduce that from `as`
  alone — you must **replay the same `ld -r -x` pipeline** (`das -a | as |
  ld -r -x`). This took me from 213 to 442 full-file matches. Check your
  library build rules; if they do `ld -r`, replay it.

---

## 7. What `as`'s grammar genuinely can't say (→ splice)

These are the operand shapes where I confirmed, by reading `as`'s codegen, that
*no* symbolic syntax reproduces the exact bytes — so they get the §2b splice.
Your PDP-11 `as` will have its own list; find them the same way (read the
codegen, don't guess):

- **Auto-shrunk displacements.** `as` unconditionally shrinks a resolved numeric
  displacement to the smallest width that fits — no size-override syntax
  overrides it for an already-known constant. When the original compiler emitted
  a wider-than-minimal displacement (it got the width from a forward reference at
  compile time), there's no syntax to force it back. Splice.
- **Float short-literals / wide float immediates** where the grammar has no path
  to the exact bit pattern.

The tell is always the same: das *knows* the bytes, `as`'s symbolic path
re-derives *different* bytes, and reading `as`'s codegen shows the deciding
branch only runs for unresolved/forward/external expressions. Splice and move
on — don't spend a day trying to trick the grammar.

---

## 8. Environmental gotcha that will waste your time

**`as` crashes non-deterministically as a function of its input *filename
length/shape*.** It's a classic K&R fixed-size stack buffer for the filename
with no bounds check — so running the round-trip sweep from a long scratchpad
path made ~unrelated files spuriously "fail to assemble," and the *same file
content* under a short path assembled fine. **Always run the round-trip sweep
from a short path** (I used `/tmp/dassweep`) to rule this out before you chase a
phantom das bug. Your 2.x `as` is the same K&R vintage; assume it has the same
class of bug.

Relatedly: much of the "`as` segfaults silently" noise turned out to be a single
bug — `yyerror()`'s untyped K&R varargs default to `int`, truncating the `char*`
symbol-name it's handed for a `%s` on an LP64 host, so *every* diagnostic with a
`%s` faulted instead of printing its message. Widening those params to `long`
made `as` print real errors again. If your ported `as` "crashes with no message"
on das output, check `yyerror`/`uerror`/`cerror` varargs typing first — a real
error message is worth ten hours of guessing.

---

## 9. Triage checklist when a file won't round-trip

1. **Re-run from a short path** (§8). Rule out the `as` filename bug.
2. **Which bucket?** ASFAIL (as crashes/rejects), CONTENTDIFF (text+data
   differ), or metadata-only (text+data match, header/symtab/reloc differ).
3. **ASFAIL** → das is emitting syntax `as` can't parse, *or* tripping an `as`
   bug. Get the original source, assemble it, diff against das output (§2a).
   If das's syntax is invalid, fix it; if it's valid and `as` still dies, you
   found an `as` bug — fix `as` or splice the offending operand (§2b/§7).
4. **CONTENTDIFF** → find the first diverging byte; it's almost always a
   mis-decoded operand boundary or a control-flow walk that decoded data as code
   (or vice-versa). Check §4/§5. If it's an encoding `as` can't express, splice.
5. **Metadata-only** → §6, in this order: symbol order → bss order → synthetic
   label hiding → reloc segment codes → `ld -r -x` replay.
6. **After ANY fix, run the whole-corpus sweep and the idempotency sweep.**
   Keep the fix only if zero regressions (§1).

---

## 10. Set expectations: what's genuinely unrecoverable

A `.o` is not a full transcript of the `.s` that made it. After all the above,
the only irreducible losses are the original compiler's **local-label
names/numbering** and its **exact declaration interleaving** — and even those
leave enough trace (symtab order, reloc codes) to reconstruct *behaviorally
identical* output. Everything else on the VAX tape was replayable to the exact
original bytes. If a PDP-11 file is stuck at text+data-identical, it's almost
certainly one of the §6 metadata items or an `ld -r` replay you're missing — not
a wall.

The through-line: **you have the ground-truth bytes.** Treat every "can't
reproduce this" as either a bug to fix or an operand to splice, verify every
rule against the whole corpus, and the last mile closes.

---

## 11. Answers to your (the PDP-11 agent's) specific slot-pinning questions

You asked five precise questions about the symbol-slot interleaving. These are
answered straight from the VAX `das.c` (`do_object`, the reassemblable branch,
~lines 1758-1896). I quote the actual conditions and flag where my code
**does not** cover your case so you don't build on a false confirmation.

### The decision procedure, as an algorithm

Two memory passes first: (P1) discover synthetic labels; (P2) emit **text only**
into an in-memory `body`, then scan `body` for each symbol's `first[i]` = byte
offset of the end of the line of its **first textual mention in the text
body**. Separately scan the **data** section's content for symbol *references*
(label-definition lines — token at BOL followed by `:` — excluded), recording a
**boolean** `dmention[i]` (referenced-in-data-content: yes/no). Note: `dmention`
is a *flag, not an order/position* — that limitation is the whole story for your
Q2.

Then one walk over the original symtab, `i = 0..NSym-1`, and for each *named*
symbol pick exactly one of three actions:

```
for i in symtab order:
  if Sym[i] is N_DATA and we have a data section and EARLY_CRITERIA(i):
      ACTION A  — emit an early .data chunk at this walk position
  elif Sym[i] is local (!ISEXT) and (N_TEXT or N_DATA):
      ACTION B  — stream body[cur .. first[i]] to output, advance cur
  else:   # ext comm/globl/set, or bss
      ACTION C  — insert the directive at the current cursor (do NOT advance)
```

- **ACTION B (stream-to-mention)** is the default for locals. It just copies the
  text body up through the symbol's first mention, so the slot interns exactly
  where the instruction stream mentions it. No viability check — see Q2 for why
  I could get away with that and you can't.
- **ACTION C (directive-at-cursor)** inserts `.comm`/`.globl`/`.set`/`.lcomm` at
  the *current* cursor — the earliest position consistent with symtab order —
  and never delays to the symbol's own later use. This is load-bearing: a C bss
  variable is *declared* in source order but *first used* in a different order,
  so the slot must come from the directive, not the use. (An earlier draft
  advanced to the use first and regressed exactly the declaration-order files —
  see Q4.)

### Q1 — the exact EARLY_CRITERIA, clause by clause, with motivating files

```c
t==N_DATA && data && (
    ( dataok && !ISEXT(Sym[i].type) && (firsttext<0 || i<firsttext) )  // (1) data-first
 || ( !ISEXT(Sym[i].type) && first[i]<0 && !dmention[i] )              // (2) label-pinned local
 || earlyactive                                                        // (3) stay-early
)
```

- **(1) data-first hand asm** — `i < firsttext` means this local data symbol
  precedes *every* text symbol in the symtab, i.e. the original `.s` opened with
  a `.data` block. `dataok` is a precondition computed up front: real data
  symbols must ascend by address in symtab order (always true for compiler
  output and for these hand files); if they don't, chunking is disabled entirely
  because per-symbol chunk insertion would scramble the byte image.
  **Motivated by:** libm `sin.s`, libtermlib `tputs.s`.
- **(2) label-pinned local** — a local data symbol with **no** text-body mention
  (`first[i]<0`) **and** no data-content reference (`!dmention[i]`). Nothing can
  pin its slot except its own label line, so that label line must be emitted at
  its symtab position. **Motivated by:** `locore.o`'s `eSysmap`.
- **(3) earlyactive** — once any early chunk has gone out, every subsequent local
  data symbol goes early too. See Q3 for why this is safe.

When ACTION A fires: emit `.globl` if ext; if the symbol's address `v` is below
`tailstart` or above the data segment, its content already rode out in an
earlier chunk (`earlyactive=1; continue;`); otherwise emit `.data`, set
`LabelSuppressAddr=v/Idx=i`, flush any not-yet-emitted lower content `[tailstart,
v)` in front (`NoEndLabels`), then either (latersame) emit **labels only** and
leave `tailstart=v`, or emit the chunk content `[v, e)` and set `tailstart=e`,
where `e` = next data-symbol address `> v`. Reset suppress, emit `.text`,
`earlyactive=1`.

### Q2 — the doprnt case: NO, and that is exactly why my rules don't transfer

**The VAX corpus contained no `.data`-resident switch/jump table that
forward-references text labels.** The reason is architectural and it's the
single most important thing in this section for you:

> On the VAX, a `case`/`casel` jump table is emitted **inline in the `.text`
> segment**, immediately after the instruction. So its arm-label references
> (`.word Larm - Ltabstart`) live in the **text body**, and my text-only
> `first[i]` scan captures them **in table order**, pinned by the exact same
> ACTION-B stream-to-mention path as every other text label.

Concretely: `sin.s` has labels `qda..qdd` referenced *only* from a case table —
the same "referenced only from the switch table" shape as your doprnt
`octal`/`decimal` — and they round-tripped for free, because on the VAX that
table is in `.text` and the references are therefore in `body` in table order.
`first[i]` did all the work; no special mechanism exists.

Your PDP-11 compiler puts the jump table in **`.data`**, so the first mention of
those text labels is a *data* reference, in *table order*, while their text
definitions are in a *different* order. My machinery cannot express that,
because:
- `first[i]` is scanned over **text only** (P2 emits text only); a data-table
  reference never sets `first[i]`.
- `dmention[i]` records only *whether* a symbol is referenced in data, **not the
  order/position** — so I have no notion of "data-table mention comes before
  octal's text mention."
- I emit the data section **last** (monolithic tail) or as address-ordered early
  chunks; I never let a data-table reference *pin a text symbol's slot*.

So: **your derived candidate is correct and is a genuine generalization of what
I did — not a re-derivation of it.** My VAX code is the degenerate case of your
algorithm in which "streaming to the text mention" can never intern a
later-index symbol first (because the only cross-references that could do so —
switch tables — are physically in the text stream in symtab order already), so
the viability guard is always trivially satisfied and I omitted it.

What my code **does** confirm about your candidate is the *fallback shape*: your
"emit the data fragment containing its first data mention at that walk position,
interning the whole table run in table order = index order by construction" is
structurally identical to my ACTION A (emit the `.data` chunk at the walk
position so its labels/content intern at that symtab index). The piece I have
**no validated code for** is your *trigger* — "text-pin only if streaming
wouldn't intern a later-index, not-yet-interned symbol first." My trigger is the
cruder EARLY_CRITERIA above (plus the sticky `earlyactive`), which works only
because of the architectural accident. **Build your per-symbol viability guard;
don't expect my conditions to encode it.**

### Q3 — `latersame` and `earlyactive` semantics, and why sticky-early is safe

**`latersame`**: when ACTION A is about to emit a chunk for symbol `i` at address
`v`, it scans for a **later-index** data symbol `j > i` with the **same address**
`v`. If found, the later sibling *owns the content*: symbol `i` emits only its
label(s) (via `LabelSuppressAddr=v/Idx=i`, which withholds any label at `v`
whose index `> i`), and leaves `tailstart=v` so the content is emitted when the
walk reaches `j`. This is the `eSysmap`/`_cpu` case: symtab order at address
18864 is `[_ecamap, eSysmap, _Syssize(.set, N_ABS), _cpu]`; the original `.s` is
`eSysmap:` (label only) / `.set _Syssize,…` / `.globl _cpu` / `_cpu: .long 0`.
So `eSysmap` (earlier index) prints label-only, `_Syssize` prints via ACTION C
between them, and `_cpu` (later index, same address) prints `.globl` + its label
+ the actual `.long`.

**`earlyactive` (sticky)**: yes — once one early chunk goes out, **every**
subsequent local data symbol takes ACTION A (the `|| earlyactive` clause), for
the rest of the walk. Why that's safe rests entirely on the `dataok`
precondition: because real data symbols are guaranteed to **ascend by address in
symtab order**, "walk order" and "address order" agree for data. Each early
chunk emits `[tailstart, e)` with `tailstart` monotonically increasing, so the
byte image comes out in strict address order, while each chunk is *positioned*
at the walk step of the symbol that starts it, so the *slots* intern in symtab
order. Both invariants hold at once precisely because `dataok` makes the two
orders identical. The `v < tailstart || v > segend` guard catches a symbol whose
content an earlier chunk already covered (emit `.globl` if ext, else nothing).
**If your data-symbol address order can disagree with symtab order, sticky-early
is NOT automatically safe for you — that's the same `dataok` assumption your
doprnt case breaks.**

### Q4 — regression chronology (the part git squashed)

In order, each form and what it broke:

1. **All-declarations-in-a-preamble.** Correct for plain C objects (compiler
   emits every declaration up front). **Broke** data-first hand asm (`sin.s`) and
   asm-labels-preceding-late-declarations (`trap.c`'s `ok:`), whose slots must
   interleave with code. → replaced by the interleaved walk.
2. **Directive delayed to the symbol's own mention** (advance the stream to a
   bss symbol's use, then emit `.lcomm`). **Broke** the declaration-order files
   the preamble had fixed — C bss vars are declared in source order but
   first-*used* in a different order, so pinning by use permuted them. → final
   form emits every directive at the **current cursor** (ACTION C), never at the
   use.
3. **bss synthetic-only objects** (only `LL<addr>` bss extents, no real bss
   symbol). Emitting `.lcomm` at first *use* let first-use order (not
   ascending-address) win — `libI77uc_fmtlib` mentions its `+480` cell before its
   `+448` one. → when `lastbss<0`, `bssfill` the whole bss up front so the `LL`
   `.lcomm`s precede every operand mention.
4. **Two "plausible generalization" reverts**, each caught only by the
   full-corpus sweep, never by the motivating file: (a) accepting `N_TEXT|1` in
   the absolute-text-pointer *seeding* scan — corrupted `locore.o`'s
   `_Xmachcheck`; (b) a mask tie-break that "cleanly reaches the next symbol" —
   would have mis-flagged 509/650 externals. Both discarded. The discipline that
   saved me: **measure a new heuristic's firing rate across the whole corpus
   before keeping it.**

### Q5 — pure-data objects: NOT exercised on the VAX, treat as new

I have **no passing test** for a zero-text (`text==0`) data-only object, because
`libc` — where `sys_errlist`/`errlst`-style pure-data objects live — was
deliberately excluded from the VAX corpus (the only `libc.a` under the tree was
a stale wrong-format archive). So your `errlst` regression is genuinely new
ground my code never validated; do not assume it's handled.

What my code *would* do with `text==0`: `body` is just `".text\n"`, so every
`first[i]<0`; `firsttext=-1`, making EARLY_CRITERIA clause (1) `(firsttext<0 ||
…)` true for **all** local data symbols → they all take ACTION A in symtab
order (fine *if* `dataok`). But an **ext** data symbol at a low index, before any
early chunk has set `earlyactive`, matches none of the three clauses (all
require `!ISEXT` except sticky-early), so it falls to ACTION C and emits only
`.globl` — its **content** would land in the monolithic tail, out of position
relative to the interleaved locals. That's an untested path and a plausible
source of exactly your kind of regression. If your pure-data objects mix ext and
local data symbols, I'd expect you to need an explicit "no text → treat the data
section as the primary stream" mode rather than leaning on the text-driven walk
at all.

### Bottom line on your candidate algorithm

Your derived shape is right, and it's the correct *generalization*: greedy
index-order walk; text-pin a symbol only when streaming to its text mention
wouldn't intern a later-index, not-yet-interned symbol first; otherwise fall back
to emitting the data fragment containing its first data mention at that walk
position (interning the run in table order = index order). My VAX code is the
special case where the guard is always satisfied, so it confirms your **fallback
mechanism** exactly and your **overall walk structure**, but not the **viability
guard** — that's yours to build because VAX never needed it. Implement it under
the same sweep-gated discipline (§1), keep it only at **≥ your current
best**, and treat the pure-data (Q5) path as its own separate mode rather than a
tweak to the text-driven walk.
