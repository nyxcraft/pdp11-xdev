/*
 * das -- a disassembler for 2.8BSD PDP-11 a.out, object (.o), and archive (.a)
 * files.  The inverse of this toolchain's `as'.
 *
 *  - A bare object (.o) disassembles to one listing.
 *  - A linked a.out is split back into per-object listings using the N_FN
 *    file-name symbols `ld' leaves in the symbol table (each marks where an
 *    input object's text begins).
 *  - An archive (.a) disassembles each member to its own listing.
 *
 * Available debugging symbols label functions (text), variables (data/bss),
 * and branch/call targets.  A single listing (a bare .o, or an a.out with no
 * N_FN boundaries) goes to stdout.  When there are several listings to separate
 * -- an a.out split by N_FN, or archive members -- each is written to its own
 * <stem>.<object>.dis file; -p forces all of them to stdout instead.
 *
 * Runs on the LP64 host; reads the 16-bit little-endian PDP-11 formats
 * explicitly (never by struct overlay), so it is endian/word-size clean.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "a.out.h"
#include "ar.h"

/* ---- the file being disassembled ---------------------------------------- */
static unsigned char *F; /* whole input file */
static long FLEN;

static int
w16(long off)
{ /* little-endian 16-bit word at byte offset */
	if (off < 0 || off + 1 >= FLEN)
		return 0;
	return F[off] | (F[off + 1] << 8);
}

/* ---- symbols ------------------------------------------------------------ */
struct sym {
	char name[40];
	int type;
	int value;
};

static int NewFmt; /* 2.11 string-table symbol format (8-byte nlist + strtab) */
static struct sym *Sym;
static int NSym;

#define BASETYPE(t) ((t) & 037) /* segment, masking the EXT bit */
#define ISEXT(t) ((t) & 040)

/* output mode + segment/relocation geometry (file offsets) of the current
 * object, so the operand formatter can turn relocated words into symbols. */
static int Asm;				  /* -a: emit reassemblable source */
static long Tbase, Dbase, RTbase, RDbase; /* text/data + their reloc areas */
static int Tsize, Dsize, HasReloc;
static int AuxFrozen; /* synthetic-local digits assigned (scan pass complete) */
/* -a interleaved segment emission: the compiler intersperses .bss/.data/.text
 * segment switches, so data/bss symbols are interned mid-file; we reproduce that
 * order by weaving each defined data/bss symbol's definition (label + `.=.+' or
 * word content) into the -a stream at its symtab-index position, rather than
 * dumping .text then .data then .bss.  CurSeg tracks the emitted segment (so
 * `.data'/`.bss'/`.text' directives are lazy); WmData/WmBss are the highest
 * address already emitted per segment; InFragment suppresses re-entrant flushing
 * while a woven fragment is being dumped. */
static int CurSeg = -1, InFragment, WmData, WmBss;
static char *ForceSynth; /* per-symbol: spell every REFERENCE to this defined
			  * local through a synthetic numbered local instead of
			  * its name, so the name interns at its LABEL line (its
			  * symtab-order position) -- set when body_respell hits a
			  * mention with NO interned lower anchor (V5 fx8.o's
			  * early `tst nlflg', whose slot is LAST).  Byte-free:
			  * same value, same relocation. */
static int FSNew;	 /* a ForceSynth flag was set this pass: rebuild */
static char *asmname(char *n);
static char *PromoteWish; /* per-symbol: the walk could not pin this defined
			   * LOCAL in slot order -- on the rebuild, symword spells
			   * any internal word whose value is congruent to it
			   * (offset 0: a same-address alias chosen wrongly, V6
			   * alloc.o's `initl'/`b1'; offset 0o40000/0o100000/
			   * 0o140000: 1972 fortran threaded-code flag bits,
			   * tanh.o's `.tanh+40000') THROUGH it, so its name
			   * interns at the slot the original order demands.
			   * One spell per wish (`used'): later words revert to
			   * the natural anchor (alloc.o's later `$b1'). */
static char *PromoteUsed;
static int EscPhase;	 /* escalation phase: 0 = promote wishes only; 1 = assign-
			  * pins allowed; 2 = ForceSynth allowed.  Advanced only
			  * when a dry run registers nothing new yet still
			  * degrades -- later-stage measures never fire while an
			  * earlier stage could still resolve the order (fp.o's
			  * bsign was force-synth'd by a stale early round). */
static int NeedEsc;	 /* this dry run hit a dead end it could not register */
static int WalkDegraded; /* this dry run degraded somewhere (count) */
static int AnnealEval;	 /* trial run: count degradation, register nothing */
static int *SynthFirstN; /* per-symbol: spell this many LEADING mentions
			  * synthetically, the rest by name -- 1972 threaded
			  * code cites a label early as a numbered local and
			  * later by name (libb printf.o's l3: top jump-table
			  * word `2f'-style, slot-defining word `l3').  The
			  * dead-end ladder grows N until the order resolves. */
static int *MentionSeen; /* mentions of each symbol seen so far this
			  * emitter run (scan or body build) */
/* SynthFirstN gate: this mention of `sym' should be spelled SYNTHETICALLY
 * (one of its leading N); counts the occurrence.  Deterministic across the
 * scan pass and body builds -- MentionSeen resets before each. */
static int Buffering2E(void); /* emission-pass predicate, defined below */

/* a ForceSynth'd symbol sits exactly at (seg-less) address `a': references
 * must synthesize, not anchor to a lower named label */
static int
fs_at(int a)
{
	int i;
	if (!ForceSynth)
		return 0;
	for (i = 0; i < NSym; i++)
		if (ForceSynth[i] && (Sym[i].value & 0xffff) == (a & 0xffff))
			return 1;
	return 0;
}

static int
synth_gate(int sym)
{
	if (!SynthFirstN || !SynthFirstN[sym])
		return 0;
	if (!Buffering2E())
		return 0; /* only real emission passes:
			   * walk-time probes (alias_canonical)
			   * must not consume the count */
	if (MentionSeen[sym] >= SynthFirstN[sym])
		return 0;
	MentionSeen[sym]++;
	return 1;
}

static char *AssignPin;	 /* per-symbol: pin a defined LOCAL at its slot with a
			  * plain ASSIGNMENT `name = anchor+off' (identical
			  * symtab entry to a label, but flushable anywhere --
			  * V6 alloc.s' `frend = frlist+40.'), suppressing its
			  * label line.  The escalation between a promote WISH
			  * and ForceSynth. */
static char *LabelOut;	 /* symbol DEFINED this walk pass (label line, cast,
			  * alias/pin/dup assignment) -- the -y sweep-up
			  * rescues the rest */
static char *LabelBuilt; /* label line present in the BUILT bodies --
			  * stable across the dry and real walk passes
			  * (emit-time marks reset per pass from this) */
static char *DupLocal;	 /* defined LOCAL whose name another defined local also
			  * carries: an sdb TILDE entry (`~keyblk=L15' -- the real
			  * L-label was stripped; the ~ namespace is unhashed, so
			  * duplicates coexist).  Its label line is suppressed, its
			  * references go synthetic (ForceSynth), and the walk pins
			  * it with a flushable `~name=anchor+off' at its slot. */
static int seg_mismatch(int i);
static int type_donor(int bt, int self);

/* as's keyword table (shared pure-data header): a symbol whose name
 * collides with a keyword can only be mentioned through the `~' marker,
 * which bypasses as's symbol-table lookup (V1 bos's undef local `halt') */
struct op {
	char *name;
	int type;
	int opcode;
};

#include "../pdp11-as/optab.h" /* single source of truth: the assembler's opcode table */

static int
as_keyword(char *n)
{
	struct op *o;
	for (o = optab; o->name; o++)
		if (!strcmp(o->name, n))
			return 1;
	return 0;
}

static int
assign_anchor_exists(int i)
{ /* a lower same-segment plain local
   * AssignPin could anchor to */
	int j;
	for (j = 0; j < i; j++) { /* EARLIER slot only: `l1 = l3' with l3 still
				   * undefined makes l1 undefined (printn.o) */
		if (!Sym[j].name[0] || ISEXT(Sym[j].type) || Sym[j].name[0] == '~')
			continue;
		if (BASETYPE(Sym[j].type) != BASETYPE(Sym[i].type))
			continue;
		if ((Sym[j].value & 0xffff) > (Sym[i].value & 0xffff))
			continue;
		if ((DupLocal && DupLocal[j]) || (AssignPin && AssignPin[j]) || (ForceSynth && ForceSynth[j]))
			continue;
		return 1;
	}
	return 0;
}

static int Buffering;			     /* building a pure segment-body buffer: labels() emits only
					      * the label line (no declaration flushing, no segment switch,
					      * no once-only marks) -- the index-order walk over the finished
					      * bodies places every declaration and does all the interning
					      * bookkeeping itself */
static int is_undef_ext(int i);		     /* fwd: undefined external */
static int is_common(int i);		     /* fwd: `.comm' common */
static char *alias_canonical(int i);	     /* fwd: earlier same-address local label */
static int haslabel(int addr, int seg);	     /* fwd */
static int is_seg_defined(int i);	     /* fwd: defined data/bss symbol */
static void emit_fragment(int i, FILE *out); /* fwd: weave a data/bss fragment */
static void seg_switch(int seg, FILE *out);  /* fwd: lazy .text/.data/.bss */
static int InsnMinRef;			     /* smallest undefined-external index referenced by the
					      * instruction being decoded (NSym = none) -- so the
					      * declarations that precede it flush before it is mentioned */
static int FirstDef;			     /* index of the first symbol that is not an undefined
					      * external -- everything before it is a top-of-file `.comm'
					      * common (interned there, ahead of the code references) */
static char *Referenced;		     /* per-symbol: cited by some relocation entry */
static int OvObj;			     /* built by the overlay assembler (`as -V'): an REXT
					      * reloc cites a DEFINED symbol (see build_referenced) */
static int V7Obj;			     /* built by V7 as: a named LOCAL-undefined symbol (type
					      * 0000) with no ovas telltale -- V7 as does not
					      * auto-externalize undefineds (its -u flag ENABLED that;
					      * 2.x made it the default).  Reassemble with `as -7'. */
static int LocalOnly;			     /* labelat(): skip globals (ovas internal reloc) */
static char *SymEmitted;		     /* per-symbol: its label line already emitted (a woven
					      * fragment and the trailing pass must not both define it,
					      * which would be a `redefined symbol' assembly error) */
static int *RefRank;			     /* per-symbol: order of its first code reference
					      * (-1 if never referenced), filled by the scan pass */
static int NRefSeen;			     /* running rank counter during the scan pass */
static int *Tmo;			     /* per-symbol: rank of its first mention in the TEXT
					      * scan (-1 if never), used to tell a data/bss symbol
					      * interned at its definition (defined-first: weave it in
					      * index order) from one interned at a code reference
					      * (reference-first: leave to the trailing address-order
					      * pass, its index already set by the reference) */
static int NTmo, InTextScan;

static int
Buffering2E(void)
{
	return Buffering || InTextScan;
} /* real emission
   * passes only: walk-time probes (alias_canonical) must not consume
   * the synth_gate count */

static char *WeaveEarly; /* per-symbol: a defined-first data/bss symbol -- weave */
static char *ForceGlobl; /* per-symbol: emit an explicit `.globl' -- the
			  * symbol was declared ahead of the code (its symtab
			  * index precedes its reference order), so ref-interning
			  * it would place it out of order (see compute_forceglobl) */

/* record that the instruction/word being emitted mentions symtab index `idx',
 * so declarations that precede `idx' flush before this mention (a forward
 * branch or an address-of immediate interns its target -- and any preceding
 * parameter/register declaration -- exactly here) */
/* record the first-mention rank of symbol `idx' during the TEXT scan pass */
static void
text_mention(int idx)
{
	if (!AuxFrozen && InTextScan && Tmo && idx >= 0 && idx < NSym && Tmo[idx] < 0)
		Tmo[idx] = NTmo++;
}

static void
noteref(int idx)
{
	if (idx >= 0 && idx < InsnMinRef)
		InsnMinRef = idx;
	text_mention(idx);
}

/* on-disk relocation type field (matches as.c) */
#define RABS 000
#define REXT 010

/* the relocation word for the operand/data word at file offset `wo' (0 none) */
static unsigned short *V1Rel; /* parallel relocation synthesized from a V1
			       * 0405 image's variable-length BIT STREAM (First
			       * Edition a.out(V), 11/3/71: 2-bit codes MSB-first,
			       * 00 abs / 01 relocatable / 10,11xx external, one per
			       * text word EXCLUDING the 12-byte header) */

static int
relat(long wo)
{
	if (V1Rel && wo >= Tbase && wo < Tbase + Tsize)
		return V1Rel[(wo - Tbase) / 2];
	if (!HasReloc)
		return 0;
	if (wo >= Tbase && wo < Tbase + Tsize)
		return w16(RTbase + (wo - Tbase));
	if (wo >= Dbase && wo < Dbase + Dsize)
		return w16(RDbase + (wo - Dbase));
	return 0;
}

/* if the word at `wo' relocates against an EXTERNAL symbol, format it with
 * `fmt' (one %s for the name) into `out' and return 1; else 0. */
static int
relexp(long wo, int addend, char *fmt, char *out)
{
	int rel = relat(wo);
	if ((rel & 016) == REXT && (rel >> 4) < NSym) {
		char s[48];
		int si = rel >> 4;
		text_mention(si);
		if (BASETYPE(Sym[si].type) == N_UNDF) {
			if (si < InsnMinRef)
				InsnMinRef = si;
			if (!AuxFrozen && RefRank && RefRank[si] < 0)
				RefRank[si] = NRefSeen++;
		}
		if (addend)
			sprintf(s, "%s+%o", Sym[si].name, addend & 0177777);
		else
			strcpy(s, Sym[si].name);
		sprintf(out, fmt, s);
		return 1;
	}
	return 0;
}

/* Authentic `as' keeps numeric local labels (`1:'/`1f'/`1b') out of the symbol
 * table -- they resolve in-memory like 2BSD's curfb table.  So a reference whose
 * target has no named symbol gets an objdump-style synthetic label `.L<addr>',
 * emitted at the definition and used by every reference. */
static int Bsz; /* bss size (Tsize/Dsize already track text/data) */

static int
segof(int a)
{
	a &= 0xffff;
	if (a < Tsize)
		return N_TEXT;
	if (a < Tsize + Dsize)
		return N_DATA;
	if (a < Tsize + Dsize + Bsz)
		return N_BSS;
	return -1;
}

/* Every synthetic label is emitted as a NUMBERED local (`1:'/`1f'/`1b').
 * Authentic `as' keeps numbered locals OUT of the symbol table (they resolve
 * in-memory like 2BSD's curfb table) -- whereas a named `.L<addr>' is written
 * to the symtab, which would inflate the object's symbol count and shift every
 * relocation index, so a named synthetic can never round-trip.  The scan pass
 * records, per synthetic, its definition address and the span of addresses that
 * reference it; color_aux() then assigns digits 0-9 by interval coloring (two
 * synthetics may share a digit only if their reference spans do not overlap, so
 * no `Nf'/`Nb' ever resolves across an intervening same-digit definition).  A
 * synthetic that cannot be colored (>10 overlapping -- rare) falls back to a
 * named `.L<addr>' (that one object then won't round-trip, but nothing else). */
static int *AuxSet, *AuxLo, *AuxHi, *AuxDigit, *AuxBase, NAuxSet, AuxCap;

/* address-sorted (a 64KB stripped executable needs tens of thousands of
 * synthetics -- the old fixed 1024 silently dropped definitions) */
static int
aux_index(int a)
{
	int lo = 0, hi = NAuxSet - 1;
	a &= 0xffff;
	while (lo <= hi) {
		int m = (lo + hi) / 2;
		if (AuxSet[m] == a)
			return m;
		if (AuxSet[m] < a)
			lo = m + 1;
		else
			hi = m - 1;
	}
	return -1;
}

static int
in_aux(int a)
{
	return aux_index(a) >= 0;
}

/* record a synthetic defined at `targ', referenced from address `ref' */
static void
note_ref(int targ, int ref)
{
	int i;
	targ &= 0xffff;
	ref &= 0xffff;
	i = aux_index(targ);
	if (i < 0) {
		int lo = 0, hi = NAuxSet;
		if (NAuxSet >= AuxCap) {
			AuxCap = AuxCap ? AuxCap * 2 : 1024;
			AuxSet = realloc(AuxSet, AuxCap * sizeof(int));
			AuxLo = realloc(AuxLo, AuxCap * sizeof(int));
			AuxHi = realloc(AuxHi, AuxCap * sizeof(int));
			AuxDigit = realloc(AuxDigit, AuxCap * sizeof(int));
			AuxBase = realloc(AuxBase, AuxCap * sizeof(int));
			if (!AuxSet || !AuxLo || !AuxHi || !AuxDigit || !AuxBase) {
				NAuxSet = 0;
				AuxCap = 0;
				return;
			}
		}
		while (lo < hi) {
			int m = (lo + hi) / 2;
			if (AuxSet[m] < targ)
				lo = m + 1;
			else
				hi = m;
		}
		memmove(AuxSet + lo + 1, AuxSet + lo, (NAuxSet - lo) * sizeof(int));
		memmove(AuxLo + lo + 1, AuxLo + lo, (NAuxSet - lo) * sizeof(int));
		memmove(AuxHi + lo + 1, AuxHi + lo, (NAuxSet - lo) * sizeof(int));
		memmove(AuxDigit + lo + 1, AuxDigit + lo, (NAuxSet - lo) * sizeof(int));
		memmove(AuxBase + lo + 1, AuxBase + lo, (NAuxSet - lo) * sizeof(int));
		{
			int k;
			for (k = 0; k < NAuxSet - lo; k++)
				if (AuxBase[lo + 1 + k] >= lo)
					AuxBase[lo + 1 + k]++;
		}
		NAuxSet++;
		i = lo;
		AuxSet[i] = targ;
		AuxLo[i] = AuxHi[i] = targ;
		AuxDigit[i] = -1;
		AuxBase[i] = -1;
	}
	if (ref < AuxLo[i])
		AuxLo[i] = ref;
	if (ref > AuxHi[i])
		AuxHi[i] = ref;
}

static char *
synthname(int a)
{
	static char b[16];
	sprintf(b, ".L%o", a & 0xffff);
	return b;
}

/* assign numbered-local digits by interval coloring (definitions already in
 * address order; a digit is barred for a synthetic if an already-colored
 * synthetic whose span overlaps this one's holds it).  The overlap scan walks
 * backward only while spans can still reach -- spans are intervals on one
 * axis, so once AuxHi[b] < AuxLo[a] stays true for all earlier b with smaller
 * spans this remains O(n * overlap-degree) in practice. */
static int
aux_root(int i)
{
	while (AuxBase[i] >= 0)
		i = AuxBase[i];
	return i;
}

static void
color_aux(void)
{
	int i, j, merged;
	do {
		/* color the BASE spots (merged spots resolve through their root) */
		for (i = 0; i < NAuxSet; i++)
			if (AuxBase[i] < 0)
				AuxDigit[i] = -1;
		for (i = 0; i < NAuxSet; i++) {
			int used = 0, d;
			if (AuxBase[i] >= 0)
				continue;
			for (j = i - 1; j >= 0; j--) {
				if (AuxBase[j] >= 0 || AuxDigit[j] < 0)
					continue;
				if (AuxHi[j] < AuxLo[i])
					continue; /* no overlap */
				if (AuxLo[j] > AuxHi[i])
					continue;
				used |= 1 << AuxDigit[j];
				if ((used & 01777) == 01777)
					break; /* all ten taken */
			}
			for (d = 0; d < 10; d++)
				if (!(used & (1 << d))) {
					AuxDigit[i] = d;
					break;
				}
		}
		/* an uncolorable spot MERGES into a nearby lower spot of the same
		 * segment and its references become `<digit>f+off' -- byte-identical
		 * (same word value, same relocation) and it needs no definition line
		 * or digit of its own.  This is the genuine idiom: V6 uio.s writes
		 * `mov (sp)+,9f+4 ... 9: sys read; ..; ..' -- one label per sys
		 * block, offsets for the argument words; das's one-spot-per-address
		 * model exhausts the ten digits on such runs.  Re-color after each
		 * merge round: folded spans change the overlap graph. */
		merged = 0;
		for (i = 0; i < NAuxSet; i++) {
			int r;
			if (AuxBase[i] >= 0 || AuxDigit[i] >= 0)
				continue;
			for (r = i - 1; r >= 0 && AuxSet[i] - AuxSet[r] <= 020000; r--) {
				int root = aux_root(r);
				if (root == i || AuxSet[i] - AuxSet[root] > 020000)
					continue;
				if (segof(AuxSet[root]) != segof(AuxSet[i]))
					continue;
				if (AuxLo[i] < AuxLo[root])
					AuxLo[root] = AuxLo[i];
				if (AuxHi[i] > AuxHi[root])
					AuxHi[root] = AuxHi[i];
				AuxBase[i] = root;
				merged = 1;
				break;
			}
		}
	}
	while (merged);
	if (getenv("DASDBG")) {
		int k;
		for (k = 0; k < NAuxSet; k++)
			fprintf(stderr, "aux %06o span [%06o,%06o] digit %d base %d\n",
				AuxSet[k], AuxLo[k], AuxHi[k], AuxDigit[k], AuxBase[k]);
	}
	AuxFrozen = 1;
}

/* the definition token for the synthetic at `addr'.  Numbered locals are used
 * only in -a mode after coloring (AuxFrozen); the human-readable listing and the
 * -a scan pass (digits not yet assigned) fall back to a named `.L<addr>'. */
/* Numbered-local DIRECTION letters (`5f'/`5b') encode STREAM order -- but the
 * walk reorders body pieces, so a direction computed from ADDRESS order can
 * point the wrong way (regtab.o: the data piece holding `5:' streams first,
 * yet the text `br 5f' says forward).  During body build every digit-form
 * def/ref is recorded (call order == body text order); after the bodies are
 * final they are located by pattern scan, a DRY RUN of the walk assigns each
 * its global stream position, and the direction chars are flipped in place
 * (one byte -- piece boundaries stay valid) before the real walk streams. */
static int *FNAddr, NFNAddr, FNCap; /* aux addresses forced back to NAMED
				     * synthetics: their placeholder digit could not
				     * be stream-colored either, and a named .L local
				     * is strippable by the ld tiers (ioinit.o) */

static int
fn_named(int a)
{
	int i;
	for (i = 0; i < NFNAddr; i++)
		if (FNAddr[i] == (a & 0xffff))
			return 1;
	return 0;
}

static struct auxpat {
	int body;
	long off;
	int root;
	long gpos;
} *ARef, *ADef;

static int NARef, NADef, ARefCap, ADefCap;
static int *RefCall, *DefCall, NRefCall, NDefCall, RefCallCap, DefCallCap;

static void
auxcall(int **lst, int *n, int *cap, int root)
{
	if (*n >= *cap) {
		*cap = *cap ? *cap * 2 : 64;
		*lst = realloc(*lst, *cap * sizeof(int));
		if (!*lst) {
			*n = 0;
			*cap = 0;
			return;
		}
	}
	(*lst)[(*n)++] = root;
}

static char *
synthdef(int addr)
{
	int i = aux_index(addr);
	static char b[16];
	if (i >= 0 && AuxBase[i] >= 0)
		return 0; /* merged: no definition of its own */
	if (AuxFrozen && i >= 0 && AuxDigit[i] >= 0) {
		if (Buffering)
			auxcall(&DefCall, &NDefCall, &DefCallCap, i);
		sprintf(b, "%d", AuxDigit[i]);
		return b;
	}
	/* address-space coloring failed (ndbm.o's lone bss cluster whose span
	 * covers most of the object): emit a PLACEHOLDER digit -- the stream
	 * recolorer assigns the real one from stream intervals.  A named .L
	 * synthetic would mint a symtab entry the original does not have. */
	if (AuxFrozen && Buffering && i >= 0 && AuxBase[i] < 0 && !fn_named(addr)) {
		auxcall(&DefCall, &NDefCall, &DefCallCap, i);
		strcpy(b, "0");
		return b;
	}
	return synthname(addr);
}

static char *
synthref(int targ, int ref)
{
	int i = aux_index(targ);
	static char b[24];
	if (i >= 0 && AuxBase[i] >= 0) { /* merged: root anchor + offset */
		int root = aux_root(i), delta = AuxSet[i] - AuxSet[root];
		if (AuxFrozen && (AuxDigit[root] >= 0 || (Buffering && !fn_named(AuxSet[root])))) {
			if (Buffering)
				auxcall(&RefCall, &NRefCall, &RefCallCap, root);
			sprintf(b, "%d%c+%o", AuxDigit[root] >= 0 ? AuxDigit[root] : 0,
				(AuxSet[root] & 0xffff) > (ref & 0xffff) ? 'f' : 'b', delta);
		}
		else
			sprintf(b, "%s+%o", synthname(AuxSet[root]), delta);
		return b;
	}
	if (AuxFrozen && i >= 0 && (AuxDigit[i] >= 0 || (Buffering && !fn_named(targ)))) {
		if (Buffering)
			auxcall(&RefCall, &NRefCall, &RefCallCap, i);
		sprintf(b, "%d%c", AuxDigit[i] >= 0 ? AuxDigit[i] : 0, (targ & 0xffff) > (ref & 0xffff) ? 'f' : 'b');
		return b;
	}
	return synthname(targ);
}

/* a named symbol `l', else a synthetic reference to `targ' from `ref' when targ
 * is in a segment; else null (caller prints a raw address). */
static char *
orsynth(char *l, int targ, int ref)
{
	if (l)
		return l;
	/* a STRIPPED file has no relocation, so a numeric target reassembles to the
	 * identical bytes from ANY window base (pc-relative re-encodes against the
	 * same pc; branches take absolute expressions) -- and a synthetic label
	 * would need a definition that may fall mid-instruction (a separate-I&D
	 * D-space address aliasing into text).  All-numeric is exact by
	 * construction, so use no synthetics at all. */
	if (Asm && !HasReloc)
		return 0;
	if (segof(targ) < 0)
		return 0;
	if (targ & 1) { /* odd byte address: anchor to the even word + 1 (labels sit
			 * only at even boundaries; e.g. px patches a header byte) */
		static char b[24];
		note_ref(targ & ~1, ref);
		sprintf(b, "%s+1", synthref(targ & ~1, ref));
		return b;
	}
	note_ref(targ, ref);
	return synthref(targ, ref);
}

static int Iaddr; /* address of the instruction currently being decoded */

/* ---- control-flow (recursive-descent) code/data map --------------------- *
 * Linear sweep decodes inline data (jump tables, a `jsr r5,error; 'x' char
 * argument whose value happens to be a `jmp' opcode) as instructions, which
 * fabricates bogus far branch targets.  Before emitting, we walk the actual
 * control flow from known entry points (every defined text symbol) to mark
 * which bytes are reachable as CODE; everything else is emitted as data. */
static unsigned char *Mark;  /* per text byte: 0 unseen, 1 instr-start, 2 seen */
static unsigned char *DMark; /* per data byte (addr-Tsize): 1 = instruction start --
			      * CODE assembLED INTO .data (the pascal FP interpreter's
			      * trap stubs); signal: pcrel relocs in the data segment */
