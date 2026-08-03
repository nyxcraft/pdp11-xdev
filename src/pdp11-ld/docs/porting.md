# Porting guide: ld (link editor)

The authentic 2.8BSD link editor (`ld/ld.c`, ~1670 lines of pre-1977 C). It
reads classic 2.8BSD a.out objects (and archives), resolves symbols across
them, relocates text and data, and writes a linked a.out. Unlike the
assembler, it *could* be ported by compiling the original source — the
changes are the usual LP64 ones, almost all concentrated at the file-I/O
boundary.

## The core problem: the I/O model assumes `sizeof(int) == 2`

On the PDP-11 `int` is 2 bytes, and `ld` reads and writes a.out files as a
stream of 16-bit *words* using `int` buffers — `read(fd, buf, 512)` fills
exactly 256 `int`s, header structs are copied word-by-word with
`mget((int*)&hdr, sizeof hdr)`, and `sizeof(struct)>>1` counts words.

On an LP64 host `int` is 4 bytes, so every one of those assumptions breaks:
`read(fd, int_buf, 512)` fills only 128 ints, `sizeof` of the structs
doubles, the word loops copy twice as much, and zero-extension vs.
sign-extension changes magic-number comparisons. The *arithmetic* (sizes,
addresses, relocation accumulators kept in `int`) is fine at 32 bits — only
the buffers and on-disk structs that are read/written as raw words must
become exactly 16 bits. The fix is to retype them `uint16_t` (not `short`:
zero-extension is required so e.g. `ARCMAGIC` 0177545 and `FMAGIC` 0407
compare equal after `get()`; a signed `short` would sign-extend 0177545 to a
negative `int` and break the magic test).

### The changes (all in `ld/ld.c`)

1. **Word buffers → `uint16_t`.**
   - `struct page { … int buff[256]; }` → `uint16_t buff[256]` (512 bytes =
     256 words, so `read(infil, buff, 512)` fills exactly 256 words).
   - `struct stream { int *ptr; … }` → `uint16_t *ptr`. `get()` returns
     `*sp->ptr++` (a `uint16_t` promoted to `int`, 0..65535).
   - `struct buf { … int *xnext; int iobuf[256]; }` → both `uint16_t`.
     `putw(w,b)` does `*xnext++ = w` (truncates `int`→`uint16_t`, correct).
     `flush()` computes the byte count as `(char*)xnext - (char*)iobuf`,
     which is now right. The `nleft` reset must use the **element** size,
     `sizeof(iobuf)/sizeof(*iobuf)` = 256, not `sizeof(int)` (= 128 on the
     host) — fixed in both `tcreat()` and `flush()`.

2. **`filhdr` → 16-bit fields.** The 8-field header (`fmagic`, `tsize`,
   `dsize`, `bsize`, `ssize`, `entry`, `pad`, `relflg`) is read with
   `mget((uint16_t*)&filhdr, sizeof filhdr)`, so all fields become `uint16_t`
   and `sizeof filhdr == 16` (8 words). Field names are kept.

3. **`struct symbol` → 12 bytes, word-packed.** `char sname[8]; char stype;
   char spare; int svalue` → `svalue` becomes `uint16_t` so `sizeof == 12`
   with no tail padding. The symbol-table write loop in `finishout()` and the
   `mput()` of `cursym` then walk it as `uint16_t*` (6 words/symbol). Storing
   final addresses (`sp->svalue += torigin`) in a `uint16_t` is exactly the
   16-bit target semantics — addresses wrap at 64 KiB as on the PDP-11.

4. **`archdr` → packed fixed-width.** The in-file V7 archive member header
   `char aname[14]; long atime; char auid,agid; int amode; long asize` →
   `int32_t atime; int16_t amode; int32_t asize` with
   `__attribute__((packed))`, so `sizeof == 26` (13 words) with no host
   alignment padding before the 4-byte fields.

5. **`mget`/`mput` operate on `uint16_t`.** Their internal `loc`/`p`
   pointers become `uint16_t*`, and every caller's `(int*)&struct` cast
   becomes `(uint16_t*)&struct`. Because the structs above are now exact word
   multiples, the `sizeof>>1` word copy is correct.