static int HasDataCode;	     /* markdata committed at least one data-code region */
static unsigned char *Targ;  /* per text byte: a branch/jsr/jmp target lands here */
#define CF_NEXT 0	     /* falls through */
#define CF_COND 1	     /* conditional branch / sob: fall-through AND target */
#define CF_JUMP 2	     /* unconditional br/jmp: target only, no fall-through */
#define CF_CALL 3	     /* jsr: fall-through AND target */
#define CF_STOP 4	     /* rts/rti/rtt/halt/computed jmp: no fall-through */
static int CFtype, CFtarg;   /* decode() sets these for the decoded instruction */
static int Optarg;	     /* numeric pcrel/abs target of the last operand, or -1 */
static int SpliceRaw;	     /* decode() found a pc-relative operand whose index
			      * word carries NO relocation: as unconditionally
			      * relocates pcrel words (doreloc: even an absolute
			      * target gets 01), so instruction syntax cannot
			      * reproduce reloc 0 -- the emitter must splice the
			      * raw words instead (V5 c00's keyword-table strings
			      * misdecoded as pc-indexed adds) */
static int CFsysargs;	     /* inline argument words following a `sys' instruction */
static int CFjsrinline;	     /* `jsr r5,rsave' (V5-era C prologue): ONE inline
			      * frame-size word follows, unconditionally -- rsave
			      * itself does `sub (r0)+,sp; jmp (r0)'.  Unlike sys
			      * args (personality-dependent, pcrel-guarded) this
			      * needs no guard: the convention is the symbol. */

/* Number of inline argument words a `sys N' carries -- exactly what the 2BSD
 * kernel skips: `pc += sy_narg - sy_nrarg' (trap.c), the total args minus those
 * passed in registers, from the sysent[64] table (sys/sys/sysent.c).  indir (0)
 * reads one word (the indirect block address).  This lets the control-flow walk
 * step over `sys lseek; 0; 0; 0' style inline args to reach the code beyond.
 * Row 3 (the Berkeley calls 48-63: sig/rtp/acct/phys/lock/ioctl/reboot/mpxchan/
 * vfork/local/exece/umask/chroot) is transcribed from 2.9 sysent.c narg-nrarg;
 * it was wrong before, corrupting the decode of every `sys local' indirect stub. */
static int J11Dec;	   /* -J: decode late-hardware instructions (MFPT, SPL,
			    * CSM, TSTSET/WRTLCK, FIS, MED/XFC, CIS) -- opt-in so
			    * the era corpora keep their byte-oracle .s shapes
			    * (a data word 000007 must stay numeric, not `mfpt') */
static int SymNoReloc = 1; /* DEFAULT ON: keep the symbol table of a
			    * RELOCATION-STRIPPED image (kernels, unstripped
			    * executables of every magic).  Labels are emitted at
			    * symbol addresses, the unexpressible go through
			    * `name = value ^ donor' casts; operands stay numeric
			    * (nothing relocates); the harness (ldnr.py) restores
			    * ld's symtab order and strips our relocation.  `-s'
			    * opts DOWN to all-numeric CONTENT mode (symbols
			    * dropped -- the old default); `-y' is accepted as a
			    * no-op for the older harness spelling.  Irrelevant
			    * when relocation is present: those gates all require
			    * !HasReloc. */
static int V2Sys;	   /* -2: 1972 sysent -- inline arg counts from v1inline[]
			    * (attested: s2's intr.o carries `sys 33; 40050' -- one
			    * word -- twice, each followed by rts), and the V1-era
			    * relocation base `..' is 0o40000 (programs load there):
			    * outw ADDS dotdot to internal non-pcrel words and
			    * SUBTRACTS it from abs/ext pcrel words.  das undoes the
			    * bias when resolving and emits `.. = 40000' so as
			    * re-applies it. */
#define V2BASE 040000
static int V2Bias; /* this object's internal words carry the +V2BASE load
		    * bias (detected: user programs yes, the kernel no) */
static int V6Sys;  /* -6: V5/V6-era sysent -- inline arg counts from
		    * v6inline[]; the load-bearing difference is trap 19.
		    * = seek (TWO inline words), not V7's lseek (three); a
		    * wrong count swallows the following instruction
		    * (fxe.o's `sys seek; 0; 2' right before `mov $-1,r0') */
static char sysinline[64] = {
	1,
	0,
	0,
	2,
	2,
	2,
	0,
	0,
	2,
	2,
	1,
	2,
	1,
	0,
	3,
	2,
	3,
	1,
	2,
	3,
	0,
	3,
	1,
	0,
	0,
	0,
	3,
	0,
	1,
	0,
	2,
	1,
	1,
	2,
	0,
	1,
	0,
	1,
	0,
	0,
	0,
	0,
	0,
	1,
	4,
	0,
	0,
	0,
	2,
	0,
	0,
	1,
	3,
	1,
	3,
	2,
	4,
	0,
	1,
	3,
	1,
	1,
	0,
	0,
};

/* The TRUE era argument counts, replacing the 2.9-table-plus-overrides
 * guesswork the personalities used before (byte-harmless -- a wrong count
 * dropped the walk into the data fallback -- but shapeless).
 *
 * 1971-72: nwords-nregs per call, from the surviving V1 kernel listing
 * (unix72 pages, sysent: u4.s dispatch) as transcribed in apout's
 * v1trap.c; trap 0 is RELE (no inline word), not indir -- V1 has no
 * indirect call.  Cross-checked: intr (033) carries ONE word, exactly
 * what s2's intr.o shows. */
static char v1inline[35] = {
	0,
	0,
	0,
	2,
	2,
	2,
	0,
	0,
	2,
	2,
	1,
	2,
	1,
	0,
	2,
	2,
	2,
	1,
	2,
	2,
	2,
	2,
	1,
	0,
	0,
	0,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	0,
};
/* V4-V6: the count column of ken/sysent.c (V6 shown; V4/V5 agree for
 * every implemented call).  Differs from the 2.9 table at 19 (seek=2,
 * not lseek's 3), 30 (smdate=1, not utime's 2), 33/35 (nosys/sleep=0,
 * not access/ftime's 1), and everything past 48 (unassigned). */
static char v6inline[64] = {
	1,
	0,
	0,
	2,
	2,
	2,
	0,
	0,
	2,
	2,
	1,
	2,
	1,
	0,
	3,
	2,
	2,
	1,
	2,
	2,
	0,
	3,
	1,
	0,
	0,
	0,
	3,
	0,
	1,
	0,
	1,
	1,
	1,
	0,
	0,
	0,
	0,
	1,
	0,
	0,
	0,
	0,
	0,
	1,
	4,
	0,
	0,
	0,
	2,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
};

/* Era syscall NAMES, emitted as a `/ name' comment after `sys N' --
 * comments survive re-assembly byte-exactly, so annotation is always on.
 * Sources: 1971-72 = as29.s keyword list (kernel dispatch order);
 * V4-V6 = ken/sysent.c; V7/2.9 = /usr/include/sys.s (the include file
 * that replaced the keywords); 2.11 = <syscall.h> (the cpp constants
 * that replaced the include file). */
static char *sysnm1[35] = {
	"rele",
	"exit",
	"fork",
	"read",
	"write",
	"open",
	"close",
	"wait",
	"creat",
	"link",
	"unlink",
	"exec",
	"chdir",
	"time",
	"makdir",
	"chmod",
	"chown",
	"break",
	"stat",
	"seek",
	"tell",
	"mount",
	"umount",
	"setuid",
	"getuid",
	"stime",
	"quit",
	"intr",
	"fstat",
	"cemt",
	"mdate",
	"stty",
	"gtty",
	"ilgins",
	"nice",
};
static char *sysnm6[64] = {
	"indir",
	"exit",
	"fork",
	"read",
	"write",
	"open",
	"close",
	"wait",
	"creat",
	"link",
	"unlink",
	"exec",
	"chdir",
	"time",
	"mknod",
	"chmod",
	"chown",
	"break",
	"stat",
	"seek",
	"getpid",
	"mount",
	"umount",
	"setuid",
	"getuid",
	"stime",
	"ptrace",
	0,
	"fstat",
	0,
	"smdate",
	"stty",
	"gtty",
	0,
	"nice",
	"sleep",
	"sync",
	"kill",
	"switch",
	0,
	0,
	"dup",
	"pipe",
	"times",
	"profil",
	0,
	"setgid",
	"getgid",
	"signal",
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
};
static char *sysnm29[64] = {
	"indir",
	"exit",
	"fork",
	"read",
	"write",
	"open",
	"close",
	"wait",
	"creat",
	"link",
	"unlink",
	"exec",
	"chdir",
	"time",
	"mknod",
	"chmod",
	"chown",
	"break",
	"stat",
	"lseek",
	"getpid",
	"mount",
	"umount",
	"setuid",
	"getuid",
	"stime",
	"ptrace",
	"alarm",
	"fstat",
	"pause",
	"utime",
	"stty",
	"gtty",
	"access",
	"nice",
	"ftime",
	"sync",
	"kill",
	"csw",
	"setpgrp",
	0,
	"dup",
	"pipe",
	"times",
	"profil",
	"getgrp",
	"setgid",
	"getgid",
	"signal",
	"rtp",
	"setgrp",
	"acct",
	"phys",
	"lock",
	"ioctl",
	"reboot",
	"mpx",
	"vfork",
	"local",
	"exece",
	"umask",
	"chroot",
	0,
	0,
};
static char *sysnm211[157] = {
	0,
	"exit",
	"fork",
	"read",
	"write",
	"open",
	"close",
	"wait4",
	"creat",
	"link",
	"unlink",
	"execv",
	"chdir",
	"fchdir",
	"mknod",
	"chmod",
	"chown",
	0,
	0,
	"lseek",
	"getpid",
	"mount",
	"umount",
	0,
	"getuid",
	0,
	"ptrace",
	0,
	0,
	0,
	0,
	0,
	0,
	"access",
	0,
	0,
	"sync",
	"kill",
	"stat",
	0,
	"lstat",
	"dup",
	"pipe",
	0,
	"profil",
	0,
	0,
	"getgid",
	0,
	0,
	0,
	"acct",
	"phys",
	"lock",
	"ioctl",
	"reboot",
	0,
	"symlink",
	"readlink",
	"execve",
	"umask",
	"chroot",
	"fstat",
	0,
	"getpagesize",
	"mremap",
	"vfork",
	0,
	0,
	"sbrk",
	"sstk",
	"mmap",
	0,
	"munmap",
	"mprotect",
	"madvise",
	"vhangup",
	0,
	"mincore",
	"getgroups",
	"setgroups",
	"getpgrp",
	"setpgrp",
	"setitimer",
	0,
	"swapon",
	"getitimer",
	"gethostname",
	"sethostname",
	"getdtablesize",
	"dup2",
	"getdopt",
	"fcntl",
	"select",
	"setdopt",
	"fsync",
	"setpriority",
	"socket",
	"connect",
	"accept",
	"getpriority",
	"send",
	"recv",
	"sigreturn",
	"bind",
	"setsockopt",
	"listen",
	0,
	"sigvec",
	"sigblock",
	"sigsetmask",
	"sigpause",
	"sigstack",
	"recvmsg",
	"sendmsg",
	0,
	"gettimeofday",
	"getrusage",
	"getsockopt",
	0,
	"readv",
	"writev",
	"settimeofday",
	"fchown",
	"fchmod",
	"recvfrom",
	"setreuid",
	"setregid",
	"rename",
	"truncate",
	"ftruncate",
	"flock",
	0,
	"sendto",
	"shutdown",
	"socketpair",
	"mkdir",
	"rmdir",
	"utimes",
	0,
	"adjtime",
	"getpeername",
	"gethostid",
	"sethostid",
	"getrlimit",
	"setrlimit",
	"killpg",
	0,
	"setquota",
	"quota",
	"getsockname",
	0,
	"nostk",
	"fetchi",
	"ucall",
	"fperr",
	"gldav",
};

/* --sys=bsd211: 2.11's trap convention (pdp/trap.c): the full low byte
 * indexes sysent (no 0200 stack-args bit -- args ALWAYS come off the
 * user stack via copyin), syscall 0 is illegal (indir is gone), and NO
 * call carries inline argument words. */
static int Sys211;
static int NeedTab211; /* mfpi-family instruction with a pcrel operand seen:
			* output uses the real mnemonics, banner routes the
			* harness to `as --isa=bsd211' (System III mch_i.o) */

/* -y (no-reloc kernel) : a symbol whose TYPE segment mismatches its
 * address (v6/unix's `_klrint', TEXT-typed at a D-space address) cannot be
 * a label; it is emitted `name = value ^ donor' -- the `^' carries the
 * donor's type (genuine as semantics). */
static int V1Exec; /* 0405 image: bss follows text in core but has no
		    * header field -- a symbol beyond the image is a V1
		    * bss cell, cast-routed like any segment mismatch */

static int
seg_mismatch(int i)
{
	int bt = BASETYPE(Sym[i].type), sg;
	/* live on no-reloc images -- and TOTAL on V1 0405s (V1Rel):
	 * V1's relocation never cites symbols by index, its flag 3
	 * covers text and the data area alike, and its symtab order is
	 * the lost 1971 assembler's hash order -- so EVERY named symbol
	 * routes through a slot-ordered cast, and code references go
	 * through numbered locals (which the writer never emits). */
	if (V1Rel)
		return bt == N_TEXT || bt == N_DATA || bt == N_BSS;
	if (!SymNoReloc || HasReloc)
		return 0;
	if (bt != N_TEXT && bt != N_DATA && bt != N_BSS)
		return 0;
	sg = segof(Sym[i].value & 0xffff);
	if (sg < 0)
		return V1Exec;
	/* a DEFINED local whose name is an as KEYWORD would shadow the
	 * opcode from pass 2 on (one symbol table: `wait = 7.' semantics);
	 * m11 defines a data cell named `sys' yet executes sys instructions.
	 * The `~' cast keeps it out of the hashed namespace entirely. */
	if (!ISEXT(Sym[i].type) && as_keyword(Sym[i].name))
		return 1;
	return sg != bt;
}

static int
type_donor(int bt, int self)
{
	int j;
	for (j = 0; j < NSym; j++) {
		if (j == self || !Sym[j].name[0] || Sym[j].name[0] == '~')
			continue;
		if (BASETYPE(Sym[j].type) != bt)
			continue;
		if (seg_mismatch(j))
			continue;
		if ((DupLocal && DupLocal[j]) || (AssignPin && AssignPin[j]) || (ForceSynth && ForceSynth[j]))
			continue;
		if (segof(Sym[j].value & 0xffff) != bt)
			continue;
		return j;
	}
	return -1;
}

/* nearest defined label at or below `val' in segment `seg' (for symbol+offset
 * references like `0f+2'); returns its name (and *base = its value) or 0. */
static char *
nearestlabel(int val, int seg, int *base)
{
	int i, best = -1;
	for (i = 0; i < NSym; i++) {
		if (BASETYPE(Sym[i].type) != seg || !Sym[i].name[0])
			continue;
		if (ForceSynth && ForceSynth[i])
			continue; /* references go synthetic */
		if (Sym[i].name[0] == '~')
			continue; /* sdb tilde: define-only, a
				   * reference strips to nothing
				   * (ertst's bare `~' at bss) */
		if (seg_mismatch(i))
			continue; /* cast-routed: `~' namespace */
		if ((Sym[i].value & 0xffff) == (val & 0xffff) && synth_gate(i))
			continue; /* leading
				   * mention: synthesize */
		/* in an ovas (`as -V') object a DEFINED GLOBAL *TEXT* name in an
		 * operand stays EXTERNAL (as's emitextra: xseg==STEXT -- functions
		 * are thunkable), so an INTERNAL reference must never anchor to one
		 * -- libovjobs' vector entries are RTEXT|PCREL and anchoring them to
		 * `_mvector' re-relocated them external.  DATA/BSS globals resolve
		 * internally under -V (dz.c's static tables) and stay usable. */
		if (OvObj && ISEXT(Sym[i].type) && seg == N_TEXT)
			continue;
		/* a text symbol at an odd address sits inside a byte-split data
		 * word; its own definition IS emitted (the .byte split), so an
		 * EXACT match may use the name (fxg.o's `funtabt' interns by its
		 * early reference) -- but do not ANCHOR other targets to it */
		if (seg == N_TEXT && (Sym[i].value & 1) && (Sym[i].value & 0xffff) != (val & 0xffff))
			continue;
		if ((Sym[i].value & 0xffff) > (val & 0xffff))
			continue;
		if (best < 0 || Sym[i].value > Sym[best].value || (Sym[i].value == Sym[best].value && ISEXT(Sym[i].type)))
			best = i;
	}
	if (best < 0)
		return 0;
	noteref(best);
	*base = Sym[best].value;
	return Sym[best].name;
}

/* nearest defined label at or ABOVE `val' in segment `seg' -- the fallback for a
 * relocated reference that lands just below its segment's first label (a
 * `_KS-1' sentinel: the compiler's negative offset off a bss/data symbol), which
 * nearestlabel (at-or-below) can't express.  Returns the closest label > val so
 * the caller can render `label+<negative offset>'. */
static char *
nearestabove(int val, int seg, int *base)
{
	int i, best = -1;
	for (i = 0; i < NSym; i++) {
		if (BASETYPE(Sym[i].type) != seg || !Sym[i].name[0])
			continue;
		if (ForceSynth && ForceSynth[i])
			continue; /* references go synthetic */
		if (Sym[i].name[0] == '~')
			continue; /* sdb tilde: define-only */
		if (seg_mismatch(i))
			continue; /* cast-routed: `~' namespace */
		if (OvObj && ISEXT(Sym[i].type) && seg == N_TEXT)
			continue; /* see nearestlabel */
		if (seg == N_TEXT && (Sym[i].value & 1))
			continue;
		if ((Sym[i].value & 0xffff) < (val & 0xffff))
			continue;
		if (best < 0 || (Sym[i].value & 0xffff) < (Sym[best].value & 0xffff))
			best = i;
	}
	if (best < 0)
		return 0;
	noteref(best);
	*base = Sym[best].value;
	return Sym[best].name;
}

/* Format the relocated word at file offset `wo' as a symbolic reference
 * (`sym', `sym+off', or `Nf+off') so it reassembles with the same relocation.
 * `ref' is the referring address (for local-label forward/backward).  0 if the
 * word carries no usable relocation. */
static int
symword(long wo, int ref, char *out)
{
	int rel = relat(wo), val = w16(wo), type = rel & 016, base;
	char *nm;
	if (type == REXT && (rel >> 4) < NSym) {
		int si = rel >> 4;
		text_mention(si);
		if (BASETYPE(Sym[si].type) == N_UNDF) {
			if (si < InsnMinRef)
				InsnMinRef = si;
			if (!AuxFrozen && RefRank && RefRank[si] < 0)
				RefRank[si] = NRefSeen++;
		}
		nm = Sym[si].name;
		if (val)
			sprintf(out, "%s+%o", nm, val & 0177777);
		else
			strcpy(out, nm);
		return 1;
	}
	if (type == 002 || type == 004 || type == 006) { /* RTEXT/RDATA/RBSS */
		int seg = type == 002 ? N_TEXT : type == 004 ? N_DATA
							     : N_BSS;
		if (V2Bias && !(rel & 1))
			val = (val - V2BASE) & 0xffff; /* `..' bias */
		/* a ForceSynth'd symbol sits EXACTLY at this value: the reference
		 * must go synthetic -- falling through to nearestlabel would anchor
		 * it to the next lower NAMED label, minting a phantom mention there
		 * (libb printf.o: the l12-word came out `l3+362') */
		if (fs_at(val) && (nm = orsynth(0, val, ref)) != 0) {
			strcpy(out, nm);
			return 1;
		}
		if (PromoteWish) {
			int i2;
			for (i2 = 0; i2 < NSym; i2++) {
				int dl2;
				if (!PromoteWish[i2])
					continue;
				if (ForceSynth && ForceSynth[i2])
					continue;
				if (BASETYPE(Sym[i2].type) != seg)
					continue;
				dl2 = (val - Sym[i2].value) & 0xffff;
				if (dl2 & 037777)
					continue; /* offset 0 or a flag bit */
				if (dl2)
					sprintf(out, "%s+%o", asmname(Sym[i2].name), dl2);
				else
					strcpy(out, asmname(Sym[i2].name));
				return 1;
			}
		}
		if ((nm = nearestlabel(val, seg, &base)) != 0) { /* named symbol + offset */
			if (((val - base) & 0xffff))
				sprintf(out, "%s+%o", nm, (val - base) & 0xffff);
			else
				strcpy(out, nm);
			return 1;
		}
		/* A target below every label of its segment.  For DATA/BSS targets --
		 * and whenever the relocation's segment disagrees with where the
		 * address falls (a bss `_KS-1' sentinel sitting in the data range,
		 * where no synthetic can carry the right relocation) -- hand-written
		 * source anchors `label+<negative>' (fpsim's `reenter+177652', crypt's
		 * `_KS-1').  For a TEXT target inside the text range, the idiom is a
		 * numeric local instead (`$1f'): anchoring to a far text label would
		 * mention that name early and shift its symtab slot (m40.s's
		 * `mov $1f,nofault' came out `$call1+177640') -- prefer the faithful
		 * synthetic, keeping label+offset as the last resort. */
		if ((seg != N_TEXT || segof(val) != seg) && (nm = nearestabove(val, seg, &base)) != 0) {
			sprintf(out, "%s+%o", nm, (val - base) & 0177777);
			return 1;
		}
		if (segof(val) == seg && (nm = orsynth(0, val, ref)) != 0) {
			strcpy(out, nm);
			return 1;
		} /* synthesize local */
		if ((nm = nearestabove(val, seg, &base)) != 0) {
			sprintf(out, "%s+%o", nm, (val - base) & 0177777);
			return 1;
		}
		/* OUTSIDE the relocation's segment: one past its end (ecvt.o's
		 * `mov $buf+100.,r2', buf the last bss cell) or just below its
		 * start (crypt.o's `$KS-1' pre-decrement sentinel, numerically the
		 * last DATA byte).  No label can sit there and a synthetic minted
		 * at the address would carry the WRONG segment's relocation --
		 * anchor to the nearest in-segment word and spell the difference:
		 * same value, same relocation. */
		{
			int st = seg == N_TEXT ? 0 : seg == N_DATA ? Tsize
								   : Tsize + Dsize;
			int en = seg == N_TEXT ? Tsize : seg == N_DATA ? Tsize + Dsize
								       : Tsize + Dsize + Bsz;
			int anchor = val >= en ? en - 2 : st;
			/* a FLAG-BIT value (1972 idiom: handler|0o40000) anchors at its
			 * congruent in-range address, `<label>+40000' */
			{
				int fb;
				for (fb = 040000; fb; fb = (fb + 040000) & 0xffff) {
					int cg = (val - fb) & 0xffff;
					if (cg >= st && cg < en - 1 && !(cg & 1)) {
						anchor = cg;
						break;
					}
				}
			}
			if (en - 2 >= st && (val >= en || val < st)) {
				char *nm2 = orsynth(0, anchor, ref);
				if (nm2) {
					int dl = val - anchor;
					if (dl >= 0)
						sprintf(out, "%s+%o", nm2, dl & 0xffff);
					else
						sprintf(out, "%s-%o", nm2, (-dl) & 0xffff);
					return 1;
				}
			}
		}
	}
	return 0;
}

/* Best label for an address in a given segment (N_TEXT/N_DATA/N_BSS base).
 * Prefers an exact match, external over local; returns 0 if none. */
static char *
labelat(int addr, int seg)
{
	int i, best = -1;
	/* an exact-match PROMOTE wish wins over both the normal slot preference
	 * and any ForceSynth mark: the walk has determined this symbol must
	 * intern here (fp.o's bss cells, alloc.o's alias pair) */
	if (PromoteWish)
		for (i = 0; i < NSym; i++) {
			if (!PromoteWish[i])
				continue;
			if (ForceSynth && ForceSynth[i])
				continue;
			if (BASETYPE(Sym[i].type) != seg || !Sym[i].name[0])
				continue;
			if (Sym[i].name[0] == '~')
				continue; /* sdb tilde: define-only */
			if (seg_mismatch(i))
				continue; /* cast-routed: `~' namespace */
			if ((Sym[i].value & 0xffff) != (addr & 0xffff))
				continue;
			noteref(i);
			return Sym[i].name;
		}
	for (i = 0; i < NSym; i++) {
		if (BASETYPE(Sym[i].type) != seg || !Sym[i].name[0])
			continue;
		if (ForceSynth && ForceSynth[i])
			continue; /* references go synthetic */
		if (Sym[i].name[0] == '~')
			continue; /* sdb tilde: define-only */
		if (seg_mismatch(i))
			continue; /* cast-routed: `~' namespace */
		if (Sym[i].value == addr && synth_gate(i))
			return 0; /* leading mention:
				   * caller synthesizes */
		/* in an ovas object an INTERNALLY-relocated operand must anchor to a
		 * LOCAL name (a global would re-relocate external under -V); LocalOnly
		 * is set by those callers.  Branch targets carry no relocation word,
		 * so a global spelling there is byte-identical and stays allowed. */
		if (LocalOnly && ISEXT(Sym[i].type) && seg == N_TEXT)
			continue;
		if (Sym[i].value != addr)
			continue;
		if (best < 0 || (ISEXT(Sym[i].type) && !ISEXT(Sym[best].type)))
			best = i;
	}
	if (best >= 0)
		noteref(best);
	return best < 0 ? 0 : Sym[best].name;
}

/* ---- instruction decoder ------------------------------------------------
 * Decode one instruction starting at text offset `o' (its address is `addr').
 * Append the assembly to `buf'.  Returns the instruction length in bytes.
 * tbase/dbase: file offsets of the text/data segments, so index/immediate
 * words can be fetched; addr is the PDP-11 address for PC-relative targets. */
static char brmne[16][6] = {/* 0000400..0003400 and 0100000..0103400 */
			    "", "br", "bne", "beq", "bge", "blt", "bgt", "ble",
			    "bpl", "bmi", "bhi", "blos", "bvc", "bvs", "bcc", "bcs"};
static char sopmne[16][6] = {/* single-operand 0005000..0006700 */
			     "clr", "com", "inc", "dec", "neg", "adc", "sbc", "tst",
			     "ror", "rol", "asr", "asl", "mark", "mfpi", "mtpi", "sxt"};
static char sopmne_d[4][6] = {				       /* g 12..15 with bit 15 set: I/D-space + PS forms */
			      "mtps", "mfpd", "mtpd", "mfps"}; /* 0106400/0106500/0106600/0106700 */
static char dopmne[8][5] = {"", "mov", "cmp", "bit", "bic", "bis", "add", ""};
static char fpmne[8][6] = {"", "mulf", "modf", "addf", "movf", "subf", "cmpf", ""};

/* format a 6-bit operand; *po is the running text offset just past the opcode
 * word, advanced over any index/immediate word. *paddr tracks the PDP-11 PC. */
static char *
regname(int r) /* pc/sp idioms for r7/r6 */
{
	static char b[4];
	if (r == 7)
		return "pc";
	if (r == 6)
		return "sp";
	sprintf(b, "r%d", r);
	return b;
}

/* An internal (text/data/bss) reference whose resolved target is `targ' but
 * has no exact label -> `nearest+offset' (e.g. `0f+2'), keyed by the operand
 * word's relocation type at `wo'.  Returns 0 if not so relocated. */
static int
offref(int targ, long wo, char *out)
{
	int base, seg;
	char *nm;
	switch (relat(wo) & 016) {
	case 002:
		seg = N_TEXT;
		break;
	case 004:
		seg = N_DATA;
		break;
	case 006:
		seg = N_BSS;
		break;
	default:
		return 0;
	}
	if ((nm = nearestlabel(targ, seg, &base)) == 0)
		return 0;
	if ((targ - base) & 0xffff)
		sprintf(out, "%s+%o", nm, (targ - base) & 0xffff);
	else
		strcpy(out, nm);
	return 1;
}

static void
fmtop(int spec, long *po, int *paddr, char *out)
{
	int mode = (spec >> 3) & 7, reg = spec & 7, x, targ;
	char *rn = reg == 7 ? "pc" : reg == 6 ? "sp"
			     : reg == 5	      ? "r5"
					      : 0;
	char rb[4];
	if (!rn) {
		sprintf(rb, "r%d", reg);
		rn = rb;
	}
	switch (mode) {
		char *l;
		long wo;
	case 0:
		strcpy(out, rn);
		break;
	case 1:
		sprintf(out, "(%s)", rn);
		break;
	case 2:
		if (reg == 7) {
			char t[48];
			wo = *po;
			x = w16(*po);
			*po += 2;
			*paddr += 2; /* $imm */
			if (relexp(wo, x, "$%s", out))
				break;
			/* an immediate that is the ADDRESS of a local text/data/bss label
			 * (relocated RTEXT/RDATA/RBSS) -- e.g. `mov $L4,r4' loading a jump
			 * table base -- must reassemble as `$label', not a bare `$octal',
			 * or the label is not interned here and its symtab index shifts */
			if ((relat(wo) & 016) && symword(wo, Iaddr, t))
				sprintf(out, "$%s", t);
			else
				sprintf(out, "$%o", x);
		}
		else
			sprintf(out, "(%s)+", rn);
		break;
	case 3:
		if (reg == 7) {
			char t3[48];
			wo = *po;
			x = w16(*po);
			*po += 2;
			*paddr += 2; /* @#abs */
			if (relexp(wo, x, "*$%s", out))
				break;
			Optarg = x;
			/* a RELOCATED @#abs targets THIS object (the reloc names the
			 * segment) and must reassemble symbolically or the relocation
			 * is dropped -- libI77's computed `jmp *$314' (RTEXT). */
			if ((relat(wo) & 016) && symword(wo, Iaddr, t3)) {
				sprintf(out, "*$%s", t3);
				break;
			}
			/* unrelocated @#abs: in -a on a RELOCATABLE object it must stay
			 * NUMERIC -- naming a symbol that happens to sit at that address
			 * would ADD a relocation the original does not have (V6 nargs'
			 * self-modifying `jsr pc,*$0', runtime-patched, with _nargs at
			 * address 0).  Otherwise a named symbol if one sits exactly
			 * there, else the literal address (often a hardware/vector
			 * location) -- never a synthesized label; the address need not
			 * be in this object. */
			if (Asm && HasReloc) {
				sprintf(out, "*$%o", x);
				break;
			}
			l = labelat(x, N_TEXT);
			if (!l)
				l = labelat(x, N_DATA);
			if (!l)
				l = labelat(x, N_BSS);
			if (l)
				sprintf(out, "*$%s", l);
			else
				sprintf(out, "*$%o", x);
		}
		else
			sprintf(out, "*(%s)+", rn);
		break;
	case 4:
		sprintf(out, "-(%s)", rn);
		break;
	case 5:
		sprintf(out, "*-(%s)", rn);
		break;
	case 6:
		wo = *po;
		x = w16(*po);
		*po += 2;
		*paddr += 2;
		if (reg == 7) { /* PC-relative */
			targ = (*paddr + (short)x) & 0xffff;
			if (V2Bias && (relat(wo) & 017) == 1)
				targ = (targ + V2BASE) & 0xffff;   /* ABS pcrel: -.. bias */
			if (Asm && HasReloc && !(relat(wo) & 1)) { /* index word without the
								    * PCREL bit (unrelocated, or a plain-relocated table
								    * word swallowed by a junk decode -- sptab.o's
								    * `jmp *L16+145'): as always sets it on pcrel operands,
								    * so this is not assembler output -- splice raw */
				SpliceRaw = 1;
				sprintf(out, "%o", targ);
				break;
			}
			if (relexp(wo, (x + *paddr) & 0xffff, "%s", out))
				break;
			Optarg = targ;
			/* a pc-relative reference relocated RABS targets a fixed ABSOLUTE
			 * address (a kernel hardware location, or ovas's resolution of a
			 * local-undefined call to 0) -- NOT whatever symbol happens to
			 * live at that address; check BEFORE the label lookups */
			if (relat(wo) && (relat(wo) & 016) == RABS) {
				sprintf(out, "%o", targ);
				break;
			}
			LocalOnly = OvObj && (relat(wo) & 016) != 0; /* internal reloc: local anchor */
			/* the reloc names the target's SEGMENT: at a text/data boundary
			 * address two symbols coexist (fx8.o's `emes' one-past-text and
			 * `nlflg' at data start, both 0o1140) and only the one in the
			 * reloc's segment reassembles with the right relocation */
			switch (relat(wo) & 016) {
			case 002:
				l = labelat(targ, N_TEXT);
				break;
			case 004:
				l = labelat(targ, N_DATA);
				break;
			case 006:
				l = labelat(targ, N_BSS);
				break;
			default:
				l = labelat(targ, N_TEXT);
				if (!l)
					l = labelat(targ, N_DATA);
				if (!l)
					l = labelat(targ, N_BSS);
			}
			LocalOnly = 0;
			if (l)
				sprintf(out, "%s", l);
			/* 1972 (-2) sources spell an unlabeled pcrel target as a
			 * NUMBERED local -- a named `sym+off' anchor interns the
			 * anchor here (libb printf.o's entry `jmp l1+450' pulled
			 * l1 four slots early).  Later eras prefer `sym+off'. */
			else if (V2Sys && (l = orsynth(0, targ, Iaddr)))
				sprintf(out, "%s", l);
			else if (!fs_at(targ) && offref(targ, wo, out))
				; /* named symbol + offset */
			else if ((l = orsynth(0, targ, Iaddr)))
				sprintf(out, "%s", l); /* numbered local */
			else
				sprintf(out, "%o", targ);
		}
		else { /* indexed X(rn): the index word may itself be a relocated symbol
			* address -- `_sys_sig(r0)', `L4(r1)' -- which must reassemble as a
			* symbol so its relocation (and, for a first reference, its symtab
			* index) survive; a bare octal offset would drop both */
			char t[48];
			if ((relat(wo) & 016) && symword(wo, Iaddr, t))
				sprintf(out, "%s(%s)", t, rn);
			else
				sprintf(out, "%o(%s)", x & 0177777, rn);
		}
		break;
	case 7:
		wo = *po;
		x = w16(*po);
		*po += 2;
		*paddr += 2;
		if (reg == 7) {
			char o7[40];
			targ = (*paddr + (short)x) & 0xffff;
			if (V2Bias && (relat(wo) & 017) == 1)
				targ = (targ + V2BASE) & 0xffff;
			if (Asm && HasReloc && !(relat(wo) & 1)) { /* no PCREL bit: splice
								    * raw (see mode 6) */
				SpliceRaw = 1;
				sprintf(out, "*%o", targ);
				break;
			}
			/* pc-relative-deferred to a fixed ABSOLUTE address (unix.out's
			 * `mov $0,*10' -- the trap vector): numeric, exactly as mode 6 */
			if (relat(wo) && (relat(wo) & 016) == RABS) {
				sprintf(out, "*%o", targ);
				break;
			}
			if (relexp(wo, (x + *paddr) & 0xffff, "*%s", out))
				break;
			LocalOnly = OvObj && (relat(wo) & 016) != 0; /* internal reloc: local anchor */
			switch (relat(wo) & 016) {		     /* reloc segment first (see mode 6) */
			case 002:
				l = labelat(targ, N_TEXT);
				break;
			case 004:
				l = labelat(targ, N_DATA);
				break;
			case 006:
				l = labelat(targ, N_BSS);
				break;
			default:
				l = labelat(targ, N_DATA);
				if (!l)
					l = labelat(targ, N_TEXT);
			}
			LocalOnly = 0;
			if (l)
				sprintf(out, "*%s", l);
			else if (V2Sys && (l = orsynth(0, targ, Iaddr)))
				sprintf(out, "*%s", l);
			else if (!fs_at(targ) && offref(targ, wo, o7))
				sprintf(out, "*%s", o7);
			else if ((l = orsynth(0, targ, Iaddr)))
				sprintf(out, "*%s", l);
			else
				sprintf(out, "*%o", targ);
		}
		else { /* index-deferred *X(rn): the index word may be a relocated symbol
			* address (`*dvect(r0)' -- a jump through a bss vector), which must
			* reassemble as a symbol so its relocation survives */
			char t[48];
			if ((relat(wo) & 016) && symword(wo, Iaddr, t))
				sprintf(out, "*%s(%s)", t, rn);
			else
				sprintf(out, "*%o(%s)", x & 0177777, rn);
		}
		break;
	}
}

static int
decode(long o, int addr, char *buf)
{
	int instr = w16(o), op, a1, a2;
	Iaddr = addr;
	CFtype = CF_NEXT;
	CFtarg = -1;
	Optarg = -1;
	CFsysargs = 0;
	CFjsrinline = 0;
	SpliceRaw = 0;	    /* control-flow of this instruction */
	long po = o + 2;    /* offset just past the opcode word */
	int adr = addr + 2; /* PC value while reading index words */
	char o1[32], o2[32];
	int b = (instr >> 15) & 1; /* byte bit (for double/single op groups) */

	/* no-operand */
	switch (instr) {
	case 0:
		strcpy(buf, "halt");
		CFtype = CF_STOP;
		return 2;
	case 1:
		strcpy(buf, V6Sys ? "1" : "wait");
		return 2; /* the v1/v2/v6
			   * dialects have NO wait-instruction keyword --
			   * `wait' there is the SYSCALL (7) */
	case 2:
		strcpy(buf, "rti");
		CFtype = CF_STOP;
		return 2;
	case 3:
		strcpy(buf, "bpt");
		return 2;
	case 4:
		strcpy(buf, "iot");
		return 2;
	case 5:
		strcpy(buf, "reset");
		return 2;
	case 6:
		strcpy(buf, "rtt");
		CFtype = CF_STOP;
		return 2;
	}
	if (J11Dec) {
		static const char *cis[64] = {
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 000-017 */
			0, 0, 0, 0, 0, 0, 0, 0,				/* 020-027: l2dr */
			"movc", "movrc", "movtc", 0, 0, 0, 0, 0,	/* 030-037 */
			"locc", "skpc", "scanc", "spanc", "cmpc", "matc", 0, 0,
			"addn", "subn", "cmpn", "cvtnl", "cvtpn", "cvtnp", "ashn", "cvtln",
			0, 0, 0, 0, 0, 0, 0, 0, /* 060-067: l3dr */
			"addp", "subp", "cmpp", "cvtpl", "mulp", "divp", "ashp", "cvtlp"};
		if (instr == 7) {
			strcpy(buf, "mfpt");
			return 2;
		}
		if ((instr & 0177770) == 0000230) {
			sprintf(buf, "spl\t%o", instr & 7);
			return 2;
		}
		if ((instr & 0177700) == 0007000) {
			fmtop(instr & 077, &po, &adr, o1);
			sprintf(buf, "csm\t%s", o1);
			return po - o;
		}
		if ((instr & 0177700) == 0007200) {
			fmtop(instr & 077, &po, &adr, o1);
			sprintf(buf, "tstset\t%s", o1);
			return po - o;
		}
		if ((instr & 0177700) == 0007300) {
			fmtop(instr & 077, &po, &adr, o1);
			sprintf(buf, "wrtlck\t%s", o1);
			return po - o;
		}
		if ((instr & 0177740) == 0075000) {
			static const char *fis[4] = {"fadd", "fsub", "fmul", "fdiv"};
			sprintf(buf, "%s\t%s", fis[(instr >> 3) & 3], regname(instr & 7));
			return 2;
		}
		if ((instr & 0177700) == 0170300) {
			fmtop(instr & 077, &po, &adr, o1);
			sprintf(buf, "stst\t%s", o1);
			return po - o;
		}
		if (instr == 0076600) {
			strcpy(buf, "med");
			return 2;
		}
		if ((instr & 0177700) == 0076700) {
			sprintf(buf, "xfc\t%o", instr & 077);
			return 2;
		}
		if ((instr & 0177600) == 0076000) { /* CIS */
			int lo = instr & 0177;
			if ((lo & 0170) == 0020) {
				sprintf(buf, "l2dr\t%s", regname(lo & 7));
				return 2;
			}
			if ((lo & 0170) == 0060) {
				sprintf(buf, "l3dr\t%s", regname(lo & 7));
				return 2;
			}
			if (cis[lo & 077]) {
				sprintf(buf, "%s%s", cis[lo & 077], (lo & 0100) ? "i" : "");
				return 2;
			}
		}
	}
	if ((instr & 0177770) == 0000200) {
		sprintf(buf, "rts\t%s", regname(instr & 7));
		CFtype = CF_STOP;
		return 2;
	}
	if ((instr & 0177700) == 0000100) {
		fmtop(instr & 077, &po, &adr, o1);
		sprintf(buf, "jmp\t%s", o1);
		CFtype = (Optarg >= 0 ? CF_JUMP : CF_STOP);
		CFtarg = Optarg;
		return po - o;
	}
	if ((instr & 0177700) == 0000300) {
		fmtop(instr & 077, &po, &adr, o1);
		sprintf(buf, "swab\t%s", o1);
		return po - o;
	}
	if ((instr & 0177000) == 0004000) { /* jsr reg,dst  (0004000..0004777) */
		fmtop(instr & 077, &po, &adr, o1);
		sprintf(buf, "jsr\t%s,%s", regname((instr >> 6) & 7), o1);
		CFtype = CF_CALL;
		CFtarg = Optarg;
		/* the V5-era C prologue `jsr r5,rsave' is followed by ONE inline
		 * frame-size word, consumed by rsave itself (`sub (r0)+,sp;
		 * jmp (r0)' -- v5/usr/source/s4/rsave.s); flow resumes after it.
		 * Keyed to the external's name: the convention IS that symbol
		 * (the 2.x csv prologue has no inline word). */
		if (((instr >> 6) & 7) == 5 && po - o == 4) {
			int rel = relat(o + 2);
			if ((rel & 016) == REXT && (rel >> 4) < NSym) {
				char *nm = Sym[rel >> 4].name;
				if (!strcmp(nm, "rsave") || !strcmp(nm, "mrsave"))
					CFjsrinline = 1;
			}
		}
		return po - o;
	}
	if ((instr & 0177400) >= 0000240 && (instr & 0177400) < 0000400 && (instr & 0400) == 0) {
		/* condition-code ops (nop / clc / sec / ...) */
		static char *cc = "cvzn";
		if (instr == 0240) {
			strcpy(buf, "nop");
			return 2;
		}
		{
			int set = (instr & 020) != 0, i;
			char *p = buf;
			p += sprintf(p, "%s", set ? "se" : "cl");
			for (i = 0; i < 4; i++)
				if (instr & (1 << i))
					*p++ = cc[i];
			*p = 0;
			return 2;
		}
	}
	/* branches */
	if (((instr & 0177400) >= 0000400 && (instr & 0177400) <= 0003400) ||
	    ((instr & 0177400) >= 0100000 && (instr & 0177400) <= 0103400)) {
		int idx = ((instr >> 8) & 07) | (((instr >> 15) & 1) << 3);
		int off = (signed char)(instr & 0377), targ = (addr + 2 + 2 * off) & 0xffff;
		/* a branch is intra-segment: its target lives in the SAME segment as
		 * the instruction -- which for code assembled into .data (the pascal
		 * FP interpreter) is N_DATA, where the dispatch labels (class2/class3)
		 * are defined.  Looking only in N_TEXT synthesized a numbered local
		 * and lost the name mention (shifting the label's symtab slot). */
		int bseg = segof(addr) == N_DATA ? N_DATA : N_TEXT;
		if (bseg == N_TEXT && targ == Tsize) {
			/* skip-to-end-of-function: target is the text/data
			 * boundary.  A REAL label there (emitted by the
			 * end-of-text labels() call) is referenced by name, as
			 * the original was -- the mention interns it in place
			 * (doscan's `jbr L10017').  With no label, fall back to
			 * pc-relative (a synthetic .L would land in .data). */
			char *el = labelat(targ, N_TEXT);
			if (el)
				sprintf(buf, "%s\t%s", brmne[idx], el);
			else
				sprintf(buf, "%s\t.+%o", brmne[idx], (targ - addr) & 0xffff);
		}
		/* `as's own expansion of a far conditional `jlt Y' is the inverted
		 * branch over an absolute jump: `bge .+6; jmp *$Y'.  The synthesized
		 * branch never mentioned a symbol -- rendering its fall-through as a
		 * label (das would find one there) interns that label too early
		 * (doscan's L20010).  Emit the original's `.+6' spelling: same word,
		 * no mention.  (-a only; the listing keeps the informative name.) */
		else if (Asm && off == 2 && w16(o + 2) == 0000137)
			sprintf(buf, "%s\t.+6", brmne[idx]);
		else {
			char *l = labelat(targ, bseg);
			if (l)
				sprintf(buf, "%s\t%s", brmne[idx], l);
			/* no symtab label: spell it DOT-RELATIVE.  A branch word
			 * carries no relocation and numbered locals never reach the
			 * symbol table, so the spelling is byte-free -- and unlike a
			 * synthetic local it needs no definition line, no digit, and
			 * no stream-order binding (regtab's walk-reordered pieces,
			 * gamma's mid-instruction FP-constant target). */
			else if (Asm && HasReloc) {
				int d = targ - addr;
				if (d >= 0)
					sprintf(buf, "%s\t.+%o", brmne[idx], d);
				else
					sprintf(buf, "%s\t.-%o", brmne[idx], -d);
			}
			else if ((l = orsynth(0, targ, addr)))
				sprintf(buf, "%s\t%s", brmne[idx], l);
			else
				sprintf(buf, "%s\t%o", brmne[idx], targ);
		}
		CFtype = (idx == 1 ? CF_JUMP : CF_COND);
		CFtarg = targ; /* idx 1 = unconditional br */
		return 2;
	}
	/* sys / emt / trap */
	/* `sys n|0200': bit 0200 = arguments on the STACK (trap.c `if (i & 0200)'),
	 * so NO inline argument words follow -- libjobs' sigsys pushes and cleans
	 * up its own args; consuming sysinline[] words there swallowed real code. */
	if ((instr & 0177400) == 0104400) {
		int sn = instr & 0377;
		char *snm = 0;
		if (Sys211) {
			if (sn < 157)
				snm = sysnm211[sn];
		}
		else if (V2Sys) {
			if (sn < 35)
				snm = sysnm1[sn];
		}
		else if (V6Sys) {
			if (sn < 64)
				snm = sysnm6[sn];
		}
		else if (sn < 64)
			snm = sysnm29[sn];
		sprintf(buf, snm ? "sys\t%o / %s" : "sys\t%o", sn, snm);
		CFsysargs = Sys211	     ? 0
			    : (instr & 0200) ? 0
			    : V2Sys	     ? ((instr & 077) < 35 ? v1inline[instr & 077] : 0)
			    : V6Sys	     ? v6inline[instr & 077]
					     : sysinline[instr & 077];
		/* `sys exit' never returns -- code that falls through into message
		 * STRINGS right after it (V6 fxe.s's `sys exit / mes1: <Temp...>')
		 * must not drag the flow walk into them.  A wrong personality is
		 * byte-harmless: the unreached region is emitted as data. */
		if (instr == 0104401)
			CFtype = CF_STOP;
		return 2;
	}
	if ((instr & 0177400) == 0104000) {
		sprintf(buf, "emt\t%o", instr & 0377);
		return 2;
	}

	op = (instr >> 12) & 017;

	/* FP11 (017xxxx): just the common arithmetic/move forms + no-operand */
	if (op == 017) {
		switch (instr) {
		case 0170000:
			strcpy(buf, "cfcc");
			return 2;
		case 0170001:
			strcpy(buf, "setf");
			return 2;
		case 0170011:
			strcpy(buf, "setd");
			return 2;
		case 0170002:
			strcpy(buf, "seti");
			return 2;
		case 0170012:
			strcpy(buf, "setl");
			return 2;
		}
		{
			int g = (instr >> 8) & 017, ac = (instr >> 6) & 3;
			char *m;
			/* indexed by bits 11-8; the store forms (idx 8,10,11,12) take
			 * `freg,dst', the load/arith forms take `src,freg'. */
			static char *fp2[16] = {0, 0, "mulf", "modf", "addf", "movf", "subf", "cmpf",
						"movf", "divf", "movei", "movfi", "movfo", "movie", "movif", "movof"};
			if ((instr & 0177700) >= 0170400 && (instr & 0177700) <= 0170700) {
				static char *fp1[4] = {"clrf", "tstf", "absf", "negf"};
				fmtop(instr & 077, &po, &adr, o1);
				sprintf(buf, "%s\t%s", fp1[((instr >> 6) & 3)], o1);
				return po - o;
			}
			m = fp2[g];
			/* the 1972 dialect (v1/v2/v3) lacks movei/movie (V4
			 * additions, with ldfps/stfps which das never emits):
			 * raw word, so the output re-assembles under the same
			 * era's `as --std' */
			if (V2Sys && (g == 10 || g == 13))
				m = 0;
			/* ldf (172400) with a GENERAL-REGISTER source spec (< 4): as picks
			 * ldf-vs-stf for `movf' by that very spec, so `movf r0,fr0' always
			 * re-encodes as STF -- even the genuine as26 opl12 cannot be told
			 * to produce this word.  Splice the raw word (vax.md: the VAX float
			 * short-literal lesson).  -a only; the listing keeps the mnemonic. */
			if (Asm && g == 5 && (instr & 077) < 4) {
				sprintf(buf, "%o", instr);
				return 2;
			}
			if (m) {
				fmtop(instr & 077, &po, &adr, o1);
				if (g == 8 || g == 10 || g == 11 || g == 12)
					sprintf(buf, "%s\tfr%d,%s", m, ac, o1);
				else
					sprintf(buf, "%s\t%s,fr%d", m, o1, ac);
				return po - o;
			}
		}
		sprintf(buf, "%o", instr);
		return 2;
	}
	/* double-operand: 1..6 word, 11..16 byte (16/116 == sub) */
	if ((op >= 1 && op <= 6) || (op >= 011 && op <= 016)) {
		int bop = op & 07;
		char *m = (op == 016) ? "sub" : dopmne[bop];
		char mb[6];
		strcpy(mb, m);
		if (b && op != 016)
			strcat(mb, "b");
		fmtop((instr >> 6) & 077, &po, &adr, o1);
		fmtop(instr & 077, &po, &adr, o2);
		sprintf(buf, "%s\t%s,%s", mb, o1, o2);
		return po - o;
	}
	/* EIS / jsr / sob / xor (op 07 and op 004) */
	if (op == 07) {
		int reg = (instr >> 6) & 7;
		switch (instr & 0177000) {
		case 0070000:
			fmtop(instr & 077, &po, &adr, o1);
			sprintf(buf, "mul\t%s,%s", o1, regname(reg));
			return po - o;
		case 0071000:
			fmtop(instr & 077, &po, &adr, o1);
			sprintf(buf, "div\t%s,%s", o1, regname(reg));
			return po - o;
		case 0072000:
			fmtop(instr & 077, &po, &adr, o1);
			sprintf(buf, "ash\t%s,%s", o1, regname(reg));
			return po - o;
		case 0073000:
			fmtop(instr & 077, &po, &adr, o1);
			sprintf(buf, "ashc\t%s,%s", o1, regname(reg));
			return po - o;
		case 0074000:
			fmtop(instr & 077, &po, &adr, o1);
			sprintf(buf, "xor\t%s,%s", regname(reg), o1);
			return po - o;
		case 0077000: {
			int off = instr & 077, targ = (addr + 2 - 2 * off) & 0xffff;
			int sseg = segof(addr) == N_DATA ? N_DATA : N_TEXT; /* intra-segment, like branches */
			char *l = labelat(targ, sseg);
			if (l)
				sprintf(buf, "sob\t%s,%s", regname(reg), l);
			else if (Asm && HasReloc) { /* dot-relative, like branches */
				int d = targ - addr;
				if (d >= 0)
					sprintf(buf, "sob\t%s,.+%o", regname(reg), d);
				else
					sprintf(buf, "sob\t%s,.-%o", regname(reg), -d);
			}
			else if ((l = orsynth(0, targ, addr)))
				sprintf(buf, "sob\t%s,%s", regname(reg), l);
			else
				sprintf(buf, "sob\t%s,%o", regname(reg), targ);
			CFtype = CF_COND;
			CFtarg = targ;
			return 2;
		}
		}
	}
	/* single-operand group 0005000..0006700 (+ byte 0105000..) */
	if ((instr & 0077000) == 0005000 || (instr & 0077000) == 0006000) {
		int g = ((instr >> 6) & 077) - 050; /* opcode is bits 6-11 */
		char mb[6];
		/* for clr..sxt (g 0..11) bit 15 is the byte variant; for mark/mfpi/
		 * mtpi/sxt (g 12..15) bit 15 selects mtps/mfpd/mtpd/mfps instead */
		if (g >= 014 && b)
			strcpy(mb, sopmne_d[g - 014]);
		else {
			strcpy(mb, sopmne[g]);
			if (b && g < 014)
				strcat(mb, "b");
		}
		/* `mark n' encodes its argument in the LOW SIX BITS like `sys n'
		 * (as29.s classes it 011, sys-type) -- it has no addressing mode.
		 * Rendering it as an operand (`mark (r4)') garbles reassembly. */
		if (g == 014 && !b) {
			sprintf(buf, "mark\t%o", instr & 077);
			return 2;
		}
		fmtop(instr & 077, &po, &adr, o1);
		/* mfpi/mtpi and the bit-15 mtps/mfpd/mtpd/mfps have NO mnemonic in
		 * `as' (mark and sxt DO -- as29.s; the kernel's mch.s synthesizes the
		 * missing ones itself, `mfpd = 106500^tst', but that ^-typing is not
		 * recoverable from the .o).  In -a mode splice the raw words: bare
		 * expression lines assemble verbatim, and a relocated operand word
		 * stays symbolic via symword.  EXCEPT a PC-RELATIVE operand -- a
		 * bare word cannot carry the pcrel reloc bit (System III's mch_i.o
		 * does `mtpi textlabel'; the I/D-space helpers are BUILT on these):
		 * emit the true mnemonic and let the tab211 banner marker route the
		 * harness to `as --isa=bsd211'.  Under -J the mnemonics exist
		 * (`as -j'), so emit them directly always. */
		if (Asm && !J11Dec && ((b && g >= 014) || (!b && (g == 015 || g == 016)))) {
			char *p = buf;
			long w;
			int pcr = 0;
			for (w = o + 2; w < po; w += 2)
				if (relat(w) & 1)
					pcr = 1;
			if (!pcr) {
				p += sprintf(p, "%o", instr);
				for (w = o + 2; w < po; w += 2) {
					char sw[64];
					if (relat(w) && symword(w, adr, sw))
						p += sprintf(p, "\n\t%s", sw);
					else
						p += sprintf(p, "\n\t%o", w16(w));
				}
				return po - o;
			}
		}
		sprintf(buf, "%s\t%s", mb, o1);
		return po - o;
	}
	(void)a1;
	(void)a2;
	sprintf(buf, "%o", instr);
	return 2;
}