6. **`copy()`** reads a temp file back into `iobuf` (`uint16_t[256]`) and
   re-emits with `putw`; its `int *p` cursor becomes `uint16_t *p`.

No `MENLO_OVLY` (auto-overlay) support is built; the overlay globals are left
declared-but-unused (they compile fine). The original `#else MENLO_OVLY` /
`#endif MENLO_OVLY` token-after-directive style is tolerated with
`-Wno-endif-labels`.

### LP64 pointer issues

The usual class (K&R pointer-returning functions truncating to `int`) was
already guarded in this source: `lookup`/`slookup` (`struct symbol **`) and
`lookloc` (`struct symbol *`) carry explicit return types *and* forward
prototypes at the top of the file, so no new prototypes were needed. Building
first with `-Wint-conversion -Wint-to-pointer-cast -Wpointer-to-int-cast`
surfaced no truncation sites once the buffers were retyped.

### One host-libc fix

`mktemp(tfname)` needs a template ending in **six** `X`s on glibc (the
original `/tmp/ldaXXXXX` has five; glibc returns an empty string and the
`creat` then fails with "cannot create temp"). Added one `X`.

## Two `as` bugs found while verifying ld

Verifying *cross-object* relocation (which `ld`'s single-object path never
exercises) exposed two pre-existing assembler bugs — `ld` was correct:

1. **External relocation symbol index was always −1.** `as` assigned each
   symbol its table index inside `writeout()`, but the relocation words are
   stamped with `sym->index` during pass 2, which runs *before* `writeout()`.
   So every `REXT` relocation carried `(-1<<4)|REXT = 0177770`, and `ld`
   reported "Local symbol botch". Fixed by assigning indices between pass 1
   and pass 2 (same iteration order/predicate `writeout()` uses).

2. **Undefined references weren't marked external.** An undefined-but-
   referenced symbol (e.g. `csv`/`cret` in compiler output) was written with
   `n_type = N_UNDF` and *no* external bit, yet referenced via a `REXT`
   relocation. `ld` only enters `EXTERN+UNDEF` symbols into its
   local-resolution list, so the reference couldn't be resolved. In classic
   a.out an unresolved reference is `N_EXT|N_UNDF`; `as` now sets the external
   bit on undefined symbols.

## Verification

`tests/ld/link.sh` assembles and links two programs and checks the linked
a.out by reading its bytes with `od`/`dd` (no external tools):

- **Self-contained** (`mov`/`add`/`jmp` loop): output is `FMAGIC` with
  `relflg = 1` (fully relocated, no relocation bits left), text 14 bytes,
  `_start` a global text symbol at 0.
- **Two objects**, where A does `jsr pc,*$_foo` and B defines `_foo`: A's
  text is 8 bytes, so `_foo` lands at text offset 010; the test asserts the
  `jsr` operand word in the linked text equals `000010` — i.e. the external
  reference was relocated to `_foo`'s final address — and `nm` shows
  `000010 T _foo`.

Cross-checked with the GNU oracle (`pdp11-aout-objdump -D -b binary -m
pdp11`): the linked two-object text disassembles to

```
0: jsr pc, *$10      ; _start -> _foo at 010 (relocated)
4: jmp 0x4           ; Lh: self (pc-relative)
8: mov $10, r0       ; _foo
c: rts pc
```

Linking a real `cc -c` object (`h.o`) alone still fails — by design — with a
clean `Undefined: csv cret`, because the C-runtime helpers and `crt0`/libc do
not exist yet (the next stage).

## Build

```make
${BIN}/${PREFIX}-ld: ld/ld.c ${UCB_OBJS}
	${HOSTCC} ${O} ${COMPAT} -Icross -o $@ ld/ld.c ${UCB_OBJS}
```

`ld` uses `openlp` to find `-l` libraries (`lib/lib<x>.a`), so it links the
`libucbpath` objects, exactly like `cpp`.