/* ---- listing generation ------------------------------------------------- */

static void
readsyms(long symoff, int symsize)
{
	int n, i, j;
	free(Sym);
	/* 2.11's `Newsym' object format: 8-byte entries -- off_t strx (PDP-11
	 * long, HIGH word first), type word, value word -- with a string table
	 * (its first long = total length, offsets from 4) after the symtab.
	 * Detect by GEOMETRY: the old fixed-name format fills the file exactly;
	 * the new one leaves exactly the string table. */
	NewFmt = 0;
	if (symsize > 0 && symsize % 8 == 0 && symoff + symsize + 4 <= FLEN && symoff + symsize != FLEN) {
		long st = symoff + symsize;
		long tl = ((long)w16(st) << 16) | w16(st + 2);
		if (tl >= 4 && st + tl == FLEN)
			NewFmt = 1;
	}
	if (NewFmt) {
		long st = symoff + symsize;
		n = symsize / 8;
		Sym = malloc((n + 1) * sizeof(struct sym));
		NSym = 0;
		for (i = 0; i < n; i++) {
			long r = symoff + i * 8;
			long sx = ((long)w16(r) << 16) | w16(r + 2);
			if (r + 8 > FLEN)
				break;
			for (j = 0; j < 39 && st + sx + j < FLEN && F[st + sx + j]; j++)
				Sym[NSym].name[j] = F[st + sx + j];
			Sym[NSym].name[j] = 0;
			Sym[NSym].type = F[r + 4];
			Sym[NSym].value = w16(r + 6);
			NSym++;
		}
		for (FirstDef = 0; FirstDef < NSym; FirstDef++)
			if (!(ISEXT(Sym[FirstDef].type) && BASETYPE(Sym[FirstDef].type) == N_UNDF))
				break;
		return;
	}
	n = symsize / 12;
	Sym = malloc((n + 1) * sizeof(struct sym));
	NSym = 0;
	for (i = 0; i < n; i++) {
		long r = symoff + i * 12;
		if (r + 12 > FLEN)
			break;
		for (j = 0; j < 8; j++)
			Sym[NSym].name[j] = F[r + j];
		Sym[NSym].name[8] = 0;
		for (j = 7; j >= 0 && (Sym[NSym].name[j] == ' ' || Sym[NSym].name[j] == 0); j--)
			Sym[NSym].name[j] = 0;
		/* a name with bytes outside as's identifier charset cannot be
		 * spelled in assembly (the 1972 unix.out kernel carries such
		 * entries): blank it -- das skips nameless symbols everywhere */
		for (j = 0; Sym[NSym].name[j]; j++) {
			unsigned char c = Sym[NSym].name[j];
			if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '~')) {
				Sym[NSym].name[0] = 0;
				break;
			}
		}
		Sym[NSym].type = F[r + 8];
		Sym[NSym].value = w16(r + 10);
		/* an entry as cannot PRODUCE -- type bits outside EXT|BASE, or
		 * a non-ext UNDEF carrying a value (the lisp l1100.out's alien
		 * symtab, written by a non-Unix assembler) -- is blanked like
		 * a garbage name; the harness reinserts it from the original */
		if ((w16(r + 8) & ~077) || (w16(r + 8) == 0 && Sym[NSym].value))
			Sym[NSym].name[0] = 0;
		NSym++;
	}
	/* DUPLICATE stored names: as hashes the FULL spelling but stores only 8
	 * chars (as14.s rname folds every char into the hash, stops the store at
	 * 8), so `extern char *IEH3outp, *IEH3outlim' yields two `_IEH3out'
	 * slots (V6 libp revput.o).  Recreate by giving later duplicates a 9th
	 * character -- truncated away on store, but it puts the spelling in its
	 * own hash chain.  Only full-8-char names qualify (a shorter duplicate
	 * cannot be a truncation artifact: it comes from the unhashed `~'
	 * namespace, whose emission forms mint distinct entries by themselves,
	 * so those -- N_ABS/N_REG locals -- are excluded here too). */
	for (i = 0; NewFmt == 0 && i < NSym; i++) {
		int t = Sym[i].type, nd = 0;
		if (strlen(Sym[i].name) != 8)
			continue;
		if (!ISEXT(t) && (BASETYPE(t) == N_ABS || BASETYPE(t) == N_REG))
			continue;
		for (j = 0; j < i; j++)
			if (strlen(Sym[j].name) >= 8 && !memcmp(Sym[j].name, Sym[i].name, 8))
				nd++;
		if (nd) {
			Sym[i].name[8] = nd < 10 ? '0' + nd : 'a' + nd - 10;
			Sym[i].name[9] = 0;
		}
	}
	/* first symbol that is not an undefined external: the `.comm' commons the
	 * compiler emits at the top of the file all precede it (see is_common) */
	for (FirstDef = 0; FirstDef < NSym; FirstDef++)
		if (!(ISEXT(Sym[FirstDef].type) && BASETYPE(Sym[FirstDef].type) == N_UNDF))
			break;
}

/* mark which symbols any relocation entry cites (by index) -- an undefined
 * external cited by NO relocation was declared `.globl name' but never used
 * (the compiler's `fltused' FP-startup marker), and must be emitted as a
 * declaration since no code reference will intern it. */
static void
build_referenced(void)
{
	long wo;
	int r, i;
	free(Referenced);
	Referenced = NSym > 0 ? calloc(NSym, 1) : 0;
	free(SymEmitted);
	SymEmitted = NSym > 0 ? calloc(NSym, 1) : 0;
	free(ForceGlobl);
	ForceGlobl = NSym > 0 ? calloc(NSym, 1) : 0;
	free(RefRank);
	RefRank = NSym > 0 ? malloc(NSym * sizeof(int)) : 0;
	free(Tmo);
	Tmo = NSym > 0 ? malloc(NSym * sizeof(int)) : 0;
	free(WeaveEarly);
	WeaveEarly = NSym > 0 ? calloc(NSym, 1) : 0;
	NRefSeen = 0;
	NTmo = 0;
	OvObj = 0;
	if (RefRank)
		for (i = 0; i < NSym; i++)
			RefRank[i] = -1;
	if (Tmo)
		for (i = 0; i < NSym; i++)
			Tmo[i] = -1;
	/* a named LOCAL-undefined symbol (type exactly 0) marks V7-as output
	 * even on a no-reloc image (the V7 kernel's `.if'-mentioned config
	 * names) -- detect BEFORE the reloc-scan early-out */
	V7Obj = 0;
	for (i = 0; i < NSym; i++)
		if (Sym[i].name[0] && Sym[i].type == N_UNDF) {
			V7Obj = 1;
			break;
		}
	if (!Referenced || !HasReloc)
		return;
	for (wo = Tbase; wo + 1 < Tbase + Tsize; wo += 2) {
		r = relat(wo);
		if ((r & 016) == REXT && (r >> 4) < NSym)
			Referenced[r >> 4] = 1;
	}
	for (wo = Dbase; wo + 1 < Dbase + Dsize; wo += 2) {
		r = relat(wo);
		if ((r & 016) == REXT && (r >> 4) < NSym)
			Referenced[r >> 4] = 1;
	}
	/* An external relocation citing a DEFINED symbol is the telltale of the
	 * overlay assembler mode (`as -V', MENLO_OVLY ovas): only there do refs to
	 * defined globals stay external, so the overlay ld can thunk them.  Such
	 * an object must reassemble under `as -V' -- where undefined externals are
	 * NOT auto-externalized, so every referenced one needs its `.globl' back
	 * (see needs_decl).  The banner announces the mode for the harness. */
	for (i = 0; i < NSym; i++)
		if (Referenced[i] && BASETYPE(Sym[i].type) != N_UNDF) {
			OvObj = 1;
			break;
		}
	if (OvObj)
		V7Obj = 0;
}

/* Using the scan pass's text-mention order (Tmo), decide which referenced
 * undefined externals were DECLARED (`.globl name') ahead of the code instead of
 * interned at their first reference.  X was, if ANY symbol Y sits LATER in the
 * symbol table (higher index) yet is text-mentioned EARLIER than X: interning X
 * only at its own reference would then order it after Y, inverting the table.
 * An explicit `.globl X' at X's index restores it.  Y ranges over every symbol,
 * not just externals -- e.g. signal's `cerror' (an early error-path `.globl',
 * index 97) precedes the trailing NSIG/dvect/tvect declarations (98-100) whose
 * text mentions come first; execv's `.globl cerror,_environ' is the same with Y
 * another external. */
static void
compute_forceglobl(void)
{
	int x, y, rankx;
	if (!ForceGlobl || !Tmo)
		return;
	for (x = 0; x < NSym; x++) {
		if (!is_undef_ext(x) || !Sym[x].name[0] || is_common(x))
			continue;
		/* X's mention rank: its text-scan rank, or -- if referenced only in DATA
		 * (e.g. sbrk's `_end', a bss-boundary pointer) -- notionally after all
		 * text mentions, since das emits data last.  A never-mentioned external
		 * is not a candidate. */
		if (Tmo[x] >= 0)
			rankx = Tmo[x];
		else if (Referenced && Referenced[x])
			rankx = NTmo;
		else
			continue;
		for (y = 0; y < NSym; y++)
			if (y > x && Tmo[y] >= 0 && Tmo[y] < rankx) {
				ForceGlobl[x] = 1;
				break;
			}
	}
}

/* Decide which defined data/bss symbols are DEFINED-FIRST (interned at their
 * .bss/.data definition, so their symtab index precedes any code reference) and
 * must be woven in index order, vs REFERENCE-FIRST (interned at a symbolized
 * code reference, which already fixes their index -- their definition then just
 * follows in the trailing address-order pass).  X is defined-first iff, in the
 * text scan's first-mention order, a HIGHER-index symbol is mentioned before X
 * (X's low index cannot have come from its own late text mention), or X is not
 * text-mentioned at all yet some higher-index symbol is. */
static void
compute_weave_early(void)
{
	int i, r, pfx = -1, omax = -1, *r2s;
	if (!WeaveEarly || !Tmo || NTmo <= 0)
		return;
	for (i = 0; i < NSym; i++)
		if (Tmo[i] >= 0 && i > omax)
			omax = i;
	r2s = malloc(NTmo * sizeof(int));
	if (!r2s)
		return;
	for (i = 0; i < NTmo; i++)
		r2s[i] = -1;
	for (i = 0; i < NSym; i++)
		if (Tmo[i] >= 0 && Tmo[i] < NTmo)
			r2s[Tmo[i]] = i;
	for (r = 0; r < NTmo; r++) { /* walk in first-mention order */
		int s = r2s[r];
		if (s < 0)
			continue;
		if (is_seg_defined(s) && pfx > s)
			WeaveEarly[s] = 1; /* higher index seen earlier */
		if (s > pfx)
			pfx = s;
	}
	for (i = 0; i < NSym; i++) /* never text-mentioned: weave if low index */
		if (is_seg_defined(i) && Tmo[i] < 0 && omax > i)
			WeaveEarly[i] = 1;
	free(r2s);
}

/* In -a mode a leading `~' is as's namespace marker, consumed on assembly (so
 * source `~i=r4' defines the symtab name `i').  A symtab name that ITSELF begins
 * with `~' (e.g. the compiler's `~main' body-entry alias) must therefore be
 * emitted with the tilde doubled -- source `~~main:' -> symtab `~main'. */
static char *
asmname(char *n)
{
	static char b[48]; /* 2.11 string-table names run to 32 chars */
	if (Asm && n[0] == '~') {
		b[0] = '~';
		strncpy(b + 1, n, sizeof b - 2);
		b[sizeof b - 1] = 0;
		return b;
	}
	return n;
}

/* ---- interleaved symbol declarations -----------------------------------
 * A symbol's symtab INDEX is the position of its first mention in the source
 * as `as' processed it; the relocation entries cite symbols by that index, so
 * -a output must reproduce the exact first-mention ORDER.  Labels and undefined
 * externals are mentioned inline by the disassembler in address order (= index
 * order for compiler output).  The DECLARATION-only mentions -- `.globl name'
 * for a defined external, `name = value' for an absolute, `~name=rN' for a
 * register variable -- have no code position, so they are flushed here, in
 * index order, at the gaps between the inline mentions: flush_decls(i) emits
 * every pending declaration whose index is < i just before symbol i is
 * mentioned.  (The old code front-loaded all declarations, which put e.g. a
 * syscall stub's `.globl _read' before sys.s's absolute symbols and inverted
 * the whole table.)  NextDecl is the flush frontier; it is reset before the
 * real emission pass (the /dev/null scan pass advances it harmlessly). */
static int NextDecl;

static int
is_undef_ext(int i)
{
	return ISEXT(Sym[i].type) && BASETYPE(Sym[i].type) == N_UNDF;
}

static int
is_common(int i) /* undefined external declared `.comm name,size' */
{
	if (!(is_undef_ext(i) && Sym[i].name[0]))
		return 0;
	/* a common carries its size as the symbol value; a plain referenced extern
	 * is value 0.  A size-0 common (`.comm x,0', e.g. an [] array) is
	 * distinguishable only by position -- it is declared at the top, ahead of
	 * the first defined symbol. */
	return Sym[i].value != 0 || i < FirstDef;
}

/* Exemplar mnemonic for an `as' INSTRUCTION-typed symbol: hand-written asm can
 * synthesize missing mnemonics by assignment (mch.s's `mfpd = 106500^tst') --
 * the `^' takes the value of the left and the TYPE of the right, and that
 * instruction type is written to the symbol table (single-op = 015 etc.).
 * Reproduce the entry with `name = value^<exemplar>' of the same class. */
static char *
instype_ex(int t)
{
	switch (t) {
	case 005:
		return "movfi"; /* flop freg,dst (V6 exp.s `stexp=175000^movfi') */
	case 006:
		return "br"; /* branch */
	case 007:
		return "jsr"; /* jsr/xor */
	case 010:
		return "rts";
	case 011:
		return "sys";
	case 012:
		return "movf"; /* movf ld/st */
	case 013:
		return "mov"; /* double-operand */
	case 014:
		return "movif"; /* flop fsrc,freg (V6 exp.s `ldexp=176400^movif') */
	case 015:
		return "tst"; /* single-operand */
	case 030:
		return "ash"; /* EIS op src,reg */
	case 031:
		return "sob";
	}
	return 0;
}

static int
needs_decl(int i)
{
	int t = Sym[i].type;
	if (!Sym[i].name[0])
		return 0;
	if (is_undef_ext(i)) {
		if (is_common(i))
			return 1; /* `.comm' common */
		if (!(Referenced && Referenced[i]))
			return 1; /* `.globl' -- never referenced */
		if (OvObj || V7Obj)
			return 1;		    /* neither ovas (`as -V') nor V7 as
						     * (`as -7') auto-externalizes: every
						     * referenced undefined external needs its
						     * `.globl' back, at its symtab slot */
		return ForceGlobl && ForceGlobl[i]; /* `.globl' -- pin out-of-order symtab */
	} /* else the reference interns it */
	if (ISEXT(t))
		return BASETYPE(t) != N_FN; /* defined external -> .globl */
	if (BASETYPE(t) == N_UNDF)
		return 1; /* LOCAL-undefined (type 0): an ovas
			   * (-V) reference that was never .globl'd
			   * (m40.s's _piget) -- recreate the bare
			   * entry with a self-assignment */
	if (seg_mismatch(i))
		return 1;				    /* -y: `name = value ^ donor' */
	return BASETYPE(t) == N_ABS || BASETYPE(t) == N_REG /* local abs / reg var */
	       || instype_ex(BASETYPE(t)) != 0;		    /* `name = op^tst' opcode symbol */
}

/* An absolute local is emitted in the `~' namespace (`~name=value'), exactly as
 * the compiler declares its stack-variable offsets and register spills.  The `~'
 * is consumed on assembly, so the symbol-table entry is IDENTICAL to a plain
 * `name = value'; but keeping it in the `~' namespace means it does NOT shadow a
 * same-named OPCODE (a C variable `neg' -> `~neg=...', so the `neg' instruction
 * still assembles) NOR a same-named label, and each `~name=' mints a fresh entry
 * so a parameter offset redeclared per function keeps all its copies.  (The code
 * references these by raw offset, never by name, so the `~' form never loses a
 * reference.) */
/* position-independent definition of a text/data/bss symbol as a type-cast
 * assignment: `name = segrel-value ^ donor' (as re-adds the segment base at
 * write-out, and the `^' carries the donor's type).  A LOCAL goes through
 * the `~' marker: nothing references it by name (no relocation exists to),
 * and the hashed name may belong to a same-named EXT (pi's `_header'
 * function vs its `_header' bss cell). */
static void
emit_cast(int i, FILE *out)
{
	int t = Sym[i].type;
	int dn = type_donor(BASETYPE(t), i);
	if (LabelOut)
		LabelOut[i] = 1; /* a cast DEFINES: sweep-up skips it */
	int bt = BASETYPE(t), st = bt == N_TEXT ? 0 : bt == N_DATA ? Tsize
								   : Tsize + Dsize;
	char *pre = ISEXT(t) ? "" : "~";
	if (ISEXT(t))
		fprintf(out, ".globl\t%s\n", asmname(Sym[i].name));
	if (dn >= 0)
		fprintf(out, "%s%s = %o ^ %s\n", pre, ISEXT(t) ? asmname(Sym[i].name) : Sym[i].name,
			(Sym[i].value - st) & 0177777, asmname(Sym[dn].name));
	else {
		/* no clean donor of this type exists (ex: text+data alone
		 * exceed 64K, so EVERY bss symbol is a wrapped cast): take
		 * the type from `.' inside the segment, then restore */
		static const char *sd[] = {0, 0, ".text", ".data", ".bss"};
		fprintf(out, "%s\n%s%s = %o ^ .\n", sd[bt],
			pre, ISEXT(t) ? asmname(Sym[i].name) : Sym[i].name,
			(Sym[i].value - st) & 0177777);
		if (CurSeg >= N_TEXT && CurSeg <= N_BSS && CurSeg != bt)
			fprintf(out, "%s\n", sd[CurSeg]);
	}
}

static void
emit_decl(int i, FILE *out)
{
	int t = Sym[i].type;
	char *ex;
	if (seg_mismatch(i)) {
		emit_cast(i, out);
		return;
	}
	if (is_common(i))
		fprintf(out, ".comm\t%s,%o\n", asmname(Sym[i].name), Sym[i].value & 0177777);
	else if (is_undef_ext(i) && SymNoReloc && !HasReloc && as_keyword(Sym[i].name))
		/* keyword-named undefined external: `.globl ~name' mints the
		 * entry UNHASHED so the instruction stays assemblable (m11's
		 * div/mul); no-reloc only -- nothing cites it by index */
		fprintf(out, ".globl\t~%s\n", Sym[i].name);
	else if (ISEXT(t) && BASETYPE(t) == N_ABS) /* external absolute: .globl + assignment */
		fprintf(out, ".globl\t%s\n%s = %o\n", asmname(Sym[i].name), asmname(Sym[i].name), Sym[i].value & 0177777);
	else if (ISEXT(t))
		fprintf(out, ".globl\t%s\n", asmname(Sym[i].name));
	else if (BASETYPE(t) == N_REG)
		fprintf(out, "~%s=r%o\n", Sym[i].name, Sym[i].value & 07);
	else if ((ex = instype_ex(BASETYPE(t))) != 0) { /* opcode symbol (mch.s `mfpd=106500^tst') */
		/* a linked image carries one copy PER INPUT OBJECT of such a
		 * local; the first keeps the hashed name (instruction uses
		 * resolve to it), later copies mint fresh unhashed entries */
		int j, dup = 0;
		for (j = 0; j < i; j++)
			if (!ISEXT(Sym[j].type) && Sym[j].name[0] && instype_ex(BASETYPE(Sym[j].type)) && !strcmp(Sym[j].name, Sym[i].name)) {
				dup = 1;
				break;
			}
		fprintf(out, "%s%s = %o^%s\n", dup ? "~" : "", Sym[i].name, Sym[i].value & 0177777, ex);
	}
	else if (BASETYPE(t) == N_UNDF) /* local-undefined: a bare reference
					 * interns it (type 0000) without
					 * defining -- `.if x' evaluates the
					 * name and emits nothing.  A KEYWORD
					 * name would read the opcode, not
					 * mint a symbol: go through `~' */
		fprintf(out, ".if %s%s\n.endif\n",
			as_keyword(Sym[i].name) ? "~" : "", asmname(Sym[i].name));
	else /* N_ABS */
		fprintf(out, "~%s=%o\n", Sym[i].name, Sym[i].value & 0177777);
}

/* a symbol DEFINED in data or bss (a label with an address there, local or
 * external) -- emitted, in -a mode, as a woven fragment (see emit_fragment) at
 * its symtab-index position, not in a trailing per-segment pass */
static int
is_seg_defined(int i)
{
	int b = BASETYPE(Sym[i].type);
	return Sym[i].name[0] && (b == N_DATA || b == N_BSS);
}

/* emit a `.text'/`.data'/`.bss' directive only when the segment actually changes */
static void
seg_switch(int seg, FILE *out)
{
	static const char *nm[] = {0, 0, ".text", ".data", ".bss"};
	if (seg == CurSeg || seg < N_TEXT || seg > N_BSS)
		return;
	fprintf(out, "%s.%s\n", CurSeg < 0 ? "" : "\n", nm[seg] + 1);
	CurSeg = seg;
}

/* emit pending declarations with symtab index < limit, in index order; in the
 * -a real pass a defined data/bss symbol triggers its woven fragment instead */
static void
flush_decls(int limit, FILE *out)
{
	while (NextDecl < limit) {
		int i = NextDecl;
		if (AuxFrozen && is_seg_defined(i)) {
			/* weave only DEFINED-FIRST symbols in index order; a reference-first
			 * data/bss symbol is interned by its code reference and defined in
			 * the trailing address-order pass, so skip it here.  On a NO-RELOC
			 * image the symtab order is link metadata (ldnr restores it), so
			 * nothing needs weaving -- the address-order walk defines all. */
			if (WeaveEarly && WeaveEarly[i] && !(SymNoReloc && !HasReloc))
				emit_fragment(i, out); /* advances NextDecl */
			else
				NextDecl++;
			continue;
		}
		if (needs_decl(i))
			emit_decl(i, out);
		NextDecl++;
	}
}

/* emit every label defined at `addr' in segment `seg' (flushing, in -a mode,
 * any declarations the original interned before each such symbol) */
static void labels2(int addr, int seg, FILE *out, int hibyte);

static void
labels(int addr, int seg, FILE *out)
{
	int i;
	for (i = 0; i < NSym; i++)
		if (Sym[i].value == addr && BASETYPE(Sym[i].type) == seg && Sym[i].name[0]) {
			text_mention(i); /* a label defined in the text scan (gated) */
			/* body building: only the label line -- the walk does the rest.
			 * A LATER-index alias of an earlier local label gets NO label
			 * line at all: the walk emits it as `~name=canonical' at its
			 * own symtab position (the compiler's goto-label idiom,
			 * `~retoolon=L4'), which yields the identical table entry but
			 * interns at the right index instead of at the shared address. */
			if (Asm && DupLocal && DupLocal[i])
				continue; /* tilde-emitted by the walk */
			if (Asm && AssignPin && AssignPin[i])
				continue; /* assignment-pinned by the walk */
			if (Asm && seg_mismatch(i))
				continue; /* -y: cast-assigned by decl */
			if (Buffering) {
				if (!(Asm && alias_canonical(i))) {
					fprintf(out, "%s:\n", asmname(Sym[i].name));
					if (LabelBuilt)
						LabelBuilt[i] = 1;
				}
				continue;
			}
			if (Asm && SymEmitted && SymEmitted[i])
				continue; /* already defined (woven early) */
			/* Outside a woven fragment (text labels), a defined external is
			 * interned by its own `.globl' (index i), which must precede the
			 * label definition -> flush through i, then switch to this segment.
			 * Inside a fragment the flush would re-enter, so the fragment's own
			 * externals get their `.globl' emitted inline here instead. */
			if (Asm && !InFragment) {
				flush_decls(ISEXT(Sym[i].type) ? i + 1 : i, out);
				seg_switch(seg, out);
			}
			else if (Asm && ISEXT(Sym[i].type))
				fprintf(out, ".globl\t%s\n", asmname(Sym[i].name));
			fprintf(out, "%s:\n", asmname(Sym[i].name));
			if (LabelOut)
				LabelOut[i] = 1;
			if (SymEmitted)
				SymEmitted[i] = 1;
		}
	if (in_aux(addr) && segof(addr) == seg) { /* numbered local (or .L<addr> fallback);
						   * a spot merged into a lower base has no
						   * definition of its own */
		char *sd = synthdef(addr);
		if (sd)
			fprintf(out, "%s:\n", sd);
	}
}

/* labels() variant for the odd-address byte split: prints all labels for
 * `addr', with the final line carrying the high-byte statement so the pair
 * is atomic under any piece cutting. */
static void
labels2(int addr, int seg, FILE *out, int hibyte)
{
	char lb[4096];
	size_t n = 0;
	int i;
	lb[0] = 0;
	for (i = 0; i < NSym; i++)
		if (Sym[i].value == addr && BASETYPE(Sym[i].type) == seg && Sym[i].name[0]) {
			if (Asm && DupLocal && DupLocal[i])
				continue;
			if (Asm && AssignPin && AssignPin[i])
				continue;
			if (Asm && seg_mismatch(i))
				continue; /* -y: cast-assigned by decl
					   * (keyword-named `halt:' at an ODD address --
					   * System III stproto -- must not ALSO label) */
			if (Asm && alias_canonical(i))
				continue;
			text_mention(i);
			if (LabelBuilt)
				LabelBuilt[i] = 1;
			n += snprintf(lb + n, sizeof lb - n, "%s:", asmname(Sym[i].name));
			if (n >= sizeof lb - 64)
				break;
		}
	if (in_aux(addr) && segof(addr) == seg) {
		char *sd = synthdef(addr);
		if (sd)
			n += snprintf(lb + n, sizeof lb - n, "%s:", sd);
	}
	fprintf(out, "%s\t.byte %o\n", lb, hibyte);
}

/* Could [g0,g1) be a run of instructions?  A pc-relative relocation only ever
 * sits on an instruction OPERAND (data is never pc-relative), so a clean decode
 * must consume every pcrel word as an operand -- never land an instruction
 * boundary on one -- and must tile the gap exactly.  This recovers code the
 * control-flow walk cannot reach (e.g. interrupt handlers entered only through
 * hardware vectors), and rejects data: a misaligned/data decode trips the test.
 * (A false positive is still harmless: a self-consistent decode reassembles to
 * the identical bytes.) */
static int
gap_is_code(long tbase, int g0, int g1)
{
	int pc, sawpc = 0, ok = 1;
	char buf[120];
	unsigned char *start = calloc((g1 - g0) / 2 + 1, 1); /* start[(a-g0)/2]: 1=instr */
	if (!start)
		return 0;
	/* pass 1: decode, recording instruction starts; reject obvious misalignment */
	for (pc = g0; pc < g1;) {
		int rel = relat(tbase + pc), len, k;
		if (rel & 016) { /* relocated word at a boundary */
			if (rel & 1) {
				ok = 0;
				break;
			} /* a pcrel operand as an opcode => misaligned */
			pc += 2;
			continue; /* absolute inline data (sys arg / pointer) */
		}
		len = decode(tbase + pc, pc, buf);
		if (len < 2)
			len = 2;
		if (pc + len > g1) {
			ok = 0;
			break;
		} /* straddles the gap end => misaligned */
		start[(pc - g0) / 2] = 1;
		for (k = 2; k < len; k += 2)
			if (relat(tbase + pc + k) & 1)
				sawpc = 1; /* pcrel operand */
		pc += len;
	}
	/* The pcrel-operand signal (sawpc) is how we tell code from data when the
	 * file carries relocation info.  A STRIPPED binary has none (relat() always
	 * returns 0, so sawpc can never set) -- there the only way to recover code
	 * unreachable by the control-flow walk (e.g. switch case bodies after a
	 * computed `jmp *tbl(r)', which we cannot follow) is the structural test
	 * alone: an exact instruction tiling of the gap whose branches all land on
	 * real boundaries (pass 2).  A false positive stays byte-harmless -- a
	 * self-consistent decode reassembles to the identical bytes. */
	if (!ok || pc != g1 || (HasReloc && !sawpc)) {
		if (getenv("DASDBG"))
			fprintf(stderr, "gap [%06o,%06o) reject1 ok=%d pc=%06o sawpc=%d\n", g0, g1, ok, pc, sawpc);
		free(start);
		return 0;
	}
	/* pass 2: every branch/call/jump must reach a real instruction boundary --
	 * inside the gap a recorded start, outside it already-marked code (Mark==1).
	 * Data decoded as code (e.g. inline char args mixed in) lands a branch
	 * mid-instruction and is rejected here. */
	for (pc = g0; pc < g1;) {
		int rel = relat(tbase + pc), len, t;
		if ((rel & 016) && !(rel & 1)) {
			pc += 2;
			continue;
		}
		len = decode(tbase + pc, pc, buf);
		if (len < 2)
			len = 2;
		if ((CFtype == CF_COND || CFtype == CF_JUMP || CFtype == CF_CALL) && (t = CFtarg) >= 0) {
			if (t >= g0 && t < g1) {
				if (start[(t - g0) / 2] != 1) {
					if (getenv("DASDBG"))
						fprintf(stderr, "gap reject2 pc=%06o targ=%06o\n", pc, t);
					ok = 0;
					break;
				}
			} /* in-gap: a boundary */
			else if (t >= Tsize) {
				if (getenv("DASDBG"))
					fprintf(stderr, "gap reject3 pc=%06o targ=%06o\n", pc, t);
				ok = 0;
				break;
			} /* outside: at least a valid text address */
		}
		pc += len;
	}
	free(start);
	return ok;
}

/* Recursive-descent: walk control flow from every defined text symbol and mark
 * each reachable instruction's bytes (Mark[a]=1 at a start).  Unreached bytes
 * (jump tables, inline char/string args) stay 0 and are emitted as data. */
static void
markcode(long tbase, int a1)
{
	int *q, qn = 0, qi = 0, i, qmax;
	char buf[120];
	if (a1 <= 0 || !Mark || !Targ)
		return;
	/* a text segment with relocation info but no relocations at all is USUALLY
	 * constant data (real code relocates its branch/call/data references) --
	 * e.g. the lexer's `.byte' char tables, whose only symbol labels the table.
	 * But PURE REGISTER code carries no relocations either (libbstring's
	 * bcopy.s: register-only copy loops with branches to local labels).  The
	 * discriminator: real code tiles cleanly from address 0 AND branches to
	 * its own text labels; a char table does neither.  Only when the
	 * structural test fails leave the segment as data (byte-identical). */
	if (HasReloc) {
		int a, any = 0;
		for (a = 0; a + 1 < a1; a += 2)
			if (relat(tbase + a)) {
				any = 1;
				break;
			}
		if (!any) {
			int pc = 0, lbl = 0;
			char tb2[120];
			while (pc < a1) {
				int len = decode(tbase + pc, pc, tb2);
				if (len < 2)
					len = 2;
				if (pc + len > a1)
					break;
				if ((CFtype == CF_COND || CFtype == CF_JUMP || CFtype == CF_CALL) && CFtarg >= 0 && haslabel(CFtarg, N_TEXT))
					lbl++;
				pc += len;
			}
			if (pc != a1 || !lbl)
				return;
		}
	}
	qmax = a1 + NSym + 64;
	q = malloc(sizeof(int) * qmax);
	for (i = 0; i < NSym; i++) /* seeds: defined text symbols (entries) */
		if (BASETYPE(Sym[i].type) == N_TEXT && (Sym[i].value & 0xffff) < a1 && qn < qmax) {
			/* also a decode BARRIER: a multi-word junk decode must not
			 * straddle a symbol's address or its label line is never
			 * emitted and it reassembles undefined (V5 exp.o's FP
			 * constant table P0..big lives in TEXT; `bitb' junk from
			 * P2 swallowed Q0's word).  An ODD-valued symbol (a label
			 * mid-string, fxe.o's `tfil1') can never START an
			 * instruction: barrier its word, do not seed a decode --
			 * a misaligned junk walk plants phantom Targ/Mark. */
			if (Sym[i].value & 1) {
				Targ[Sym[i].value & 0xffff & ~1] = 1;
				continue;
			}
			q[qn++] = Sym[i].value & 0xffff;
			Targ[Sym[i].value & 0xffff] = 1;
		}
	/* seeds: absolute text pointers -- a word relocated RTEXT but NOT pc-relative
	 * holds the address of a code block (a jump-table entry, function pointer, or
	 * a handler address an interrupt-vector table points to) only reached through
	 * a computed jump / hardware vector we cannot follow.  Scan both segments:
	 * such a pointer table may live in text or data. */
	{
		int a;
		/* an ODD pointer (2.11 PROFCODE's counter cell holds `_func+1') can
		 * never be an instruction start: barrier only, no decode seed */
		for (a = 0; a + 1 < a1; a += 2)		       /* text words */
			if ((relat(tbase + a) & 017) == 002) { /* RTEXT, pcrel bit clear */
				int v = (w16(tbase + a) - (V2Bias ? V2BASE : 0)) & 0xffff;
				if (v < a1 && qn < qmax) {
					if (v & 1) {
						Targ[v & ~1] = 1;
						continue;
					}
					Targ[v] = 1;
					q[qn++] = v;
				}
			}
		for (a = 0; a + 1 < Dsize; a += 2) /* data words */
			if ((relat(Dbase + a) & 017) == 002) {
				int v = (w16(Dbase + a) - (V2Bias ? V2BASE : 0)) & 0xffff;
				if (v < a1 && qn < qmax) {
					if (v & 1) {
						Targ[v & ~1] = 1;
						continue;
					}
					Targ[v] = 1;
					q[qn++] = v;
				}
			}
	}
	{ /* NO DEFINED TEXT SYMBOL at all (maki's a.out: a linked program
	   * whose symbols are all data/bss): text must start with code --
	   * seed address 0, or the whole head raw-dumps and its pc-relative
	   * operand words have no representable spelling.  Objects with any
	   * text symbol keep the symbol-seeded behavior. */
		int k2, anytext = 0, at0 = 0;
		for (k2 = 0; k2 < NSym; k2++)
			if (BASETYPE(Sym[k2].type) == N_TEXT && Sym[k2].name[0]) {
				anytext = 1;
				if ((Sym[k2].value & 0xffff) == 0)
					at0 = 1;
			}
		if (!anytext && a1 > 0 && qn < qmax)
			q[qn++] = 0;
		/* under the 1972 personality, whole programs simply START at 0
		 * (maki: symbols exist but none at the entry) -- seed it there
		 * too.  Kept era-gated: a broad seed regressed newer table-at-0
		 * objects. */
		else if (V2Sys && !at0 && a1 > 0 && qn < qmax)
			q[qn++] = 0;
	}
	if (qn == 0) { /* stripped binary: no symbol/reloc seeds.  Seed the text start
			* plus every `jsr r5,csv' prologue (004567) -- the standard 2.8 C
			* function entry -- so the control-flow walk reaches every function
			* (a walk from 0 alone cannot follow the absolute `jsr pc,*$f' calls
			* to functions deeper in .text). */
		int a;
		if (qn < qmax)
			q[qn++] = 0;
		for (a = 0; a + 1 < a1; a += 2)
			if ((w16(tbase + a) & 0177777) == 0004567 && qn < qmax)
				q[qn++] = a;
	}
	while (qi < qn) {
		int pc = q[qi++];
		while (pc >= 0 && pc < a1 && Mark[pc] == 0) {
			int len, k, straddle = 0;
			if (relat(tbase + pc) & 016) { /* relocated inline data (e.g. sys arg) */
				Mark[pc] = 2;
				if (pc + 1 < a1)
					Mark[pc + 1] = 2;
				pc += 2;
				continue;
			}
			len = decode(tbase + pc, pc, buf); /* sets CFtype/CFtarg */
			if (len < 2)
				len = 2;
			/* a multi-word decode that straddles a known branch/jsr target is
			 * really data (e.g. an inline char arg sitting before real code
			 * that something branches to) -- emit it as a 1-word datum instead */
			for (k = 2; k < len; k += 2)
				if (pc + k < a1 && (Mark[pc + k] == 1 || Targ[pc + k])) {
					straddle = 1;
					break;
				}
			if (straddle) {
				Mark[pc] = 2;
				if (pc + 1 < a1)
					Mark[pc + 1] = 2;
				pc += 2;
				continue;
			}
			Mark[pc] = 1;
			for (k = 1; k < len && pc + k < a1; k++)
				Mark[pc + k] = 2;
			if (CFjsrinline) { /* rsave frame word: always exactly one */
				pc += len;
				if (pc + 1 < a1) {
					Mark[pc] = 2;
					Mark[pc + 1] = 2;
					pc += 2;
				}
				/* fall through to normal flow from here */
				if (qn < qmax)
					q[qn++] = pc;
				continue;
			}
			if (CFsysargs) { /* `sys N' is followed by sysinline[N] inline data words.
					  * The count comes from sysent[] -- but a program can run
					  * under a different personality (the pascal interpreter's
					  * `sys fetchi' shares 61. with chroot and takes NO inline
					  * words).  The pcrel theorem disambiguates: neither a sys
					  * argument nor an opcode can carry a pcrel relocation, so
					  * a pcrel word AT the arg position means it is an operand
					  * (stop), and one right AFTER means this word is that
					  * operand's OPCODE (stop before consuming it). */
				int j;
				pc += len;
				for (j = 0; j < CFsysargs && pc + 1 < a1; j++) {
					int P = -1, q;
					if (relat(tbase + pc) & 1)
						break; /* the arg slot itself is pcrel */
					/* nearest pcrel word within reach: its instruction must
					 * start at P-4 or P-2, so this word may be consumed as an
					 * arg only if it lies strictly BELOW P-4 */
					for (q = pc + 2; q <= pc + 4 && q + 1 < a1; q += 2)
						if (relat(tbase + q) & 1) {
							P = q;
							break;
						}
					if (P >= 0 && pc >= P - 4)
						break;
					Mark[pc] = 2;
					Mark[pc + 1] = 2;
					pc += 2;
				}
				continue;
			}
			if (CFtarg >= 0 && CFtarg < a1) {
				if (getenv("DASDBG") && !Targ[CFtarg])
					fprintf(stderr, "targ %06o from pc %06o (%s)\n", CFtarg, pc, buf);
				Targ[CFtarg] = 1;
				if (qn < qmax)
					q[qn++] = CFtarg;
			}
			if (CFtype == CF_JUMP || CFtype == CF_STOP)
				break; /* no fall-through */
			pc += len;
		}
	}
	/* recover unreached code: a gap that carries pc-relative relocations is
	 * instruction operands, so it is code -- commit it only if it decodes as a
	 * self-consistent run of instructions (gap_is_code).  A gap that starts
	 * with real DATA (an f77 inline format string ahead of an unreached
	 * function) fails the whole-gap tiling; retry from the first pcrel word's
	 * owning instruction (it must start at P-4 or P-2 -- the theorem again)
	 * and commit the clean SUFFIX. */
	{
		int a = 0;
		while (a < a1) {
			int g0, s, P;
			if (Mark[a] != 0) {
				a += 2;
				continue;
			}
			g0 = a;
			while (a < a1 && Mark[a] == 0)
				a += 2; /* gap [g0,a) */
			s = -1;
			if (gap_is_code(tbase, g0, a))
				s = g0;
			else {
				for (P = g0; P < a; P += 2)
					if (relat(tbase + P) & 1)
						break;
				if (P < a) {
					if (P - 4 >= g0 && gap_is_code(tbase, P - 4, a))
						s = P - 4;
					else if (P - 2 >= g0 && gap_is_code(tbase, P - 2, a))
						s = P - 2;
				}
			}
			if (s >= 0) {
				int pc = s;
				char buf[120];
				while (pc < a) {
					int len, k, rel = relat(tbase + pc);
					if ((rel & 016) && !(rel & 1)) {
						Mark[pc] = 2;
						if (pc + 1 < a)
							Mark[pc + 1] = 2;
						pc += 2;
						continue;
					}
					len = decode(tbase + pc, pc, buf);
					if (len < 2)
						len = 2;
					Mark[pc] = 1;
					for (k = 1; k < len && pc + k < a; k++)
						Mark[pc + k] = 2;
					pc += len;
				}
			}
		}
	}
	free(q);
}

static int haslabel(int addr, int seg);

/* Mark CODE assembled into the DATA segment (the pascal FP interpreter's trap
 * stubs).  A pc-relative relocation can only come from an instruction operand,
 * so a data region carrying one is code -- the same theorem the text walk uses,
 * applied to .data (the VAX das needed its twin for locore's vector catcher).
 * Regions run between real data labels; one is committed only if it decodes as
 * a clean instruction tiling that consumes every pcrel word as an operand. */
static void
markdata(long dbase)
{
	long dm = dbase - Tsize; /* dm + addr = file offset of data address */
	int a0 = Tsize, a1 = Tsize + Dsize, r0, r1;
	char buf[120];
	if (Dsize <= 0 || !HasReloc)
		return;
	DMark = calloc(Dsize, 1);
	if (!DMark)
		return;
	r0 = a0;
	while (r0 < a1) {
		int pc, ok = 1, sawpc = 0, nbr = 0, badbr = 0, lblbr = 0;
		r1 = r0 + 2;
		while (r1 < a1 && !haslabel(r1, N_DATA))
			r1 += 2; /* region [r0,r1) */
		for (pc = r0; pc < r1; pc += 2)
			if (relat(dm + pc) & 1) {
				sawpc = 1;
				break;
			}
		for (pc = r0; pc < r1;) {
			int rel = relat(dm + pc), len;
			if (rel & 016) {
				if (rel & 1) {
					ok = 0;
					break;
				} /* pcrel word at a boundary: misaligned */
				pc += 2;
				continue; /* absolute pointer rides as data */
			}
			len = decode(dm + pc, pc, buf);
			if (len < 2)
				len = 2;
			if (pc + len > r1) {
				ok = 0;
				break;
			}
			DMark[pc - a0] = 1;
			/* pass-2 signal for pcrel-less regions: every branch target must
			 * land on an instruction boundary -- in this region a recorded
			 * start, elsewhere a real data label (region boundary).  A small
			 * dispatch stub (`cmp r3,$spc; beq 1f; br check') has no pcrel
			 * operand, but its `br check' mention is exactly what must not
			 * be lost to a raw-word dump. */
			if ((CFtype == CF_COND || CFtype == CF_JUMP || CFtype == CF_CALL) && CFtarg >= 0) {
				nbr++;
				if (haslabel(CFtarg, N_DATA))
					lblbr++;
				if (CFtarg >= r0 && CFtarg < r1) {
					if (!(CFtarg > r0 ? DMark[CFtarg - a0] == 1 || CFtarg > pc : 1))
						badbr++;
					/* (a forward in-region target is verified implicitly:
					 * if the tiling never lands there, pc!=r1 rejects) */
				}
				else if (!(CFtarg == r1 || haslabel(CFtarg, N_DATA) || (CFtarg >= a0 && CFtarg < a1 && (relat(dm + CFtarg) & 1))))
					badbr++;
			}
			pc += len;
			/* inline argument words ride as data here exactly as in the
			 * text walk: `sys read; 0:..; 512.' assembled into .data (V6
			 * get.s) must not have its count word tiled as an instruction
			 * (001000 decodes as `bne') -- same pcrel guard as markcode */
			if (CFjsrinline && pc + 1 < r1)
				pc += 2;
			else if (CFsysargs) {
				int j;
				for (j = 0; j < CFsysargs && pc + 1 < r1; j++) {
					int P = -1, qq;
					if (relat(dm + pc) & 1)
						break;
					for (qq = pc + 2; qq <= pc + 4 && qq + 1 < r1; qq += 2)
						if (relat(dm + qq) & 1) {
							P = qq;
							break;
						}
					if (P >= 0 && pc >= P - 4)
						break;
					pc += 2;
				}
			}
		}
		/* a pcrel-less region must show REAL control flow -- at least one
		 * branch to a genuine data label (fp's `br check') -- or plain data
		 * that happens to tile (V6 `sys read;0;0' blocks whose trailing word
		 * decodes as `bne .') would be committed as code */
		if (!ok || pc != r1 || (!sawpc && (lblbr == 0 || badbr)))
			/* not code after all -- clear the trial marks */
			for (pc = r0; pc < r1; pc += 2)
				DMark[pc - a0] = 0;
		else
			HasDataCode = 1;
		r0 = r1;
	}
}

/* disassemble the text in PDP-11 address range [a0,a1); tbase = file offset
 * of address 0 of the text segment. */
static void
disasm_text(long tbase, int a0, int a1, FILE *out)
{
	int addr = a0;
	char buf[120];
	while (addr < a1) {
		int len, i;
		labels(addr, N_TEXT, out);
		InsnMinRef = NSym;
		/* Data, not code: a relocated word where an opcode would be (inline
		 * `sys'/data argument -- opcodes are never relocated), or a byte the
		 * control-flow walk did not reach as an instruction start.  Emit it
		 * symbolically (so relocations survive) or as a raw word. */
		/* a symbol at an ODD text address proves its containing word is
		 * BYTE data (as can only define such a label between .byte
		 * statements) -- override a junk instruction decode (the string
		 * region behind fxe.o's seeded `mes1' tiles as valid junk) */
		if ((relat(tbase + addr) & 016) || (Mark && Mark[addr] != 1) || (Asm && haslabel(addr + 1, N_TEXT))) {
			/* a SYMBOL at the ODD address inside this data word (a message
			 * label mid-string in .text -- V5 fxe.o's `emes1'/`tfil1' at
			 * 0o43): split the word into bytes so the label line can sit
			 * between them, exactly as the data emitter does */
			if (Asm && !(relat(tbase + addr) & 016) && haslabel(addr + 1, N_TEXT)) {
				fprintf(out, "\t.byte %o\n", F[tbase + addr] & 0377);
				labels2(addr + 1, N_TEXT, out, F[tbase + addr + 1] & 0377);
				buf[0] = 0;
				len = 2;
			}
			else if (!symword(tbase + addr, addr, buf))
				sprintf(buf, "%o", w16(tbase + addr));
			len = 2;
		}
		else {
			len = decode(tbase + addr, addr, buf);
			/* a multi-word instruction must not straddle the text end: the last
			 * word is data, not the opcode of an instruction whose operand word
			 * lies past .text (V6 dsw.s ends in a bare 005077, not `clr @#...').
			 * Emitting the phantom operand would grow .text and shift bss. */
			if (addr + len > a1) {
				if (!symword(tbase + addr, addr, buf))
					sprintf(buf, "%o", w16(tbase + addr));
				len = 2;
			}
			else if (Asm && SpliceRaw) { /* unreassemblable operand (see
						      * SpliceRaw): the raw words, `;'-joined on one line so
						      * the piece spans the same [addr,addr+len); a word that
						      * carries a relocation is spelled symbolically so the
						      * relocation survives */
				char *p = buf;
				char sw[64];
				for (i = 0; i < len; i += 2) {
					p += sprintf(p, "%s", i ? "; " : "");
					if (symword(tbase + addr + i, addr + i, sw))
						p += sprintf(p, "%s", sw);
					else
						p += sprintf(p, "%o", w16(tbase + addr + i));
				}
			}
		}
		/* flush declarations the original interned before this instruction's
		 * first undefined-external reference (they must precede its mention);
		 * if that reference is itself a pending declaration (a `.comm' common or
		 * a pinned `.globl'), emit it too -- its own reference must follow it */
		if (Asm && !Buffering && InsnMinRef < NSym)
			flush_decls(InsnMinRef + (needs_decl(InsnMinRef) ? 1 : 0), out);
		if (Asm) {
			if (!Buffering)
				seg_switch(N_TEXT, out);
			fprintf(out, "\t%s\n", buf);
			addr += len;
			continue;
		}
		fprintf(out, "\t%06o:  ", addr);
		for (i = 0; i < len; i += 2)
			fprintf(out, "%06o ", w16(tbase + addr + i));
		for (i = len; i < 6; i += 2)
			fprintf(out, "       ");
		fprintf(out, "  %s\n", buf);
		addr += len;
	}
	/* a text symbol can mark the very end of .text (one past the last
	 * instruction) -- e.g. a boot loader's `end' that a pointer references.
	 * The loop stops before it, so emit it here (only at the real text end,
	 * not at a per-object split boundary, to avoid a double definition). */
	if (a1 == Tsize)
		labels(a1, N_TEXT, out);
}

static int haslabel(int addr, int seg);

/* dump the data segment [a0,a0+size) as labelled words; dbase = file offset of
 * address a0. */
static void
disasm_data(long dbase, int a0, int size, FILE *out)
{
	int addr = a0, end = a0 + size;
	char sym[48];
	while (addr < end) {
		long wo = dbase + (addr - a0);
		labels(addr, N_DATA, out);
		if (Asm) {
			/* code assembled into .data (DMark, pcrel-reloc theorem): emit
			 * the instruction so its operands reassemble with their pcrel
			 * relocations -- a raw word cannot carry one */
			if (DMark && addr >= Tsize && addr - Tsize < Dsize && DMark[addr - Tsize] == 1) {
				char ib[120];
				int len = decode(wo, addr, ib);
				if (len < 2)
					len = 2;
				if (SpliceRaw) {
					char *p = ib;
					int k;
					char sw[64];
					for (k = 0; k < len; k += 2) {
						p += sprintf(p, "%s", k ? "; " : "");
						if (symword(wo + k, addr + k, sw))
							p += sprintf(p, "%s", sw);
						else
							p += sprintf(p, "%o", w16(wo + k));
					}
				}
				fprintf(out, "\t%s\n", ib);
				addr += len;
				continue;
			}
			/* a data symbol can sit at an odd address (a byte field in a struct,
			 * e.g. as11.s's outfile/globfl); split the word into two .byte so the
			 * label lands between them -- a word-granular dump would drop it and
			 * shift every reference to it */
			if (addr + 1 < end && haslabel(addr + 1, N_DATA)) {
				/* label and high byte on ONE line: no walk piece cut
				 * can separate them (1bsd as.o's `mesgf') */
				int v = w16(wo);
				fprintf(out, "\t.byte %o\n", v & 0377);
				if (Asm)
					labels2(addr + 1, N_DATA, out, (v >> 8) & 0377);
				else {
					labels(addr + 1, N_DATA, out);
					fprintf(out, "\t.byte %o\n", (v >> 8) & 0377);
				}
			}
			/* A relocated data word is a POINTER -- symbolize it (external name,
			 * or an internal `nearest-label+offset', which reassembles to the
			 * same address AND relocation type); a word-granular dump can label
			 * only even boundaries, but the +offset form covers odd targets
			 * (string tables).  A non-relocated word is a plain datum -> raw. */
			else if ((InsnMinRef = NSym, symword(wo, addr, sym))) {
				if (!Buffering && !InFragment && InsnMinRef < NSym)
					flush_decls(InsnMinRef + (needs_decl(InsnMinRef) ? 1 : 0), out);
				fprintf(out, "\t%s\n", sym);
			}
			else
				fprintf(out, "\t%o\n", w16(wo));
		}
		else
			fprintf(out, "\t%06o:  %06o\n", addr, w16(wo));
		addr += 2;
	}
	/* a data symbol can mark the very end of .data (one past the last word) --
	 * e.g. roff/primes' `ftabend' after a table.  The loop stops before it, so
	 * emit it here; otherwise it is undefined and every reference to it shifts.
	 * In a woven fragment the boundary symbol belongs to the NEXT fragment (or
	 * to emit_fragment's own zero-width handling), so leave it. */
	if (!InFragment)
		labels(end, N_DATA, out);
}

static int
haslabel(int addr, int seg)
{
	int i;
	for (i = 0; i < NSym; i++)
		if (Sym[i].value == addr && BASETYPE(Sym[i].type) == seg && Sym[i].name[0])
			return 1;
	return 0;
}

/* bss has no file content, so reserve space with `.=.+' -- but each label
 * must sit at its own offset (a single trailing `.=.+size' would collapse
 * them all to the start, shifting every bss address). */
static void
dump_bss(int a0, int size, FILE *out)
{
	int addr = a0, end = a0 + size;
	while (addr < end) {
		int next = addr + 1;
		labels(addr, N_BSS, out);
		/* byte-granular: bss variables can be `.byte'-sized and sit at odd
		 * addresses (m40.s has bflg/jflg/fflg/nofault in four adjacent bytes);
		 * a word-step scan would skip the odd ones and shift every later addr */
		while (next < end && !haslabel(next, N_BSS) && !(in_aux(next) && segof(next) == N_BSS))
			next++;
		fprintf(out, "\t.=.+%o\n", next - addr);
		addr = next;
	}
	/* a bss symbol can mark the very end of bss (one past the last cell) --
	 * f77 plants a bare `~' there (libcrtplot).  The loop stops before it. */
	if (!InFragment)
		labels(end, N_BSS, out);
}

/* emit segment `seg' content for the absolute address range [lo,hi) (the word
 * content of .data, or the `.=.+' reservations of .bss), switching segments
 * first; used to dump one woven fragment or the trailing remainder */
static void
emit_seg_range(int seg, int lo, int hi, FILE *out)
{
	if (hi <= lo)
		return;
	seg_switch(seg, out);
	InFragment = 1;
	if (seg == N_DATA)
		disasm_data(Dbase + (lo - Tsize), lo, hi - lo, out);
	else
		dump_bss(lo, hi - lo, out);
	InFragment = 0;
}

/* Weave the data/bss fragment beginning at symtab index `i' (a maximal run of
 * consecutive defined data/bss symbols in one segment) into the -a stream, in
 * symtab-index order.  Its content runs from the per-segment watermark up to the
 * next defined symbol of that segment (later index) or the segment end, which
 * tiles the segment exactly across all its fragments.  Advances NextDecl past
 * the run.  A zero-width fragment (a boundary/alias symbol with no new content)
 * still emits its label(s). */
static void
emit_fragment(int i, FILE *out)
{
	int seg = BASETYPE(Sym[i].type), fragend = i, j, a1, maxaddr;
	int segend = seg == N_DATA ? Tsize + Dsize : Tsize + Dsize + Bsz;
	int *wm = seg == N_DATA ? &WmData : &WmBss;
	/* extend over consecutive DEFINED-FIRST symbols of this segment only */
	while (fragend + 1 < NSym && is_seg_defined(fragend + 1) && BASETYPE(Sym[fragend + 1].type) == seg && WeaveEarly && WeaveEarly[fragend + 1])
		fragend++;
	/* content runs up to the next same-segment symbol BY ADDRESS (whether woven
	 * or reference-first), so a reference-first label is never pulled in early */
	maxaddr = Sym[fragend].value & 0xffff;
	a1 = segend;
	for (j = 0; j < NSym; j++)
		if (is_seg_defined(j) && BASETYPE(Sym[j].type) == seg && (Sym[j].value & 0xffff) > maxaddr && (Sym[j].value & 0xffff) < a1)
			a1 = Sym[j].value & 0xffff;
	if (a1 > *wm) {
		emit_seg_range(seg, *wm, a1, out);
		*wm = a1;
	}
	else { /* no new content -- emit just the boundary/alias label(s) */
		int prev = -1, k;
		seg_switch(seg, out);
		InFragment = 1;
		for (k = i; k <= fragend; k++) {
			int a = Sym[k].value & 0xffff;
			if (a != prev) {
				labels(a, seg, out);
				prev = a;
			}
		}
		InFragment = 0;
	}
	NextDecl = fragend + 1;
}

/* ---- -a emission: index-order walk over in-memory segment bodies ----------
 * The symbol table order = the order each name is first MENTIONED (declaration,
 * label, or operand reference) as `as' read the original source, and external
 * relocations cite symbols by table index -- so -a output must reproduce that
 * first-mention order exactly.  The compiler interleaves segments freely: a
 * `.data' switch table can forward-reference text labels (interning them in
 * TABLE order while their definitions sit elsewhere), so no fixed
 * text-then-data emission can reproduce every table.
 *
 * Strategy (the generalization of the VAX das's `freezesymtab' walk): build
 * each segment's body -- pure label+content lines -- into memory, find every
 * symbol's first mention in each body, then WALK the symtab in index order and
 * pick, per symbol, one of:
 *   DECL    -- .comm/.globl/~name=: insert at the current output position
 *              (never at the symbol's later use -- a C variable is declared in
 *              source order but first used in another order);
 *   STREAM  -- copy one body forward through the symbol's first-mention line,
 *              interning it exactly where the original stream mentioned it.
 * A body may only be streamed for symbol i if doing so is VIABLE: the mentions
 * it would emit must intern only i and later-index symbols, in index order,
 * with no smaller uninterned index skipped.  (On the VAX every switch table is
 * in .text, so text-streaming is always viable and the guard is vacuous; the
 * PDP-11's .data tables are exactly why it exists: for doprnt's `decimal',
 * text-streaming would pass `octal''s mention first -- not viable -- so the
 * walk streams the DATA body instead, emitting the swtab table right there,
 * which interns the handler labels in table order = index order.) */
struct wbody {
	char *s;
	size_t len;	 /* the body text */
	size_t cur;	 /* stream cursor (line-aligned) */
	long *ord, *eol; /* per-symbol: first-mention token offset / its line end (-1 none) */
};

static char **WalkName; /* per-symbol: asmname()'d name (what the bodies contain) */
static char *DeclKind;	/* per-symbol: pinned by a declaration directive, not a mention */

/* build a segment body: run the emitter with Buffering set so only label and
 * content lines come out (no declarations, no segment directives) */
static void
body_build(struct wbody *b, void (*emit)(FILE *), int nsym)
{
	FILE *f;
	memset(b, 0, sizeof *b);
	b->ord = malloc(nsym * sizeof(long));
	b->eol = malloc(nsym * sizeof(long));
	f = open_memstream(&b->s, &b->len);
	if (f) {
		Buffering = 1;
		emit(f);
		Buffering = 0;
		fclose(f);
	}
}

/* find each symbol's first whole-token mention in the body.  DECL symbols are
 * skipped: they are pinned by their directive, and their names can collide with
 * instruction mnemonics (an absolute local `neg') -- a match would be noise. */
static void
body_scan(struct wbody *b)
{
	size_t p = 0;
	int i, *nl;
	for (i = 0; i < NSym; i++) {
		b->ord[i] = -1;
		b->eol[i] = -1;
	}
	if (!b->s)
		return;
	nl = malloc(NSym * sizeof(int));
	for (i = 0; i < NSym; i++)
		nl[i] = WalkName[i] ? strlen(WalkName[i]) : 0;
	while (p < b->len) {
		char c = b->s[p];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '.' || c == '~') {
			size_t s0 = p, n;
			while (p < b->len) {
				c = b->s[p];
				if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '~')
					p++;
				else
					break;
			}
			n = p - s0;
			for (i = 0; i < NSym; i++)
				if (b->ord[i] < 0 && (!DeclKind[i] || ISEXT(Sym[i].type)) && (size_t)nl[i] == n && !strncmp(WalkName[i], b->s + s0, n)) {
					char *e = memchr(b->s + p, '\n', b->len - p);
					b->ord[i] = s0;
					b->eol[i] = e ? (e - b->s) + 1 : (long)b->len;
				}
		}
		else
			p++;
	}
	free(nl);
}

/* is symbol k still unpinned?  (its first mention in every body lies beyond the
 * cursor, and no directive for it has been emitted -- DeclEmitted covers that) */
static char *DeclEmitted;
static struct wbody TB, DB, BB; /* text / data / bss bodies */

static int
walk_interned(int k)
{
	if (DeclEmitted[k])
		return 1; /* directive (or `~alias=') already out */
	/* a DeclKind symbol's body MENTION also interns it (dbm.o's internal
	 * pcrel `jsr pc,_firstha' before its .globl slot) -- fall through */
	if (TB.ord[k] >= 0 && (size_t)TB.eol[k] <= TB.cur)
		return 1;
	if (DB.ord[k] >= 0 && (size_t)DB.eol[k] <= DB.cur)
		return 1;
	if (BB.ord[k] >= 0 && (size_t)BB.eol[k] <= BB.cur)
		return 1;
	return 0;
}

/* Would streaming body b through symbol i's first-mention line keep symtab
 * order?  The stream emits the pending mentions of a set S of symbols; require
 * (a) S's mention order == its index order with i last-or-inline, and (b) no
 * still-unpinned index smaller than max(S) is left out of S (it would intern
 * later, after a larger index). */
static int WVDbg;

static int
walk_viable(struct wbody *b, int i)
{
	int k, maxidx = i, prev = -1;
	long prevord = -1;
	if (b->ord[i] < 0 || (size_t)b->eol[i] <= b->cur) {
		if (WVDbg && getenv("DASDBG"))
			fprintf(stderr, "  wv %s b=%d: ord=%ld eol=%ld cur=%ld\n",
				Sym[i].name, b == &TB ? 0 : b == &DB ? 1
								     : 2,
				b->ord[i], b->eol[i], (long)b->cur);
		return 0;
	}
	/* S = unpinned k with a pending mention before i's line end, in mention order */
	for (;;) {
		int best = -1;
		long bestord = (long)b->eol[i];
		for (k = 0; k < NSym; k++) {
			if (walk_interned(k) || b->ord[k] < 0)
				continue;
			if ((size_t)b->ord[k] < b->cur)
				continue;
			if (b->ord[k] < bestord && b->ord[k] > prevord) {
				best = k;
				bestord = b->ord[k];
			}
		}
		if (best < 0)
			break;
		if (best < prev) { /* mention order breaks index order */
			if (WVDbg && getenv("DASDBG"))
				fprintf(stderr, "  wv %s b=%d: order %s(%d) before %s(%d)\n",
					Sym[i].name, b == &TB ? 0 : b == &DB ? 1
									     : 2,
					Sym[best].name, best, Sym[prev].name, prev);
			return 0;
		}
		if (best > maxidx)
			maxidx = best;
		prev = best;
		prevord = bestord;
	}
	/* no unpinned index between i and max(S) may be left out of S -- it would
	 * intern at a later walk step, AFTER the larger indices S just pinned */
	for (k = i + 1; k < maxidx; k++) {
		if (walk_interned(k))
			continue;
		if (DeclKind[k])
			continue; /* always pinnable: its directive can be
				   * flushed at any position before the piece --
				   * only its in-range MENTIONS (first loop)
				   * constrain the order, never its absence */
		if (!(b->ord[k] >= 0 && (size_t)b->ord[k] >= b->cur && b->ord[k] < b->eol[i])) {
			if (WVDbg && getenv("DASDBG"))
				fprintf(stderr, "  wv %s b=%d: skipped smaller %s(%d)\n",
					Sym[i].name, b == &TB ? 0 : b == &DB ? 1
									     : 2,
					Sym[k].name, k);
			return 0;
		}
	}
	return 1;
}

static int
idch2(int c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '~';
}

/* locate the recorded digit-form defs/refs in the FINAL body text by pattern
 * (a def is `N:' at line start; a ref is `N' + `f'/`b' at token boundaries);
 * occurrence order matches the build-time call order.  On any count mismatch
 * the patch pass is skipped entirely (current behavior). */
static int
build_auxpats(void)
{
	struct wbody *bl[3];
	int bi, nr = 0, nd = 0;
	bl[0] = &TB;
	bl[1] = &DB;
	bl[2] = &BB;
	free(ARef);
	free(ADef);
	ARef = 0;
	ADef = 0;
	NARef = 0;
	NADef = 0;
	ARefCap = 0;
	ADefCap = 0;
	if (NRefCall == 0 && NDefCall == 0)
		return 1;
	ARef = malloc((NRefCall ? NRefCall : 1) * sizeof *ARef);
	ADef = malloc((NDefCall ? NDefCall : 1) * sizeof *ADef);
	if (!ARef || !ADef)
		return 0;
	for (bi = 0; bi < 3; bi++) {
		struct wbody *b = bl[bi];
		size_t p = 0;
		int atbol = 1;
		if (!b->s)
			continue;
		while (p < b->len) {
			char c = b->s[p];
			if (atbol && c >= '0' && c <= '9' && p + 1 < b->len && b->s[p + 1] == ':') {
				if (nd >= NDefCall)
					return 0;
				ADef[nd].body = bi;
				ADef[nd].off = p;
				ADef[nd].root = DefCall[nd];
				ADef[nd].gpos = -1;
				nd++;
				p += 2;
				atbol = 0;
				continue;
			}
			if (c >= '0' && c <= '9' && (p == 0 || !idch2(b->s[p - 1])) && p + 1 < b->len && (b->s[p + 1] == 'f' || b->s[p + 1] == 'b') && (p + 2 >= b->len || !idch2(b->s[p + 2]))) {
				if (nr >= NRefCall)
					return 0;
				ARef[nr].body = bi;
				ARef[nr].off = p + 1;
				ARef[nr].root = RefCall[nr];
				ARef[nr].gpos = -1;
				nr++;
				p += 2;
				atbol = 0;
				continue;
			}
			atbol = (c == '\n');
			p++;
		}
	}
	if (nr != NRefCall || nd != NDefCall)
		return 0;
	NARef = nr;
	NADef = nd;
	return 1;
}

static int WalkDry; /* dry run: assign stream positions, write nothing real */
static long WalkGPos;

/* is symbol a's LABEL line already in the emitted stream?  (walk_interned
 * counts mention- and directive-interning, but an ASSIGNMENT evaluates its
 * anchor immediately -- the anchor must be DEFINED, i.e. its label line out.
 * A label-first local's first mention IS its label: the body text at ord
 * starts `name:'.) */
static int
label_streamed(int a)
{
	struct wbody *b = BASETYPE(Sym[a].type) == N_TEXT ? &TB : BASETYPE(Sym[a].type) == N_DATA ? &DB
							  : BASETYPE(Sym[a].type) == N_BSS	  ? &BB
												  : 0;
	size_t o;
	int n;
	if (!b || !b->s || b->ord[a] < 0 || (size_t)b->eol[a] > b->cur)
		return 0;
	o = b->ord[a];
	n = WalkName[a] ? strlen(WalkName[a]) : 0;
	if (!n || o + n >= b->len)
		return 0;
	if (o > 0 && b->s[o - 1] != '\n')
		return 0;	   /* line start */
	return b->s[o + n] == ':'; /* a label definition */
}

/* copy body b forward through symbol i's first-mention line */
static void
walk_stream(struct wbody *b, int seg, long upto, FILE *out)
{
	if (!b->s || (size_t)upto <= b->cur)
		return;
	if (WalkDry) {
		int bid = b == &TB ? 0 : b == &DB ? 1
						  : 2;
		int m;
		for (m = 0; m < NARef; m++)
			if (ARef[m].body == bid && ARef[m].off >= (long)b->cur && ARef[m].off < upto)
				ARef[m].gpos = WalkGPos + (ARef[m].off - b->cur);
		for (m = 0; m < NADef; m++)
			if (ADef[m].body == bid && ADef[m].off >= (long)b->cur && ADef[m].off < upto)
				ADef[m].gpos = WalkGPos + (ADef[m].off - b->cur);
	}
	WalkGPos += upto - b->cur;
	seg_switch(seg, out);
	fwrite(b->s + b->cur, 1, upto - b->cur, out);
	b->cur = upto;
}

static void emit_decl(int i, FILE *out);

/* Stream body b through symbol i's first mention PIECEWISE, flushing pending
 * declaration directives in index order before each symbol the stream interns:
 * a `.comm' whose index precedes a streamed symbol's must be emitted before
 * that mention goes out (libovc vfork.s: sys.s decls, then `.comm _errno,2'
 * at slot 82, then the text operand that interns `savov' at 83).  Viability
 * has already established that the interned symbols' mention order equals
 * their index order, so a single forward pass suffices. */
static void
walk_stream_ordered(struct wbody *b, int seg, int i, FILE *out)
{
	for (;;) {
		int s = -1, k;
		long best = b->eol[i];
		for (k = 0; k < NSym; k++) { /* next symbol this stream interns */
			if (walk_interned(k) || b->ord[k] < 0)
				continue;
			if ((size_t)b->ord[k] < b->cur)
				continue;
			if (b->ord[k] < best) {
				s = k;
				best = b->ord[k];
			}
		}
		if (s < 0) {
			walk_stream(b, seg, b->eol[i], out);
			return;
		}
		/* the piece is a whole LINE and may intern several symbols at once
		 * (`mov __ovno,savov'): flush declarations preceding the LARGEST
		 * index the piece interns, not just s's */
		{
			int m = s, j;
			for (k = 0; k < NSym; k++) {
				if (walk_interned(k) || b->ord[k] < 0)
					continue;
				if ((size_t)b->ord[k] >= b->cur && b->ord[k] < b->eol[s] && k > m)
					m = k;
			}
			for (k = 0; k < m; k++)
				if (DeclKind[k] && !DeclEmitted[k] && Sym[k].name[0]) {
					/* an unpinned lower-index undefined external must intern
					 * before this directive: its mention may share the very
					 * LINE that forces the flush (`mov __ovno,savov' with
					 * `.comm _errno' between their indices), so only an
					 * explicit `.globl' can place it -- as the original did */
					for (j = 0; j < k; j++)
						if (!DeclKind[j] && !walk_interned(j) && is_undef_ext(j) && Sym[j].name[0] && Referenced && Referenced[j]) {
							fprintf(out, ".globl\t%s\n", asmname(Sym[j].name));
							DeclEmitted[j] = 1;
						}
					emit_decl(k, out);
					DeclEmitted[k] = 1;
				}
		}
		walk_stream(b, seg, b->eol[s], out);
		if (s == i)
			return;
	}
}

/* Demote an order-violating exact anchor to neighbor+offset.  Hand-written asm
 * (fpsim) spells an early reference to a not-yet-introduced cell through its
 * ALREADY-KNOWN neighbor (`aexp+2'), introducing `bexp' only later -- but the
 * operand formatter always picks the exact-match name, pulling bexp's slot
 * early.  At walk step i, any pending mention of a LATER-index local k that
 * blocks streaming is respelled `N+off' via an already-interned same-segment
 * LOCAL below it: same value, same segment, same relocation -- byte-identical,
 * only the slot order changes (back to the original's). */
static int
body_respell(struct wbody *b, int i)
{
	size_t p, s0, n;
	int k, j, did = 0;
	if (!b->s || b->ord[i] < 0)
		return 0;
	p = b->cur;
	while (p < (size_t)b->eol[i]) {
		char c = b->s[p];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '.' || c == '~') {
			s0 = p;
			while (p < b->len) {
				c = b->s[p];
				if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '~')
					p++;
				else
					break;
			}
			n = p - s0;
			/* a pending whole-token mention of a later-index unpinned local? */
			for (k = 0; k < NSym; k++) {
				int best;
				long delta, klen, rlen;
				char rep[48], *ns;
				if (k <= i || walk_interned(k) || is_undef_ext(k))
					continue;
				if (DeclKind[k] && b->ord[k] < 0)
					continue;
				if (BASETYPE(Sym[k].type) != N_TEXT && BASETYPE(Sym[k].type) != N_DATA && BASETYPE(Sym[k].type) != N_BSS)
					continue;
				if (!WalkName[k] || strlen(WalkName[k]) != n || strncmp(WalkName[k], b->s + s0, n))
					continue;
				if ((s0 == 0 || b->s[s0 - 1] == '\n') && s0 + n < b->len && b->s[s0 + n] == ':')
					break; /* its label line */
				/* nearest ALREADY-INTERNED same-segment local at/below k */
				best = -1;
				for (j = 0; j < NSym; j++) {
					if (j == k || !Sym[j].name[0] || ISEXT(Sym[j].type))
						continue;
					if (Sym[j].name[0] == '~')
						continue; /* define-only: a
							   * reference mints a fresh
							   * unhashed symbol */
					/* dup/cast-routed: defined only in the `~'
					 * namespace -- the hashed name is another
					 * symbol's (structure's `_create' pair) */
					if ((DupLocal && DupLocal[j]) || (ForceSynth && ForceSynth[j]))
						continue;
					if (seg_mismatch(j))
						continue;
					if (BASETYPE(Sym[j].type) != BASETYPE(Sym[k].type))
						continue;
					if ((Sym[j].value & 0xffff) > (Sym[k].value & 0xffff))
						continue;
					if (!walk_interned(j))
						continue;
					if (best < 0 || (Sym[j].value & 0xffff) > (Sym[best].value & 0xffff))
						best = j;
				}
				if (best < 0) { /* NO interned lower anchor exists.  First try a
						 * PROMOTE wish (a value-congruent word may simply be
						 * spelled through the wrong anchor -- fp.o's bss
						 * cells); if a previous retry's wish did not take,
						 * escalate to synthetic-reference spelling. */
					WalkDegraded++;
					if (getenv("DASDBG"))
						fprintf(stderr, "deadend %s (slot %d) blocking walk step %s (slot %d) at bodyoff %ld body %d cur %ld eol %ld\n", Sym[k].name, k, Sym[i].name, i, (long)s0, b == &TB ? 0 : b == &DB ? 1
																												   : 2,
							(long)b->cur, (long)b->eol[i]);
					if (ForceSynth && !AnnealEval && !ISEXT(Sym[k].type) && !OvObj && !(SymNoReloc && !HasReloc) && Sym[k].name[0] != '~') {
						if (PromoteWish && !PromoteWish[k] && !AssignPin[k] && !ForceSynth[k]) {
							PromoteWish[k] = 1;
							FSNew = 1;
							if (getenv("DASDBG"))
								fprintf(stderr, "promote %s (slot %d, respell dead-end)\n", Sym[k].name, k);
						}
						/* SynthFirstN (leading-mention synthesis) is
						 * plumbed but DORMANT: enabling it in the
						 * ladder regressed four passing objects
						 * without resolving libb printf.o -- needs
						 * slot-compatible victim analysis first */
						else if (EscPhase >= 1 && AssignPin && !AssignPin[k] && !ForceSynth[k] && assign_anchor_exists(k)) {
							AssignPin[k] = 1;
							FSNew = 1;
							if (PromoteWish)
								PromoteWish[k] = 0;
							if (getenv("DASDBG"))
								fprintf(stderr, "assignpin %s (slot %d)\n", Sym[k].name, k);
						}
						else if (EscPhase >= 2 && !ForceSynth[k]) {
							ForceSynth[k] = 1;
							FSNew = 1;
							if (AssignPin)
								AssignPin[k] = 0;
							if (getenv("DASDBG"))
								fprintf(stderr, "forcesynth %s (slot %d)\n", Sym[k].name, k);
						}
						else
							NeedEsc = 1;
					}
					break;
				}
				if ((Sym[best].value & 0xffff) == (Sym[k].value & 0xffff))
					break; /* alias: other mechanism */
				if (getenv("DASDBG"))
					fprintf(stderr, "demote %s(%d) -> %s+%o at step %s(%d)\n",
						Sym[k].name, k, WalkName[best], (Sym[k].value - Sym[best].value) & 0xffff, Sym[i].name, i);
				sprintf(rep, "%s+%o", WalkName[best], (Sym[k].value - Sym[best].value) & 0xffff);
				rlen = strlen(rep);
				klen = n;
				delta = rlen - klen;
				ns = malloc(b->len + delta + 1);
				if (!ns)
					return did;
				memcpy(ns, b->s, s0);
				memcpy(ns + s0, rep, rlen);
				memcpy(ns + s0 + rlen, b->s + s0 + klen, b->len - (s0 + klen));
				free(b->s);
				b->s = ns;
				b->len += delta;
				b->s[b->len] = 0;
				for (j = 0; j < NSym; j++) {
					if (b->ord[j] > (long)s0)
						b->ord[j] += delta;
					if (b->eol[j] > (long)s0)
						b->eol[j] += delta;
				}
				{
					int bid = b == &TB ? 0 : b == &DB ? 1
									  : 2;
					int m;
					for (m = 0; m < NARef; m++)
						if (ARef[m].body == bid && ARef[m].off > (long)s0)
							ARef[m].off += delta;
					for (m = 0; m < NADef; m++)
						if (ADef[m].body == bid && ADef[m].off > (long)s0)
							ADef[m].off += delta;
				}
				/* k's next mention (if any) now pins it; rescan from here */
				{
					size_t q = p + delta;
					long o2 = -1, e2 = -1;
					/* recompute k's first mention past this splice */
					size_t pp = 0;
					while (pp < b->len) {
						char cc = b->s[pp];
						if ((cc >= 'A' && cc <= 'Z') || (cc >= 'a' && cc <= 'z') || cc == '_' || cc == '.' || cc == '~') {
							size_t ss = pp, nn;
							while (pp < b->len) {
								cc = b->s[pp];
								if ((cc >= 'A' && cc <= 'Z') || (cc >= 'a' && cc <= 'z') || (cc >= '0' && cc <= '9') || cc == '_' || cc == '.' || cc == '~')
									pp++;
								else
									break;
							}
							nn = pp - ss;
							if (o2 < 0 && strlen(WalkName[k]) == nn && !strncmp(WalkName[k], b->s + ss, nn)) {
								char *e = memchr(b->s + pp, '\n', b->len - pp);
								o2 = ss;
								e2 = e ? (e - b->s) + 1 : (long)b->len;
							}
						}
						else
							pp++;
					}
					b->ord[k] = o2;
					b->eol[k] = e2;
					p = q;
				}
				did = 1;
				break;
			}
		}
		else
			p++;
	}
	return did;
}

/* the canonical spelling the operand formatter used for i's address, if i is a
 * LOCAL label aliased by another LOCAL at the same address (0 otherwise -- an
 * external alias relocates differently, so its spelling is never exchangeable) */
static char *
alias_canonical(int i)
{
	int seg = BASETYPE(Sym[i].type), j;
	char *cn;
	if (ISEXT(Sym[i].type))
		return 0;
	if (seg != N_TEXT && seg != N_DATA && seg != N_BSS)
		return 0;
	cn = labelat(Sym[i].value & 0xffff, seg);
	if (!cn || !strcmp(cn, Sym[i].name))
		return 0;
	for (j = 0; j < NSym; j++)
		if (!strcmp(Sym[j].name, cn) && BASETYPE(Sym[j].type) == seg && Sym[j].value == Sym[i].value)
			return ISEXT(Sym[j].type) ? 0 : cn;
	return 0;
}

/* body_build() emit callbacks (geometry via file-scope, C has no closures) */
static long WkTBASE, WkDBASE;
static int WkText, WkData, WkBss;

static void
emit_text_body(FILE *f)
{
	disasm_text(WkTBASE, 0, WkText, f);
}

static void
emit_data_body(FILE *f)
{
	if (WkData)
		disasm_data(WkDBASE, WkText, WkData, f);
}

/* bss body runs even when bss==0: f77 plants its `~' end marker at the (empty)
 * bss start (libcrtplot's pset.o), emitted by dump_bss's end-label case */
static void
emit_bss_body(FILE *f)
{
	dump_bss(WkText + WkData, WkBss, f);
}

/* header banner */
static void
banner(FILE *out, char *what, int magic, int text, int data, int bss)
{
	int c = Asm ? '/' : ';'; /* `as' comments are `/', not `;' */
	fprintf(out, "%c %s  --  pdp11-bsd29-das%s%s%s%s\n", c, what,
		Asm ? " -a (reassemblable)" : " disassembly",
		OvObj ? "  [ovas: reassemble with `as -V']" : V7Obj ? "  [v7as: reassemble with `as -7']"
								    : "",
		NewFmt ? "  [nsym: 2.11 string-table format, `as -n']" : "",
		NeedTab211 ? "  [tab211: reassemble with `as --isa=bsd211']" : "");
	if (!Asm)
		fprintf(out, "; magic 0%o  text %d  data %d  bss %d  (%d symbols)\n",
			magic, text, data, bss, NSym);
	fprintf(out, "\n");
}

/* Disassemble one self-contained object (exec header at file offset `base`)
 * -- a bare .o or an archive member -- to `out`. */
/* one full pass of the index-order walk (shared by the dry run and the real
 * emission; all cut decisions are deterministic given identical start state) */
static void
walk_pass(FILE *out)
{
	int i;
	/* emit-time definition marks restart from the built-body labels:
	 * the DRY pass's casts must not suppress the REAL pass's sweep */
	if (LabelOut && LabelBuilt)
		memcpy(LabelOut, LabelBuilt, NSym ? NSym : 1);
	CurSeg = -1;
	seg_switch(N_TEXT, out);
	/* -y FAST PATH: on a no-reloc image the symtab order is link
	 * metadata (ldnr restores it), so the intern-order machinery --
	 * per-slot streaming, body_respell demotes, escalation -- buys
	 * nothing and costs O(n^2) body rewrites (mkfs, qterm).  Emit
	 * the declarations, stream the bodies whole, cast the rest. */
	if (SymNoReloc && !HasReloc) {
		for (i = 0; i < NSym; i++)
			if (Sym[i].name[0] && DeclKind[i] && !DeclEmitted[i]) {
				emit_decl(i, out);
				DeclEmitted[i] = 1;
			}
		walk_stream(&TB, N_TEXT, (long)TB.len, out);
		walk_stream(&DB, N_DATA, (long)DB.len, out);
		walk_stream(&BB, N_BSS, (long)BB.len, out);
		for (i = 0; i < NSym; i++) {
			int bt = BASETYPE(Sym[i].type);
			if (!Sym[i].name[0] || (LabelOut && LabelOut[i]))
				continue;
			if (bt != N_TEXT && bt != N_DATA && bt != N_BSS)
				continue;
			emit_cast(i, out);
		}
		return;
	}
	for (i = 0; i < NSym; i++) {
		if (!Sym[i].name[0])
			continue;

		if (walk_interned(i)) {
			if (DeclKind[i] && !DeclEmitted[i]) {
				emit_decl(i, out);
				DeclEmitted[i] = 1;
			}
			continue;
		}
		if (DeclKind[i]) {
			/* an unpinned LOWER-index undefined external must intern
			 * before this directive -- the original declared it
			 * (`.globl __ovno' ahead of `.comm _errno', libovc vfork.s);
			 * nothing else can place it that early, so emit its .globl */
			int k;
			for (k = 0; k < i; k++)
				if (!DeclKind[k] && !walk_interned(k) && is_undef_ext(k) && Sym[k].name[0] && Referenced && Referenced[k]) {
					fprintf(out, ".globl\t%s\n", asmname(Sym[k].name));
					DeclEmitted[k] = 1;
				}
			emit_decl(i, out);
			DeclEmitted[i] = 1;
			continue;
		}
		/* a later-index alias of an earlier local label: its label line was
		 * suppressed at body build; emit the compiler's own idiom
		 * `~name=canonical' here -- identical table entry, right index.
		 * (RAW name after the marker tilde, like emit_decl's reg/abs forms:
		 * the `~' IS the namespace marker as consumes.) */
		{
			char *cn = alias_canonical(i);
			if (cn) {
				fprintf(out, "~%s=%s\n", Sym[i].name, cn);
				DeclEmitted[i] = 1;
				if (LabelOut)
					LabelOut[i] = 1;
				continue;
			}
		}
		if (AssignPin && AssignPin[i] && !DeclEmitted[i]) {
			/* pin with `name = anchor+off': identical symtab entry
			 * to a label, emittable at any slot */
			int bb2, bs2 = -1;
			for (bb2 = 0; bb2 < i; bb2++) { /* earlier slot: defined by now */
				if (!Sym[bb2].name[0] || ISEXT(Sym[bb2].type))
					continue;
				if (Sym[bb2].name[0] == '~')
					continue;
				if ((DupLocal && DupLocal[bb2]) || (AssignPin && AssignPin[bb2]) || (ForceSynth && ForceSynth[bb2]))
					continue;
				if (BASETYPE(Sym[bb2].type) != BASETYPE(Sym[i].type))
					continue;
				if ((Sym[bb2].value & 0xffff) > (Sym[i].value & 0xffff))
					continue;
				if (!walk_interned(bb2))
					continue;
				if (alias_canonical(bb2))
					continue; /* label-suppressed:
						   * defined only in the
						   * unhashed `~' namespace,
						   * unreferencable (printn's
						   * `l1 = l3', unix.out's
						   * `sysflg = i.flgs+...') */
				if (bs2 < 0 || (Sym[bb2].value & 0xffff) > (Sym[bs2].value & 0xffff))
					bs2 = bb2;
			}
			if (bs2 >= 0) {
				int dl = (Sym[i].value - Sym[bs2].value) & 0xffff;
				if (dl)
					fprintf(out, "%s = %s+%o\n", WalkName[i], WalkName[bs2], dl);
				else
					fprintf(out, "%s = %s\n", WalkName[i], WalkName[bs2]);
				DeclEmitted[i] = 1;
				if (LabelOut)
					LabelOut[i] = 1;
				continue;
			}
		}
		if (DupLocal && DupLocal[i]) {
			/* sdb tilde entry: pin with `~name=anchor+off' via the
			 * nearest INTERNED same-segment local below */
			int bb2, bs2 = -1;
			for (bb2 = 0; bb2 < NSym; bb2++) {
				if (bb2 == i || !Sym[bb2].name[0] || ISEXT(Sym[bb2].type))
					continue;
				if (Sym[bb2].name[0] == '~')
					continue; /* define-only */
				if (DupLocal[bb2])
					continue;
				if (BASETYPE(Sym[bb2].type) != BASETYPE(Sym[i].type))
					continue;
				if ((Sym[bb2].value & 0xffff) > (Sym[i].value & 0xffff))
					continue;
				if (!walk_interned(bb2))
					continue;
				if (alias_canonical(bb2))
					continue; /* label-suppressed */
				if (bs2 < 0 || (Sym[bb2].value & 0xffff) > (Sym[bs2].value & 0xffff))
					bs2 = bb2;
			}
			if (bs2 >= 0) {
				int dl = (Sym[i].value - Sym[bs2].value) & 0xffff;
				if (dl)
					fprintf(out, "~%s=%s+%o\n", Sym[i].name, WalkName[bs2], dl);
				else
					fprintf(out, "~%s=%s\n", Sym[i].name, WalkName[bs2]);
				DeclEmitted[i] = 1;
				if (LabelOut)
					LabelOut[i] = 1;
				continue;
			}
			/* no anchor: fall through (bounded degradation) */
		}
		if (walk_viable(&TB, i)) {
			walk_stream_ordered(&TB, N_TEXT, i, out);
			continue;
		}
		if (walk_viable(&DB, i)) {
			walk_stream_ordered(&DB, N_DATA, i, out);
			continue;
		}
		if (walk_viable(&BB, i)) {
			walk_stream_ordered(&BB, N_BSS, i, out);
			continue;
		}
		/* blocked by an early mention of a later-index local: respell that
		 * mention as interned-neighbor+offset (fpsim's `aexp+2') and retry */
		if (body_respell(&TB, i) | body_respell(&DB, i) | body_respell(&BB, i)) {
			if (walk_viable(&TB, i)) {
				walk_stream_ordered(&TB, N_TEXT, i, out);
				continue;
			}
			if (walk_viable(&DB, i)) {
				walk_stream_ordered(&DB, N_DATA, i, out);
				continue;
			}
			if (walk_viable(&BB, i)) {
				walk_stream_ordered(&BB, N_BSS, i, out);
				continue;
			}
		}
		WalkDegraded++;
		if (getenv("DASDBG"))
			fprintf(stderr, "degraded %s (slot %d)\n", Sym[i].name, i);
		/* not pinnable in order: register a PROMOTE wish -- on the
		 * rebuild, symword spells a value-congruent internal word
		 * through this symbol so its name interns here (V6 alloc.o's
		 * alias pair, the 1972 fortran flag-bit words) */
		if (PromoteWish && !AnnealEval && !ISEXT(Sym[i].type) && Sym[i].name[0] != '~' && !(SymNoReloc && !HasReloc) /* -y: order is link
															      * metadata and the sweep-up defines
															      * every leftover -- no escalation */
		    && (BASETYPE(Sym[i].type) == N_TEXT || BASETYPE(Sym[i].type) == N_DATA || BASETYPE(Sym[i].type) == N_BSS) && !(DupLocal && DupLocal[i]) && !(ForceSynth && ForceSynth[i])) {
			if (!PromoteWish[i] && !AssignPin[i]) {
				PromoteWish[i] = 1;
				FSNew = 1;
				if (getenv("DASDBG"))
					fprintf(stderr, "promote %s (slot %d)\n", Sym[i].name, i);
			}
			else if (EscPhase >= 1 && SynthFirstN && !AssignPin[i]) {
				/* count i's mentions ALREADY BEHIND the cursor:
				 * those interned it too early -- synthesize
				 * exactly them on the rebuild (1972 threaded
				 * code cites a label early as a numbered local
				 * and later by name: libb printf.o's l3) */
				int nb = 0;
				struct wbody *bl3[3];
				int b3;
				bl3[0] = &TB;
				bl3[1] = &DB;
				bl3[2] = &BB;
				for (b3 = 0; b3 < 3; b3++) {
					struct wbody *bb = bl3[b3];
					size_t p2 = 0;
					int n2 = WalkName[i] ? strlen(WalkName[i]) : 0;
					if (!bb->s || !n2)
						continue;
					while (p2 + n2 <= bb->cur) {
						char c2 = p2 ? bb->s[p2 - 1] : '\n';
						if (!((c2 >= 'A' && c2 <= 'Z') || (c2 >= 'a' && c2 <= 'z') || (c2 >= '0' && c2 <= '9') || c2 == '_' || c2 == '.' || c2 == '~') && !strncmp(bb->s + p2, WalkName[i], n2) && !((bb->s[p2 + n2] >= 'A' && bb->s[p2 + n2] <= 'Z') || (bb->s[p2 + n2] >= 'a' && bb->s[p2 + n2] <= 'z') || (bb->s[p2 + n2] >= '0' && bb->s[p2 + n2] <= '9') || bb->s[p2 + n2] == '_' || bb->s[p2 + n2] == '.' || bb->s[p2 + n2] == '~') && bb->s[p2 + n2] != ':')
							nb++;
						p2++;
					}
				}
				if (nb > 0 && SynthFirstN[i] != nb && SynthFirstN[i] < 8) {
					SynthFirstN[i] = nb;
					FSNew = 1;
					if (getenv("DASDBG"))
						fprintf(stderr, "synthfirst %s (slot %d) = %d (counted)\n", Sym[i].name, i, nb);
				}
				else if (AssignPin && !AssignPin[i] && assign_anchor_exists(i)) {
					AssignPin[i] = 1;
					FSNew = 1;
					if (PromoteWish)
						PromoteWish[i] = 0;
					if (getenv("DASDBG"))
						fprintf(stderr, "assignpin %s (slot %d)\n", Sym[i].name, i);
				}
				else
					NeedEsc = 1;
			}
			else
				NeedEsc = 1;
		}
		/* not pinnable in order: stream whichever body mentions it anyway
		 * (bounded degradation -- content stays byte-exact, only the slot
		 * order may drift, exactly as before this walk existed) */
		if (TB.ord[i] >= 0 && (size_t)TB.eol[i] > TB.cur)
			walk_stream(&TB, N_TEXT, TB.eol[i], out);
		else if (DB.ord[i] >= 0 && (size_t)DB.eol[i] > DB.cur)
			walk_stream(&DB, N_DATA, DB.eol[i], out);
		else if (BB.ord[i] >= 0 && (size_t)BB.eol[i] > BB.cur)
			walk_stream(&BB, N_BSS, BB.eol[i], out);
		/* else: mentioned nowhere, no declaration -- nothing can pin it */
	}
	walk_stream(&TB, N_TEXT, (long)TB.len, out); /* the remainders */
	walk_stream(&DB, N_DATA, (long)DB.len, out);
	walk_stream(&BB, N_BSS, (long)BB.len, out);
	/* -y sweep-up: on a NO-RELOC image the symtab order is link
	 * metadata (ldnr restores it), so any defined text/data/bss
	 * symbol that fell off every route -- suppressed label, dup
	 * with no anchor, alias of a hash-owned name -- is emitted
	 * here as a position-independent `~name = value ^ donor'
	 * cast (structure's `_create' local vs its `_create' ext). */
	if (SymNoReloc && !HasReloc && LabelOut)
		for (i = 0; i < NSym; i++) {
			int bt = BASETYPE(Sym[i].type);
			if (!Sym[i].name[0] || LabelOut[i])
				continue;
			if (bt != N_TEXT && bt != N_DATA && bt != N_BSS)
				continue;
			emit_cast(i, out);
		}
}

/* UN-WEAVE an `ld -X -r'-processed object (2.10's profiled libraries:
 * `ld -X -r member.o' inserts an N_FN filename at slot 0 and RE-SORTS the
 * symtab into [filename, locals, globals], each block keeping its relative
 * order).  That post-ld order is not reproducible by any as source, but any
 * PRE-LD order with the same per-block relative orders is -- ld re-sorts it
 * back.  So: drop the N_FN, permute to [globals..., locals...] (globals
 * first: their .comm/.globl declarations can be flushed anywhere, so the
 * walk can honor the order -- SYS.h's `.comm _errno,2' precedes ENTRY), and
 * remap the REXT relocation indices in the file buffer.  The harness's
 * `ld -X -r <member>.o' tier reconstructs the filename slot. */
static int LdRDone;

static void
unweave_ldr(void)
{
	int i, n = 0, *map, *neworder;
	struct sym *ns;
	long a;
	if (LdRDone || !Asm || !HasReloc || NSym < 2)
		return;
	if (BASETYPE(Sym[0].type) != N_FN)
		return;
	LdRDone = 1;
	map = malloc(NSym * sizeof(int));
	neworder = malloc(NSym * sizeof(int));
	ns = malloc(NSym * sizeof(struct sym));
	if (!map || !neworder || !ns) {
		free(map);
		free(neworder);
		free(ns);
		return;
	}
	/* MERGE the two blocks by stream anchor, keeping each block's own
	 * relative order (ld re-sorts, so the interleaving is free -- but a
	 * globals-first order is often unachievable: pinning a high-address
	 * defined data global would stream past local labels, interning them
	 * early -- ruserpass.o's `_PC1_C:').  Anchor = (segment, address) for
	 * text/data/bss symbols; undefined externals, registers and absolutes
	 * inherit their predecessor's anchor (decls flush anywhere; sdb
	 * locals ride with their function's group). */
	{
		long *anch = malloc(NSym * sizeof(long));
		int gi, li, *gl, *ll, ng = 0, nl2 = 0;
		gl = malloc(NSym * sizeof(int));
		ll = malloc(NSym * sizeof(int));
		if (!anch || !gl || !ll) {
			free(anch);
			free(gl);
			free(ll);
			free(map);
			free(neworder);
			free(ns);
			return;
		}
		for (i = 1; i < NSym; i++)
			if (ISEXT(Sym[i].type))
				gl[ng++] = i;
		for (i = 1; i < NSym; i++)
			if (!ISEXT(Sym[i].type))
				ll[nl2++] = i;
		for (i = 1; i < NSym; i++) {
			int bt = BASETYPE(Sym[i].type);
			/* a symbol whose intern is a flushable DIRECTIVE (.globl/
			 * .comm/~alias -- every external, and abs/reg locals) is
			 * position-free: inherit.  Only plain LOCAL labels pin to
			 * their label line's stream position. */
			if (ISEXT(Sym[i].type) || (bt != N_TEXT && bt != N_DATA && bt != N_BSS)) {
				anch[i] = -1;
				continue;
			}
			anch[i] = bt == N_TEXT	 ? (long)(Sym[i].value & 0xffff)
				  : bt == N_DATA ? 0x10000L + (Sym[i].value & 0xffff)
						 : 0x20000L + (Sym[i].value & 0xffff);
		}
		for (i = 0; i < ng; i++)
			if (anch[gl[i]] < 0)
				anch[gl[i]] = i > 0 ? anch[gl[i - 1]] : 0;
		for (i = 0; i < nl2; i++)
			if (anch[ll[i]] < 0)
				anch[ll[i]] = i > 0 ? anch[ll[i - 1]] : 0;
		/* clamp each list monotonic (a lower-addressed symbol after a
		 * higher one keeps the later position -- suborder is fixed) */
		for (i = 1; i < ng; i++)
			if (anch[gl[i]] < anch[gl[i - 1]])
				anch[gl[i]] = anch[gl[i - 1]];
		for (i = 1; i < nl2; i++)
			if (anch[ll[i]] < anch[ll[i - 1]])
				anch[ll[i]] = anch[ll[i - 1]];
		gi = li = 0;
		while (gi < ng || li < nl2) {
			if (li >= nl2 || (gi < ng && anch[gl[gi]] <= anch[ll[li]]))
				neworder[n++] = gl[gi++];
			else
				neworder[n++] = ll[li++];
		}
		free(anch);
		free(gl);
		free(ll);
	}
	for (i = 0; i < NSym; i++)
		map[i] = -1;
	for (i = 0; i < n; i++) {
		ns[i] = Sym[neworder[i]];
		map[neworder[i]] = i;
	}
	memcpy(Sym, ns, n * sizeof(struct sym));
	NSym = n;
	for (a = 0; a + 1 < Tsize + Dsize; a += 2) { /* remap REXT reloc indices */
		long wo = RTbase + a;
		int rel = w16(wo);
		if ((rel & 016) == REXT && (rel >> 4) < NSym + 1 && map[rel >> 4] >= 0) {
			int nr = (map[rel >> 4] << 4) | (rel & 017);
			F[wo] = nr & 0377;
			F[wo + 1] = (nr >> 8) & 0377;
		}
	}
	for (FirstDef = 0; FirstDef < NSym; FirstDef++)
		if (!(ISEXT(Sym[FirstDef].type) && BASETYPE(Sym[FirstDef].type) == N_UNDF))
			break;
	if (getenv("DASDBG"))
		for (i = 0; i < NSym && i < 12; i++)
			fprintf(stderr, "unweave %d: %s type %o val %o\n", i, Sym[i].name, Sym[i].type, Sym[i].value & 0xffff);
	free(map);
	free(neworder);
	free(ns);
}

static void
do_object(long base, char *what, FILE *out)
{
	int magic = w16(base), text = w16(base + 2), data = w16(base + 4), bss = w16(base + 6),
	    syms = w16(base + 8), flag = w16(base + 14);
	long TBASE = base + 16, DBASE;
	/* V1-era 0405 executable (`br .+12'): a 12-byte header that a_text
	 * INCLUDES, then symbols and V1-format relocation.  Take the pure
	 * CONTENT path: text only, no symbols, no relocation model. */
	free(V1Rel);
	V1Rel = 0;
	if (magic == 0405) {
		/* header word 5 = the DATA AREA (exec sets the break to
		 * text+data; generated by trailing `.=.+n' -- the era's bss) */
		text = w16(base + 2) - 12;
		data = 0;
		bss = w16(base + 8);
		flag = 1;
		/* header word 2 is the symbol-table size (12-byte entries
		 * right after the text); word 3 the size of the RELOCATION
		 * BITS area (First Edition a.out(V)): a bit stream, 2-bit
		 * codes MSB-first in 16-bit words, one code per text word
		 * (header excluded), decoded here into a parallel array so
		 * the normal machinery sees real relocation.  Symbols only
		 * under -y (the generic SYMOFF lands on them: base+12+text). */
		syms = SymNoReloc ? w16(base + 4) : 0;
		TBASE = base + 12;
		{
			long rsz = w16(base + 6), roff = base + w16(base + 2) + w16(base + 4);
			if (rsz > 0 && roff + rsz <= FLEN && text > 0) {
				long bit = 0;
				int w, ok = 1;
				V1Rel = calloc(text / 2 + 1, sizeof(unsigned short));
				for (w = 0; V1Rel && w < text / 2; w++) {
					int c, c2;
#define V1BIT() (bit >= rsz * 8 ? (ok = 0) : ((w16(roff + ((bit / 16) * 2)) >> (15 - (bit % 16))) & 1))
					c = V1BIT() << 1;
					bit++;
					c |= V1BIT();
					bit++;
					if (!ok)
						break;
					if (c == 0)
						V1Rel[w] = 0; /* absolute */
					else if (c == 1)
						V1Rel[w] = 02; /* relocatable: RTEXT */
					else {		       /* external: 10 pcrel; 1100/1110 +16-bit ext */
						if (c == 3) {
							c2 = V1BIT() << 1;
							bit++;
							c2 |= V1BIT();
							bit++;
							if (c2 == 0 || c2 == 2)
								bit += 16;
						}
						V1Rel[w] = (c == 2) ? 011 : 010; /* REXT idx 0 */
					}
#undef V1BIT
				}
				if (!ok) {
					free(V1Rel);
					V1Rel = 0;
				}
				else
					flag = 0; /* relocation present */
			}
		}
	}
	V1Exec = (magic == 0405 && !V1Rel);
	DBASE = TBASE + text;
	/* a V1 0405 image has NO parallel relocation area in the file --
	 * the bit stream (decoded into V1Rel above) sits AFTER the symtab */
	long reloc = (flag || magic == 0405) ? 0 : (long)(text + data);
	long SYMOFF = DBASE + data + reloc;
	int i;
	readsyms(SYMOFF, syms);
	/* V1 symbol flags (First Edition): 00 undef, 01 abs, 02 REGISTER,
	 * 03 relocatable, |40 global -- translate to the later base types
	 * (03 -> TEXT: a linked V1 image is all text) so labels land;
	 * the harness translates back (invertible) */
	if (magic == 0405 && V1Rel)
		for (i = 0; i < NSym; i++) {
			int ty = Sym[i].type, bt = ty & 037;
			/* V1 flag 3 (`relocatable') covers text AND the data
			 * area alike, so everything maps to N_TEXT: a beyond-
			 * text cell then pins as `name = label+off' (the
			 * AssignPin form -- type TEXT, any value, slot-exact),
			 * which the V1 writer folds back to flag 3. */
			int nt = bt == 0 ? N_UNDF : bt == 1 ? N_ABS
					    : bt == 2	    ? N_REG
					    : bt == 3	    ? N_TEXT
							    : bt;
			Sym[i].type = nt | (ty & 040);
		}
	/* relocation geometry: reltext follows data, reldata follows reltext */
	Tbase = TBASE;
	Tsize = text;
	Dbase = DBASE;
	Dsize = data;
	Bsz = bss;
	NAuxSet = 0;
	AuxFrozen = 0;
	RTbase = DBASE + data;
	RDbase = DBASE + data + text;
	HasReloc = !flag;
	/* V6 ld leaves a_flag CLEAR on linked output even though it strips the
	 * relocation (2.79's bin.v6 binaries): trusting the flag would read
	 * symbol-table bytes as relocation.  Believe the GEOMETRY instead. */
	if (HasReloc && SYMOFF + syms > FLEN)
		HasReloc = 0;
	/* -a on a LINKED file (no relocation): a symbol-faithful .s cannot be
	 * reconstructed in general -- a separate-I&D (0411) data symbol lives in
	 * D-space overlapping text, and even in one space a label whose address
	 * falls mid-instruction cannot be defined -- but the all-NUMERIC stripped
	 * path reproduces every text+data byte exactly (pc-relative re-encodes
	 * against the same pc, branches take absolute expressions).  Drop the
	 * symbol table and take that path (2.79 bin.v6's unstripped eyacc/tset/
	 * csh; the magic is irrelevant once relocation is absent). */
	if (Asm && !HasReloc && !(SymNoReloc && (magic == 0405 || magic == 0407 || magic == 0410 || magic == 0411)))
		NSym = 0;
	/* an `ld -i' kernel (V4/V6 unix): DATA/BSS symbol values are data-space
	 * RELATIVE (they restart near 0), not unified -- normalize by +Tsize so
	 * labels land; the harness re-relativizes the output symtab.  For 0411
	 * (separate I&D) the D-space restart is FORMAT-DEFINED, no vote needed.
	 * t+d+bss can exceed 64K there; the +Tsize sum is masked to 16 bits, so
	 * a high bss symbol WRAPS -- its masked address is either still in the
	 * bss range (label lands there; the stored 16-bit value is identical
	 * mod 2^16, which is all the symtab keeps) or in text/data, where
	 * seg_mismatch() routes it to a `name = value ^ donor' cast. */
	if (SymNoReloc && !HasReloc && (magic == 0407 || magic == 0410 || magic == 0411) && NSym > 0) {
		int lo = 0, hi = 0, delta = 0;
		for (i = 0; i < NSym; i++) {
			int bt = BASETYPE(Sym[i].type);
			if (bt != N_DATA && bt != N_BSS)
				continue;
			if ((Sym[i].value & 0xffff) < text)
				lo++;
			else
				hi++;
		}
		if (magic == 0411 || (magic == 0407 && lo > hi && lo > 2))
			delta = text;	  /* D-space restarts at 0 */
		else if (magic == 0410) { /* shared text: data VA usually at
					   * the next 8K boundary after text
					   * -- but some images (m11.x) are
					   * linked with UNIFIED values, so
					   * VOTE like the -i case */
			int g = (((text + 8191) / 8192) * 8192) - text, va = 0, uni = 0;
			for (i = 0; i < NSym; i++) {
				int bt = BASETYPE(Sym[i].type), v = Sym[i].value & 0xffff;
				if (bt != N_DATA && bt != N_BSS)
					continue;
				if (v >= text + g)
					va++;
				else if (v >= text)
					uni++;
			}
			if (va > uni)
				delta = -g;
		}
		if (delta)
			for (i = 0; i < NSym; i++) {
				int bt = BASETYPE(Sym[i].type);
				if (bt == N_DATA || bt == N_BSS)
					Sym[i].value = (Sym[i].value + delta) & 0xffff;
			}
	}
	/* V1-era `..' bias detection: count internal non-pcrel words that only
	 * fall inside their segment once V2BASE is subtracted.  User programs
	 * load (and bias) at 0o40000; the kernel runs at 0 -- both need -2's
	 * sysent, only one the bias. */
	V2Bias = 0;
	if (V2Sys && HasReloc) {
		long a2;
		int raw = 0, unb = 0, end2 = text + data + bss;
		for (a2 = 0; a2 + 1 < text + data; a2 += 2) {
			int r2 = relat(TBASE + a2), v2;
			if (!(r2 & 016) || (r2 & 016) == REXT || (r2 & 1))
				continue;
			v2 = w16(TBASE + a2) & 0xffff;
			if (v2 < end2)
				raw++;
			if (((v2 - V2BASE) & 0xffff) < end2)
				unb++;
		}
		if (unb > raw && unb > 2)
			V2Bias = 1;
	}
	unweave_ldr();
	(void)magic;
	build_referenced();
	Mark = text > 0 ? calloc(text, 1) : 0; /* control-flow code/data map */
	Targ = text > 0 ? calloc(text, 1) : 0;
	markcode(TBASE, text);
	free(DMark);
	DMark = 0;
	HasDataCode = 0;
	markdata(DBASE); /* code assembled into .data */
	/* prescan: does any mfpi-family instruction carry a PC-RELATIVE operand?
	 * Those must be emitted as real mnemonics (a raw-word splice can't carry
	 * the pcrel bit), so the banner must announce `as --isa=bsd211' BEFORE
	 * the body streams.  Mark[a]==1 = instruction start (markcode above). */
	NeedTab211 = 0;
	if (Asm && !J11Dec && HasReloc) {
		long a;
		int w, dd, pass;
		/* pass 0: text (Mark); pass 1: code assembled into .data (DMark --
		 * mch_id.o's D-space trap trampolines do `mtpi _savfp' there) */
		for (pass = 0; pass < 2; pass++) {
			long lim = pass ? data : text;
			char *mk = pass ? DMark : Mark;
			long base = pass ? DBASE : TBASE;
			if (!mk)
				continue;
			if (pass && !HasDataCode)
				continue;
			for (a = 0; a + 3 < lim; a += 2) {
				if (mk[a] != 1)
					continue;
				w = w16(base + a);
				if ((w & 0177700) != 0006500 && (w & 0177700) != 0006600 &&
				    (w & 0177700) != 0106400 && (w & 0177700) != 0106500 &&
				    (w & 0177700) != 0106600 && (w & 0177700) != 0106700)
					continue;
				dd = w & 077;
				if ((dd & 070) != 060 && (dd & 070) != 070 && dd != 027 && dd != 037)
					continue;
				if (relat(base + a + 2) & 1)
					NeedTab211 = 1;
			}
		}
	}
	banner(out, what, magic, text, data, bss);
	if (Asm && V2Bias && HasReloc)
		fprintf(out, ".. = 40000\n");
	if (Asm) { /* scan pass + bodies + walk, under a REBUILD loop: a
		    * body_respell dead end (no interned lower anchor for a
		    * blocking mention) flags the blocker ForceSynth and the
		    * pipeline reruns -- its references then go through a
		    * synthetic local, so its NAME interns at its label line,
		    * the symtab-order position (V5 fx8.o's `tst nlflg'). */
		int retry, j2;
		int AnnealIdx = 0, AnnealActive = 0, AnnealSym = -1, BaseDegr = -1;
		char *SvWish = 0, *SvAssign = 0, *SvSynth = 0;
		EscPhase = 0;
		NFNAddr = 0;
		free(ForceSynth);
		ForceSynth = calloc(NSym ? NSym : 1, 1);
		free(PromoteWish);
		PromoteWish = calloc(NSym ? NSym : 1, 1);
		free(PromoteUsed);
		PromoteUsed = calloc(NSym ? NSym : 1, 1);
		free(AssignPin);
		AssignPin = calloc(NSym ? NSym : 1, 1);
		free(SynthFirstN);
		SynthFirstN = calloc(NSym ? NSym : 1, sizeof(int));
		free(MentionSeen);
		MentionSeen = calloc(NSym ? NSym : 1, sizeof(int));
		free(DupLocal);
		DupLocal = calloc(NSym ? NSym : 1, 1);
		if (ForceSynth && DupLocal)
			for (i = 0; i < NSym; i++) {
				int bt = BASETYPE(Sym[i].type);
				if (ISEXT(Sym[i].type) || !Sym[i].name[0])
					continue;
				if (bt != N_TEXT && bt != N_DATA && bt != N_BSS)
					continue;
				for (j2 = 0; j2 < NSym; j2++) {
					if (j2 == i)
						continue;
					/* a same-named DEFINED EXT owns the hashed name
					 * (structure's local `_create' vs the global): the
					 * local goes through `~' -- no-reloc images only,
					 * keeping the historical local-local rule on the
					 * relocatable corpora */
					if (ISEXT(Sym[j2].type) && !(SymNoReloc && !HasReloc))
						continue;
					if (BASETYPE(Sym[j2].type) != N_TEXT && BASETYPE(Sym[j2].type) != N_DATA && BASETYPE(Sym[j2].type) != N_BSS)
						continue;
					if (strcmp(Sym[i].name, Sym[j2].name))
						continue;
					DupLocal[i] = 1;
					ForceSynth[i] = 1;
					break;
				}
			}
		for (retry = 0;; retry++) {
			FILE *sink;
			int pats;
			FSNew = 0;
			if (retry) { /* state exactly as at first entry */
				build_referenced();
				WmData = WmBss = 0;
				if (PromoteUsed)
					memset(PromoteUsed, 0, NSym ? NSym : 1);
			}
			/* scan pass: resolve all references (to a null sink) so every
			 * synthetic local -- its definition address and the span of
			 * addresses referencing it -- is known, then color_aux() assigns
			 * the numbered-local digits before any definition/reference */
			sink = fopen("/dev/null", "w");
			NAuxSet = 0;
			AuxFrozen = 0;
			NextDecl = 0;
			if (MentionSeen)
				memset(MentionSeen, 0, NSym * sizeof(int));
			if (sink) {
				InTextScan = 1;
				disasm_text(TBASE, 0, text, sink);
				/* when the DATA segment carries code (markdata), its scan IS
				 * the continuation of the instruction stream: keep recording
				 * first-mention order there, so .globl pinning sees the whole
				 * program (fpdata printf.o has ALL its code in .data) */
				InTextScan = HasDataCode;
				if (data)
					disasm_data(DBASE, text, data, sink);
				InTextScan = 0;
				fclose(sink);
			}
			color_aux();
			compute_forceglobl();
			compute_weave_early();
			NextDecl = 0; /* reset the flush frontier the scan pass advanced */
			/* the index-order walk over segment bodies -- see the block comment
			 * above struct wbody */
			WkTBASE = TBASE;
			WkDBASE = DBASE;
			WkText = text;
			WkData = data;
			WkBss = bss;
			WalkName = malloc((NSym ? NSym : 1) * sizeof(char *));
			DeclKind = calloc(NSym ? NSym : 1, 1);
			DeclEmitted = calloc(NSym ? NSym : 1, 1);
			free(LabelOut);
			LabelOut = calloc(NSym ? NSym : 1, 1);
			free(LabelBuilt);
			LabelBuilt = calloc(NSym ? NSym : 1, 1);
			for (i = 0; i < NSym; i++) {
				WalkName[i] = strdup(asmname(Sym[i].name));
				DeclKind[i] = needs_decl(i);
			}
			NRefCall = 0;
			NDefCall = 0; /* per-object: the digit-form call ledgers */
			if (MentionSeen)
				memset(MentionSeen, 0, NSym * sizeof(int));
			body_build(&TB, emit_text_body, NSym ? NSym : 1);
			body_build(&DB, emit_data_body, NSym ? NSym : 1);
			body_build(&BB, emit_bss_body, NSym ? NSym : 1);
			body_scan(&TB);
			body_scan(&DB);
			body_scan(&BB);
			/* an ODD-address data/bss label's definition is the middle line of a
			 * `.byte lo / L: / .byte hi' split; cutting a stream piece right
			 * after the label line leaves the segment's dot ODD across the
			 * switch, and as (genuine opl25: `inc dot; bic $1,dot') rounds it up
			 * -- a phantom pad byte (PUCC mbuf11.o's odd string labels).  Extend
			 * such a label's piece one line so the split stays atomic. */
			for (i = 0; i < NSym; i++) {
				struct wbody *bb2 = BASETYPE(Sym[i].type) == N_TEXT ? &TB : BASETYPE(Sym[i].type) == N_DATA ? &DB
										    : BASETYPE(Sym[i].type) == N_BSS	    ? &BB
															    : 0;
				if (bb2 && (Sym[i].value & 1) && bb2->ord[i] >= 0 && (size_t)bb2->eol[i] < bb2->len) {
					char *e = memchr(bb2->s + bb2->eol[i], '\n', bb2->len - bb2->eol[i]);
					bb2->eol[i] = e ? (e - bb2->s) + 1 : (long)bb2->len;
				}
			}
			/* DRY RUN -- always: it detects respell dead ends (FSNew) and,
			 * for the recolor patch, assigns every recorded digit-form
			 * def/ref its stream position.  The walk is deterministic and
			 * the patch flips are single bytes, so the real run repeats
			 * the same cuts. */
			pats = build_auxpats();
			WalkDegraded = 0;
			sink = fopen("/dev/null", "w");
			if (sink) {
				WalkDry = 1;
				WalkGPos = 0;
				walk_pass(sink);
				WalkDry = 0;
				fclose(sink);
			}
			if (AnnealActive) {
				/* the trial run with ONE escalation cleared: keep it
				 * only if it degrades strictly LESS than the state it
				 * replaced (the baseline is rarely fully clean) */
				AnnealActive = 0;
				AnnealEval = 0;
				WVDbg = 0;
				if (WalkDegraded >= BaseDegr || FSNew || NeedEsc) {
					memcpy(PromoteWish, SvWish, NSym ? NSym : 1);
					memcpy(AssignPin, SvAssign, NSym ? NSym : 1);
					memcpy(ForceSynth, SvSynth, NSym ? NSym : 1);
					if (getenv("DASDBG"))
						fprintf(stderr, "anneal rejected (%s)\n", Sym[AnnealSym].name);
					FSNew = 1;
					NeedEsc = 0; /* force rebuild back to saved state */
				}
				else {
					if (getenv("DASDBG"))
						fprintf(stderr, "anneal kept (%s)\n", Sym[AnnealSym].name);
					BaseDegr = WalkDegraded;
					FSNew = 0;
					NeedEsc = 0;
				}
			}
			if (!FSNew && NeedEsc && EscPhase < 2 && retry < 200) {
				EscPhase++;
				FSNew = 1; /* unlock the next stage */
				if (getenv("DASDBG"))
					fprintf(stderr, "escphase %d\n", EscPhase);
			}
			if (!FSNew && !NeedEsc && retry < 180) {
				if (BaseDegr < 0)
					BaseDegr = WalkDegraded;
				/* stable -- ANNEAL: escalations taken in early rounds can
				 * be stale (printf.o's l9 was force-synth'd before the
				 * wishes that unblock it existed).  Try clearing each one
				 * back to a wish, one at a time; keep only clean results. */
				while (AnnealIdx < NSym) {
					i = AnnealIdx++;
					if (!((AssignPin && AssignPin[i]) || (ForceSynth && ForceSynth[i])))
						continue;
					if (!SvWish) {
						SvWish = malloc(NSym ? NSym : 1);
						SvAssign = malloc(NSym ? NSym : 1);
						SvSynth = malloc(NSym ? NSym : 1);
					}
					if (!SvWish || !SvAssign || !SvSynth)
						break;
					memcpy(SvWish, PromoteWish, NSym ? NSym : 1);
					memcpy(SvAssign, AssignPin, NSym ? NSym : 1);
					memcpy(SvSynth, ForceSynth, NSym ? NSym : 1);
					PromoteWish[i] = 1;
					AssignPin[i] = 0;
					ForceSynth[i] = 0;
					AnnealActive = 1;
					AnnealEval = 1;
					WVDbg = 1;
					AnnealSym = i;
					FSNew = 1;
					if (getenv("DASDBG"))
						fprintf(stderr, "anneal try (%s)\n", Sym[i].name);
					break;
				}
			}
			NeedEsc = 0;
			WalkDegraded = 0;
			if (FSNew && retry < 200) { /* rebuild under the new flags */
				for (i = 0; i < NSym; i++)
					free(WalkName[i]);
				free(WalkName);
				WalkName = 0;
				free(DeclKind);
				DeclKind = 0;
				free(DeclEmitted);
				DeclEmitted = 0;
				free(TB.s);
				free(TB.ord);
				free(TB.eol);
				memset(&TB, 0, sizeof TB);
				free(DB.s);
				free(DB.ord);
				free(DB.eol);
				memset(&DB, 0, sizeof DB);
				free(BB.s);
				free(BB.ord);
				free(BB.eol);
				memset(&BB, 0, sizeof BB);
				continue;
			}
			if (pats && (NARef || NADef) && sink) {
				int m, m2, nr2 = 0;

				/* stream-space RE-COLORING: spans in gpos coordinates,
				 * the same interval argument as color_aux -- disjoint
				 * same-digit spans mean nearest-in-direction binding
				 * always reaches the intended def.  Digits, like the
				 * direction letters, are single chars: rewriting the
				 * def line and every ref keeps all offsets valid. */
				struct {
					int root;
					long lo, hi, defg;
					int digit, defm;
				} *rt = 0;

				rt = malloc((NADef ? NADef : 1) * sizeof *rt);
				for (m = 0; m < NADef && rt; m++) {
					if (ADef[m].gpos < 0)
						continue;
					rt[nr2].root = ADef[m].root;
					rt[nr2].defm = m;
					rt[nr2].lo = rt[nr2].hi = rt[nr2].defg = ADef[m].gpos;
					rt[nr2].digit = -1;
					for (m2 = 0; m2 < NARef; m2++) {
						if (ARef[m2].root != ADef[m].root || ARef[m2].gpos < 0)
							continue;
						if (ARef[m2].gpos < rt[nr2].lo)
							rt[nr2].lo = ARef[m2].gpos;
						if (ARef[m2].gpos > rt[nr2].hi)
							rt[nr2].hi = ARef[m2].gpos;
					}
					nr2++;
				}
				for (m = 0; m < nr2; m++) {
					int used = 0, d;
					for (m2 = 0; m2 < m; m2++) {
						if (rt[m2].digit < 0)
							continue;
						if (rt[m2].hi < rt[m].lo || rt[m2].lo > rt[m].hi)
							continue;
						used |= 1 << rt[m2].digit;
					}
					for (d = 0; d < 10; d++)
						if (!(used & (1 << d))) {
							rt[m].digit = d;
							break;
						}
					/* a PLACEHOLDER root the stream could not color
					 * either: force it back to a NAMED synthetic
					 * (strippable by the ld tiers -- ioinit.o) and
					 * rebuild */
					if (rt[m].digit < 0 && AuxDigit[rt[m].root] < 0 && retry < 200) {
						if (NFNAddr >= FNCap) {
							FNCap = FNCap ? FNCap * 2 : 32;
							FNAddr = realloc(FNAddr, FNCap * sizeof(int));
						}
						if (FNAddr && !fn_named(AuxSet[rt[m].root]))
							FNAddr[NFNAddr++] = AuxSet[rt[m].root] & 0xffff;
						FSNew = 1;
					}
				}
				for (m = 0; m < nr2; m++) {
					struct wbody *db2;
					int root = rt[m].root;
					if (rt[m].digit < 0)
						continue; /* keep original digit */
					db2 = ADef[rt[m].defm].body == 0 ? &TB : ADef[rt[m].defm].body == 1 ? &DB
													    : &BB;
					db2->s[ADef[rt[m].defm].off] = '0' + rt[m].digit;
					for (m2 = 0; m2 < NARef; m2++) {
						struct wbody *rb;
						if (ARef[m2].root != root || ARef[m2].gpos < 0)
							continue;
						rb = ARef[m2].body == 0 ? &TB : ARef[m2].body == 1 ? &DB
												   : &BB;
						rb->s[ARef[m2].off - 1] = '0' + rt[m].digit;
						rb->s[ARef[m2].off] = rt[m].defg < ARef[m2].gpos ? 'b' : 'f';
					}
				}
				if (getenv("DASDBG")) {
					for (m = 0; m < nr2; m++)
						fprintf(stderr, "root %d addr %06o defg %ld span [%ld,%ld] digit %d\n",
							rt[m].root, AuxSet[rt[m].root], rt[m].defg, rt[m].lo, rt[m].hi, rt[m].digit);
					for (m = 0; m < NARef; m++)
						fprintf(stderr, "ref root %d addr %06o gpos %ld body %d off %ld\n",
							ARef[m].root, AuxSet[ARef[m].root], ARef[m].gpos, ARef[m].body, ARef[m].off);
				}
				free(rt);
			}
			if (FSNew && retry < 200) { /* force-named roots: rebuild */
				for (i = 0; i < NSym; i++)
					free(WalkName[i]);
				free(WalkName);
				WalkName = 0;
				free(DeclKind);
				DeclKind = 0;
				free(DeclEmitted);
				DeclEmitted = 0;
				free(TB.s);
				free(TB.ord);
				free(TB.eol);
				memset(&TB, 0, sizeof TB);
				free(DB.s);
				free(DB.ord);
				free(DB.eol);
				memset(&DB, 0, sizeof DB);
				free(BB.s);
				free(BB.ord);
				free(BB.eol);
				memset(&BB, 0, sizeof BB);
				continue;
			}
			/* reset for the real run */
			TB.cur = DB.cur = BB.cur = 0;
			memset(DeclEmitted, 0, NSym ? NSym : 1);
			NextDecl = 0;
			break;
		}
		free(SvWish);
		free(SvAssign);
		free(SvSynth);
		walk_pass(out);
		for (i = 0; i < NSym; i++)
			free(WalkName[i]);
		free(WalkName);
		WalkName = 0;
		free(DeclKind);
		DeclKind = 0;
		free(DeclEmitted);
		DeclEmitted = 0;
		free(TB.s);
		free(TB.ord);
		free(TB.eol);
		memset(&TB, 0, sizeof TB);
		free(DB.s);
		free(DB.ord);
		free(DB.eol);
		memset(&DB, 0, sizeof DB);
		free(BB.s);
		free(BB.ord);
		free(BB.eol);
		memset(&BB, 0, sizeof BB);
	}
	else {
		fprintf(out, ".text\n");
		disasm_text(TBASE, 0, text, out);
		if (data) {
			fprintf(out, "\n.data\n");
			disasm_data(DBASE, text, data, out);
		}
		if (bss) {
			fprintf(out, "\n.bss\n");
			dump_bss(text + data, bss, out);
		}
	}
	free(Mark);
	Mark = 0;
	free(Targ);
	Targ = 0;
}

/* open <stem>.<obj>.dis, de-duplicating repeated basenames */
static FILE *
openpart(char *stem, char *obj, int seq)
{
	static char path[1200];
	char clean[64];
	int i;
	for (i = 0; obj[i] && i < 60; i++)
		clean[i] = (obj[i] == '/') ? '_' : obj[i];
	clean[i] = 0;
	if (seq)
		sprintf(path, "%s.%s.%d.dis", stem, clean, seq);
	else
		sprintf(path, "%s.%s.dis", stem, clean);
	{
		FILE *f = fopen(path, "w");
		if (!f) {
			perror(path);
			exit(1);
		}
		fprintf(stderr, "%s\n", path);
		return f;
	}
}

/* A linked a.out: split text into per-object listings via the N_FN file
 * symbols `ld' left behind; emit the (unsplittable) data/bss once. */
static void
do_aout_split(char *stem, int tostdout)
{
	int text = w16(2), data = w16(4), bss = w16(6), syms = w16(8), flag = w16(14);
	long TBASE = 16, DBASE = 16 + text, reloc = flag ? 0 : (long)(text + data);
	long SYMOFF = DBASE + data + reloc;
	int i, k, fn[512], nfn = 0;
	readsyms(SYMOFF, syms);
	Tbase = TBASE;
	Tsize = text;
	Dbase = DBASE;
	Dsize = data;
	Bsz = bss;
	NAuxSet = 0;
	AuxFrozen = 0;
	RTbase = DBASE + data;
	RDbase = DBASE + data + text;
	HasReloc = !flag;
	/* V6 ld leaves a_flag CLEAR on linked output even though it strips the
	 * relocation (2.79's bin.v6 binaries): trusting the flag would read
	 * symbol-table bytes as relocation.  Believe the GEOMETRY instead. */
	if (HasReloc && SYMOFF + syms > FLEN)
		HasReloc = 0;
	for (i = 0; i < NSym; i++)
		if (BASETYPE(Sym[i].type) == N_FN && nfn < 512)
			fn[nfn++] = i;
	/* sort the file boundaries by text address (insertion sort) */
	for (i = 1; i < nfn; i++) {
		int t = fn[i], j = i;
		while (j > 0 && Sym[fn[j - 1]].value > Sym[t].value) {
			fn[j] = fn[j - 1];
			j--;
		}
		fn[j] = t;
	}

	if (nfn == 0 || tostdout) { /* single listing (raw .o, or a.out with no
				     * N_FN boundaries) -- nothing to split into
				     * separate files, so default to stdout */
		do_object(0, stem, stdout);
		return;
	}
	for (k = 0; k < nfn; k++) {
		int a0 = Sym[fn[k]].value;
		int a1 = (k + 1 < nfn) ? Sym[fn[k + 1]].value : text;
		int dup = 0, j;
		for (j = 0; j < k; j++)
			if (!strcmp(Sym[fn[j]].name, Sym[fn[k]].name))
				dup++;
		{
			FILE *out = openpart(stem, Sym[fn[k]].name, dup);
			banner(out, Sym[fn[k]].name, w16(0), a1 - a0, 0, 0);
			fprintf(out, ".text\n");
			disasm_text(TBASE, a0, a1, out);
			fclose(out);
		}
	}
	/* data + bss cannot be split by object from the a.out alone -> one file */
	if (data || bss) {
		FILE *out = openpart(stem, "DATA", 0);
		banner(out, "shared data/bss (object split not determinable)",
		       w16(0), 0, data, bss);
		if (data) {
			fprintf(out, ".data\n");
			disasm_data(DBASE, text, data, out);
		}
		if (bss) {
			fprintf(out, "\n.bss\n");
			dump_bss(text + data, bss, out);
		}
		fclose(out);
	}
}

/* An archive: disassemble each object member to its own listing. */
static void
do_archive(char *stem, int tostdout)
{
	long off = 2; /* past the 2-byte ARMAG word */
	while (off + 26 <= FLEN) {
		char name[15];
		int i;
		long size, mbase;
		for (i = 0; i < 14; i++)
			name[i] = F[off + i];
		name[14] = 0;
		for (i = 13; i >= 0 && (name[i] == ' ' || name[i] == 0); i--)
			name[i] = 0;
		/* ar_size is a PDP-11 long: HIGH word first, each word little-endian
		 * (was read low-word-first, which turned every native archive's
		 * member size into size<<16 and stopped the scan after one member) */
		size = (unsigned)((w16(off + 22) << 16) | w16(off + 24));
		mbase = off + 26;
		if (name[0] && strcmp(name, "__.SYMDEF") && w16(mbase) >= 0407 && w16(mbase) <= 0411) {
			FILE *out = tostdout ? stdout : openpart(stem, name, 0);
			do_object(mbase, name, out);
			if (!tostdout)
				fclose(out);
		}
		off = mbase + size + (size & 1); /* members are word-aligned */
	}
}

/* ---- driver ------------------------------------------------------------- */
int
main(int argc, char **argv)
{
	char *path, *stem, *p;
	int tostdout = 0;
	FILE *f;
	long n;

	while (argc > 1 && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-p"))
			tostdout = 1;
		else if (!strcmp(argv[1], "-a"))
			Asm = 1; /* reassemblable source */
		else if (!strcmp(argv[1], "-6"))
			V6Sys = 1; /* V5/V6 sysent personality */
		else if (!strcmp(argv[1], "-2")) {
			V6Sys = 1;
			V2Sys = 1;
		} /* 1972 sysent */
		else if (!strcmp(argv[1], "-y"))
			SymNoReloc = 1; /* compat no-op (now default) */
		else if (!strcmp(argv[1], "-J"))
			J11Dec = 1; /* late-hardware decode */
		else if (!strncmp(argv[1], "--sys=", 6)) {
			/* the syscall axis alone: decode conventions for the
			 * era's traps (inline argument words, `..' bias) */
			char *t = argv[1] + 6;
			if (!strcmp(t, "none"))
				;
			else if (!strcmp(t, "v1") || !strcmp(t, "1972")) {
				V6Sys = 1;
				V2Sys = 1;
			}
			else if (!strcmp(t, "v6"))
				V6Sys = 1;
			else if (!strcmp(t, "bsd211") || !strcmp(t, "newbsd") || !strcmp(t, "211"))
				Sys211 = 1;
			else {
				fprintf(stderr, "das: --sys= takes none,v1,v6,bsd211\n");
				return 1;
			}
		}
		else if (!strncmp(argv[1], "--isa=", 6)) {
			char *t = argv[1] + 6;
			if (!strcmp(t, "extended"))
				J11Dec = 1;
			else if (!strcmp(t, "v1") || !strcmp(t, "v2") || !strcmp(t, "v3") || !strcmp(t, "v4") || !strcmp(t, "bsd211") || !strcmp(t, "newbsd") || !strcmp(t, "common") || !strcmp(t, "unix") || !strcmp(t, "1972") || !strcmp(t, "211"))
				;
			else {
				fprintf(stderr, "das: --isa= takes v1,v4,bsd211,extended\n");
				return 1;
			}
		}
		else if (!strncmp(argv[1], "--std=", 6)) {
			/* era dialects, mirroring as: the sysent personalities
			 * and the extended decode are what das varies by era
			 * (object formats are auto-detected from the file) */
			char *t = argv[1] + 6;
			while (*t) {
				char tok[16];
				int n = 0;
				while (*t && *t != ',' && n < 15)
					tok[n++] = *t++;
				tok[n] = 0;
				if (*t == ',')
					t++;
				if (!strcmp(tok, "v1") || !strcmp(tok, "v2") || !strcmp(tok, "v3")) {
					V6Sys = 1;
					V2Sys = 1;
				}
				else if (!strcmp(tok, "v4") || !strcmp(tok, "v5") || !strcmp(tok, "v6"))
					V6Sys = 1;
				else if (!strcmp(tok, "v7") || !strcmp(tok, "bsd"))
					;
				else if (!strcmp(tok, "newbsd"))
					Sys211 = 1;
				else if (!strcmp(tok, "extended"))
					J11Dec = 1;
				else {
					fprintf(stderr, "das: unknown --std token '%s' (v1,v2,v3,v4,v5,v6,v7,bsd,newbsd,extended)\n", tok);
					return 1;
				}
			}
		}
		else if (!strcmp(argv[1], "-s"))
			SymNoReloc = 0; /* content-only: drop the
					 * symtab of a no-reloc image
					 * (the pre--y behavior) */
		else {
			fprintf(stderr, "usage: das [-a] [-p] [-s] [-6] [-2] [-y] file\n");
			return 1;
		}
		argc--;
		argv++;
	}
	if (argc != 2) {
		fprintf(stderr, "usage: das [-a] [-p] [-s] [-6] [-2] [-y] file\n");
		return 1;
	}
	path = argv[1];
	if ((f = fopen(path, "rb")) == NULL) {
		perror(path);
		return 1;
	}
	fseek(f, 0, 2);
	FLEN = ftell(f);
	fseek(f, 0, 0);
	F = malloc(FLEN);
	n = fread(F, 1, FLEN, f);
	fclose(f);
	if (n != FLEN) {
		fprintf(stderr, "%s: short read\n", path);
		return 1;
	}

	/* output stem = input basename without directory */
	stem = (p = strrchr(path, '/')) ? p + 1 : path;

	if (FLEN >= 2 && (unsigned short)w16(0) == (unsigned short)ARMAG) {
		do_archive(stem, tostdout);
	}
	else if (FLEN >= 12 && w16(0) == 0405) {
		do_object(0, stem, stdout); /* V1 12-byte-header executable */
	}
	else if (FLEN >= 16 && (w16(0) == 0407 || w16(0) == 0410 || w16(0) == 0411)) {
		do_aout_split(stem, tostdout); /* splits if N_FN present, else whole */
	}
	else {
		fprintf(stderr, "%s: not a PDP-11 a.out, object, or archive\n", path);
		return 1;
	}
	return 0;
}
