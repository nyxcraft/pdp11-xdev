/*
 * as -- PDP-11 assembler for 2.8BSD, reimplemented in C so it cross-
 * assembles on a modern 64-bit host.
 *
 * Accepts the authentic 2BSD assembler syntax c1 emits and produces classic
 * 2.8BSD a.out objects: 0407 header, one 16-bit relocation word per code
 * word, and 12-byte symbol entries with inline 8-char names -- the format
 * this toolchain's ld and nm read (NOT the newer string-table a.out that GNU
 * binutils pdp11-aout uses).
 *
 * The opcode table (as/optab.h) is the authentic table from as19.s.  The
 * type-class encoding follows the key documented there:
 *   1 abs  6 branch  7 jsr/xor  010 rts  011 sys  013 double-op  015 single
 *   016 .byte  017 .ascii  020 .even  023 .globl  024 register  025/26/27
 *   .text/.data/.bss  030 mul/div(EIS)  031 sob  032 .comm  035 jbr  036 jxxx
 *   040 .word (bare expression).  Span-dependent jumps (035/036) are resolved
 *   to the short branch form when the target is a near text label, matching the
 *   2BSD assembler (see the relaxation loop in main).
 *
 * Usage:  as [-o out.o] [-u] file
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct op {
	char *name;
	int type;
	int opcode;
};

#include "optab.h"
#include "extoptab.h"
int uflag; /* explicit -u seen (overrides --std strictness) */
int jflag; /* --std=extended (compat -j): the full hardware line
	    * (FIS/MED/XFC/CIS, FP11 DEC names) -- mnemonics no
	    * assembler standard ever had.  Opt-in: several names
	    * collide with corpus symbols. */
/* Three ORTHOGONAL era axes (a --std token is a preset over them):
 *   --isa=  which INSTRUCTIONS exist: 0 = the common set (V4..2.10,
 *           identical); 1972 = the original V1-V3 table (no jbr/jxx,
 *           no .ascii; attested 1972, presumed back to 1971); 211
 *           (token bsd211) = -EAE +tab211, completing the J-11
 *           processor set.  `extended' is additive via jflag.
 *   --sys=  SYSCALL-NAME keywords: 0 none (V7+), 1972 = the 34-name
 *           list (quit/intr/cemt/ilgins), 6 = the V4-V6 31-name list
 *           (signal).  Either shadows the WAIT instruction: in every
 *           table that has syscall names, `wait' IS the syscall (7).
 *   --aout= object format: v1fmt = First Edition 12-byte + bit-stream
 *           reloc; newsym = 2.11 string-table; default 16-byte 0407. */
int isa;      /* 0 common, 1972, 211 */
int sysnames; /* 0 none, 1972, 6 */
int v1fmt;    /* --aout=v1: emit the FIRST EDITION object format
	       * (a.out(V), 11/3/71): 12-byte header, V1 symbol
	       * flags, 2-bit relocation bit stream */

/* era-gate a BASE-table keyword: 1972 has no jbr/jxx (type 035/036)
 * and no .ascii, and its `wait' is the SYSCALL (v1systab shadows the
 * instruction); 2.11 dropped the EAE keywords. */
static int
kwskip(struct op *o)
{
	if (isa == 1972) {
		static char *v4new[] = {".ascii", "ldfps", "stfps", "movie", "movei", 0};
		int i;
		if (o->type == 035 || o->type == 036)
			return 1; /* jbr/jxx */
		for (i = 0; v4new[i]; i++)
			if (!strcmp(o->name, v4new[i]))
				return 1;
		/* V4 also added FPS access and the exponent idiom -- the
		 * 1972 table has the FP group WITHOUT these four */
	}
	if (sysnames && !strcmp(o->name, "wait"))
		return 1; /* the syscall
			   * shadows the instruction in
			   * every era that names calls */
	if (isa == 211) {
		static char *eae[] = {"ac", "mq", "sc", "sr", "csw", "mpy", "dvd",
				      "nor", "lsh", "als", "alsc", 0};
		int i;
		for (i = 0; eae[i]; i++)
			if (!strcmp(o->name, eae[i]))
				return 1;
	}
	return 0;
}

/* expression segment / relocation kind */
#define SABS 0
#define STEXT 1
#define SDATA 2
#define SBSS 3
#define SEXT 4

/* on-disk relocation type field (r & 016) and pcrel bit */
#define RABS 000
#define RTEXT 002
#define RDATA 004
#define RBSS 006
#define REXT 010
#define RPCREL 001

/* on-disk symbol n_type */
#define N_UNDF 0
#define N_ABS 01
#define N_TEXT 02
#define N_DATA 03
#define N_BSS 04
#define N_REG 024 /* register-name symbol (`rer = r3') */
#define N_EXT 040

#define NCPS 33 /* buffer size: 32 (2.11 `-n' string-table format) + NUL */
int ncps = 8;	/* active significant length: 8, or 32 under -n */
int newsym = 0; /* -n: 2.11 string-table symbol format (8-byte nlist + strtab) */

struct sym {
	char name[NCPS];
	int seg; /* SABS/STEXT/SDATA/SBSS, or SEXT if undefined */
	int value;
	int flags;
	int kwtype;  /* instruction type carried via `^' (`stst = N^tst'), or 0 */
	int index;   /* assigned at symbol-table output */
	int defpass; /* passno of the last label definition (dup-label check) */
	struct sym *next;
	struct sym *onext; /* insertion-order chain (symbol-table emission order) */
};
struct sym *symhead, *symtail; /* symbols in first-creation order, as 2BSD as emits them */
#define SF_GLOBL 01
#define SF_DEF 02
#define SF_REG 04    /* symbol aliased to a register (`lp = r5') */
#define SF_TMINT 010 /* minted by a `~'-marked MENTION: emit even when \
		      * undefined and opcode-named (V1 bos's `halt') */

static struct op *
oplook(char *name)
{
	struct op *o;
	for (o = optab; o->name; o++)
		if (strcmp(o->name, name) == 0 && !kwskip(o))
			return o;
	if (isa == 211 || jflag)
		for (o = tab211; o->name; o++)
			if (strcmp(o->name, name) == 0)
				return o;
	if (sysnames == 1972)
		for (o = v1systab; o->name; o++)
			if (strcmp(o->name, name) == 0)
				return o;
	if (sysnames == 6)
		for (o = v6systab; o->name; o++)
			if (strcmp(o->name, name) == 0)
				return o;
	if (jflag)
		for (o = extoptab; o->name; o++)
			if (strcmp(o->name, name) == 0)
				return o;
	return 0;
}

/* Does this symbol belong in the .o symbol table?  Shared by the index-assign
 * and emit passes so their order/predicate can never drift.  A symbol is
 * emitted iff it is global, defined, or an undefined external reference -- but
 * NEVER a numeric local label (\001N, resolved in-memory).
 *
 * `^' aliases (kwtype!=0): native 2BSD as emits one iff its NAME is not itself a
 * builtin opcode.  `ldfps = 170100^tst' redefines the builtin ldfps, which stays
 * in the opcode table and is NOT emitted; but `error = 104000^sys' (px's
 * interpreter) names a NON-opcode, so it IS emitted -- as the absolute symbol
 * `104000 a error'.  So exclude a `^' alias only when it shadows a real opcode.
 * Register aliases (`rer = r3', kwtype 0 / SF_REG) are emitted as N_REG. */
static int
emitsym(struct sym *sp)
{
	return ((sp->flags & SF_GLOBL) || (sp->flags & SF_DEF) || sp->seg == SEXT) && sp->name[0] != 1 && !(sp->kwtype && oplook(sp->name))
	       /* an undefined opcode-name (never `='-defined nor .globl'd) is a
		* bare opcode-value reference, not a real symbol -- native as does
		* not emit it (reset in rkuboot before `reset = 0', $br in as26). */
	       && !(oplook(sp->name) && !(sp->flags & (SF_DEF | SF_GLOBL | SF_TMINT)));
}

int termreg;		    /* set by term/expr when the value is a bare register */
struct op *termkw, *exprkw; /* keyword of the last term / of a `^' right operand */
long dotdot;		    /* value of `..', the reloc-base constant boot code sets
			     * (`.. = ENDCORE-512.'); an absolute value used in
			     * expressions (`mov $..,sp'), NOT a symbol and NOT the
			     * location counter -- native as never emits it */

#define NHASH 1023
struct sym *htab[NHASH];

char *infile, *outfile = "a.out";
char *ibuf, *ibufstart, *ibufend;
int errors, pass, lineno;

/* TEXT=0 DATA=1 BSS=2 */
#define NSEG 3
int dot[NSEG];
int curseg; /* 0/1/2 */
unsigned char *segbuf[NSEG], *relbuf[NSEG];
int segcap[NSEG];
int passno; /* unique counter per assembly pass (dup-label check) */

/* span-dependent jumps: jbr/jxxx (035/036) emit a 1-word branch when the target
 * is a near text label, else the long jmp form.  The choice (spanlong[]) is made
 * by relaxation over repeated address passes; spanloc/spantarg/spanseg are the
 * jump word's address, target address and target segment recorded each pass. */
#define MAXSPAN 30000
char spanlong[MAXSPAN];
int spanloc[MAXSPAN], spantarg[MAXSPAN], spanseg[MAXSPAN];
int brdelt;   /* 2.8 as drift corrector: at every label def (ANY segment,
	       * as23.s) brdelt = label's previous value - current dot.
	       * During the deciding pass the previous value is the
	       * forward-long estimate, so brdelt = local layout drift;
	       * a data/bss label (stable dot) zeroes it, exposing the
	       * full upstream drift to following forward branches --
	       * faithfully reproduced, it is what makes some in-range
	       * forward branches assemble long (rogue fight.s prname). */
int deciding; /* in the single as2-style sizing pass: spanrec decides inline */
int spanidx, nspan, relaxing, estimating;

static void
aerror(char *s)
{
	fprintf(stderr, "as: %s:%d: %s\n", infile, lineno, s);
	errors++;
}

static void *
xalloc(int n)
{
	void *p = calloc(1, n);
	if (!p) {
		fprintf(stderr, "as: no memory\n");
		exit(1);
	}
	return p;
}

/* The hash covers the FULL spelling; only NCPS chars are stored.  Genuine as
 * (as14.s rname) does exactly this: `add r3,(sp); swab (sp)' folds EVERY
 * character into the hash while `dec r5; blt' stops the store at 8.  So two
 * names that agree in their first 8 chars but differ beyond land in different
 * chains and stay SEPARATE entries with identical stored names -- V6 iolib's
 * `extern char *IEH3outp, *IEH3outlim' yields two `_IEH3out' symtab slots. */
static unsigned
nhash(char *b)
{
	unsigned h = 0;
	for (; *b; b++)
		h = h * 33 + (unsigned char)*b;
	return h % NHASH;
}

/* find an existing symbol, or NULL -- never creates one (the dispatch must not
 * mint a spurious entry, e.g. for the `..' reloc-base placeholder) */
static struct sym *
find_sym(char *name)
{
	char nb[NCPS];
	int i;
	struct sym *sp;
	unsigned h;
	for (i = 0; i < ncps && name[i]; i++)
		nb[i] = name[i];
	for (; i < NCPS; i++)
		nb[i] = 0;
	h = nhash(name);
	for (sp = htab[h]; sp; sp = sp->next)
		if (memcmp(sp->name, nb, NCPS) == 0)
			return sp;
	return 0;
}

static struct sym *
lookup(char *name)
{
	struct sym *sp = find_sym(name);
	char nb[NCPS];
	int i;
	unsigned h;
	if (sp)
		return sp;
	for (i = 0; i < ncps && name[i]; i++)
		nb[i] = name[i];
	for (; i < NCPS; i++)
		nb[i] = 0;
	h = nhash(name);
	sp = (struct sym *)xalloc(sizeof *sp);
	memcpy(sp->name, nb, NCPS);
	sp->seg = SEXT;
	sp->index = -1;
	sp->next = htab[h];
	htab[h] = sp;
	sp->onext = 0;
	if (symtail)
		symtail->onext = sp;
	else
		symhead = sp;
	symtail = sp;
	return sp;
}

/* `~'-prefixed names are c2's local/sdb symbols (`~~funcname:', `~var=reg').
 * 2BSD as (as14.s rname) consumes one leading `~', marks the symbol "not for
 * hash table", and appends it to symend directly -- so it is NOT deduplicated:
 * each `~var=reg' in a different function is a distinct entry (this is the +10
 * symbol count vs a hashing assembler).  These are define-only (never referenced)
 * debug symbols, stripped from the final binary.  We mirror that with a per-pass
 * occurrence counter (like the numeric-local-label loccnt[]) so the Nth `~'-symbol
 * is the same object across relaxation passes (stable values/brdelt) yet distinct
 * occurrences stay distinct.  Not hashed: find_sym never returns them. */
#define MAXTILDE 8000
struct sym *tildesyms[MAXTILDE];
int ntilde, tildeidx;
int toklocal; /* the identifier just lexed began with a stripped `~' */

static struct sym *
tildesym(char *name)
{
	struct sym *sp;
	char nb[NCPS];
	int i;
	if (tildeidx < ntilde && tildeidx < MAXTILDE) /* ntilde can exceed the array */
		return tildesyms[tildeidx++];
	for (i = 0; i < ncps && name[i]; i++)
		nb[i] = name[i];
	for (; i < NCPS; i++)
		nb[i] = 0;
	sp = (struct sym *)xalloc(sizeof *sp);
	memcpy(sp->name, nb, NCPS);
	sp->seg = SEXT;
	sp->index = -1;
	sp->onext = 0;
	if (symtail)
		symtail->onext = sp;
	else
		symhead = sp;
	symtail = sp;
	if (ntilde < MAXTILDE)
		tildesyms[ntilde] = sp;
	ntilde++;
	tildeidx++;
	return sp;
}

/* ---------------- lexer ---------------- */
enum {
	TEOF = 256,
	TNL,
	TID,
	TNUM,
	TSTR,
	TLSH,
	TRSH
}; /* \< \> shift operators */

char *ip;
int tok;
long tokval;
char tokname[64];
struct op *tokkw;
char tokstr[1024];
int tokslen;

/* token pushback stack (LIFO) */
struct savedtok {
	int tok;
	long val;
	char name[64];
	struct op *kw;
};
struct savedtok pbstk[8];
int pbsp;

static void
pushtok(int tk, long v, char *nm, struct op *kw)
{
	pbstk[pbsp].tok = tk;
	pbstk[pbsp].val = v;
	if (nm)
		strcpy(pbstk[pbsp].name, nm);
	else
		pbstk[pbsp].name[0] = 0;
	pbstk[pbsp].kw = kw;
	pbsp++;
}

/* numeric local labels 0..9: each definition `N:' gets a unique mangled
 * symbol "\1N_<count>"; `Nf' refers to the next definition, `Nb' the
 * previous.  loccnt[N] counts definitions seen so far in the current pass. */
int loccnt[10];

static char *
locname(int dig, int n)
{
	static char b[16];
	sprintf(b, "\1%d_%d", dig, n);
	return b;
}

static int
idchar(int c)
{
	return isalnum(c) || c == '_' || c == '.' || c == '~';
}

/* 2BSD as character escapes (the `schar' table in as15.s): \n \s \t \e \0 \r
 * \a \p; any other escaped char is taken literally (`\\' -> `\', etc.). */
static int
escval(int c)
{
	switch (c) {
	case 'n':
		return 012;
	case 's':
		return 040;
	case 't':
		return 011;
	case 'e':
		return 004;
	case '0':
		return 000;
	case 'r':
		return 015;
	case 'a':
		return 006;
	case 'p':
		return 033;
	default:
		return (unsigned char)c;
	}
}

static int
lex()
{
	int c;
	if (pbsp) {
		pbsp--;
		tok = pbstk[pbsp].tok;
		tokval = pbstk[pbsp].val;
		strcpy(tokname, pbstk[pbsp].name);
		tokkw = pbstk[pbsp].kw;
		return tok;
	}
again:
	/* skip blanks; an embedded NUL is whitespace (c1 emits "mov%c" with a
	 * 0 modifier, i.e. "mov\0", for word ops -- the 2BSD as skipped it).
	 * True end-of-input is the buffer end, tracked by ibufend. */
	while (ip < ibufend && (*ip == ' ' || *ip == '\t' || *ip == 0))
		ip++;
	if (ip >= ibufend)
		return tok = TEOF;
	c = *ip;
	if (c == 0)
		return tok = TEOF;
	if (c == '/') {
		while (*ip && *ip != '\n')
			ip++;
		goto again;
	}
	if (c == '\n') {
		ip++;
		return tok = TNL;
	}
	if (c == ';') {
		ip++;
		return tok = ';';
	}
	if (c == '_' || c == '.' || c == '~' || isalpha(c)) {
		int n = 0;
		struct op *o;
		while (idchar(*ip)) {
			if (n < 63)
				tokname[n++] = *ip;
			ip++;
		}
		tokname[n] = 0;
		tokkw = 0;
		toklocal = 0;
		/* a leading `~' marks a c2 local/sdb symbol: 2BSD as (as14.s) strips one
		 * `~' and does not hash the result (`~~func' -> `~func', `~var' -> `var') */
		if (tokname[0] == '~') {
			int j;
			for (j = 0; tokname[j]; j++)
				tokname[j] = tokname[j + 1];
			toklocal = 1;
		}
		/* last match wins, matching 2BSD as's overwriting symbol table:
		 * mul/div/ash appear first as absolute EAE register addresses and
		 * later as the EIS instruction, which is the one that must win.
		 * A `~'-marked name NEVER matches a keyword: in as14.s the marker
		 * bypasses the symbol-table lookup entirely, and the opcodes live
		 * in that same table (V1 bos carries an undef local named `halt';
		 * `.if ~halt' must mint it, not read the instruction). */
		if (!toklocal) {
			for (o = optab; o->name; o++)
				if (strcmp(o->name, tokname) == 0 && !kwskip(o))
					tokkw = o;
			if ((isa == 211 || jflag) && !tokkw)
				for (o = tab211; o->name; o++)
					if (strcmp(o->name, tokname) == 0)
						tokkw = o;
			if (sysnames == 1972 && !tokkw)
				for (o = v1systab; o->name; o++)
					if (strcmp(o->name, tokname) == 0)
						tokkw = o;
			if (sysnames == 6 && !tokkw)
				for (o = v6systab; o->name; o++)
					if (strcmp(o->name, tokname) == 0)
						tokkw = o;
			if (jflag && !tokkw)
				for (o = extoptab; o->name; o++)
					if (strcmp(o->name, tokname) == 0)
						tokkw = o;
		}
		return tok = TID;
	}
	/* numeric local label reference: <digit>f / <digit>b */
	if (isdigit(c) && (ip[1] == 'f' || ip[1] == 'b') && !idchar(ip[2])) {
		int dig = c - '0', dir = ip[1];
		ip += 2;
		strcpy(tokname, dir == 'f' ? locname(dig, loccnt[dig] + 1)
					   : locname(dig, loccnt[dig]));
		tokkw = 0;
		return tok = TID;
	}
	if (isdigit(c)) {
		long v = 0;
		int base = 8;
		char *q = ip;
		while (isdigit(*q))
			q++;
		if (*q == '.')
			base = 10;
		if (c == '0' && (ip[1] == 'x' || ip[1] == 'X')) {
			base = 16;
			ip += 2;
		}
		/* Always consume the whole digit run (8/9 included -- they occur as
		 * local-label digits and as decimal); accumulate in the base.  The
		 * digit must advance ip even in octal, or `9:' loops forever. */
		if (base == 16) {
			while (isxdigit(*ip)) {
				int d = isdigit(*ip) ? *ip - '0' : tolower(*ip) - 'a' + 10;
				v = v * 16 + d;
				ip++;
			}
		}
		else {
			while (isdigit(*ip)) {
				v = v * base + (*ip - '0');
				ip++;
			}
		}
		if (*ip == '.')
			ip++;
		tokval = v;
		return tok = TNUM;
	}
	if (c == '\'') {
		ip++; /* character constant 'X or '\X */
		if (*ip == '\\') {
			ip++;
			tokval = escval((unsigned char)*ip);
		}
		else
			tokval = (unsigned char)*ip;
		if (*ip)
			ip++;
		return tok = TNUM;
	}
	if (c == '"') {
		int lo, hi;
		ip++; /* double-character constant "xy:
		       * first char low byte, second high byte */
		if (*ip == '\\') {
			ip++;
			lo = escval((unsigned char)*ip);
		}
		else
			lo = (unsigned char)*ip;
		if (*ip)
			ip++;
		if (*ip == '\\') {
			ip++;
			hi = escval((unsigned char)*ip);
		}
		else
			hi = (unsigned char)*ip;
		if (*ip)
			ip++;
		tokval = (lo & 0377) | ((hi & 0377) << 8);
		return tok = TNUM;
	}
	if (c == '<') { /* string (as in .ascii <...>); 2BSD uses < > */
		int term = '>';
		ip++;
		tokslen = 0;
		while (*ip && *ip != term) {
			int ch = (unsigned char)*ip++;
			if (ch == '\\' && *ip) {
				ch = escval((unsigned char)*ip);
				ip++;
			}
			if (tokslen < 1023)
				tokstr[tokslen++] = ch;
		}
		if (*ip)
			ip++;
		return tok = TSTR;
	}
	if (c == '\\' && (ip[1] == '/' || ip[1] == '%')) { /* esctab, constant 1972..2.11:
							    * \/ division (plain / opens a
							    * comment), \% bitwise OR */
		ip += 2;
		return tok = ip[-1] == '/' ? '/' : '|';
	}
	if (c == '\\' && (ip[1] == '<' || ip[1] == '>')) { /* \< left shift, \> right shift */
		int s = ip[1];
		ip += 2;
		return tok = (s == '<') ? TLSH : TRSH;
	}
	if (c == '\\' && ip[1] == '/') {
		ip += 2;
		return tok = '/';
	} /* \/ division ('/' is a comment) */
	ip++;
	return tok = c;
}

static void
unlex()
{
	pushtok(tok, tokval, tokname, tokkw);
}

static int
peek()
{
	int t = lex();
	unlex();
	return t;
}

/* ---------------- expressions ---------------- */
struct sym *exsym; /* set by term/expr when result is external */
int v7flag;	   /* -7: genuine-as default -- no auto-external of
		    * undefineds (our port's default = `as -u', which is
		    * how cc always invokes as) */
int ovasflag;	   /* -V (MENLO_OVLY `ovas'): keep refs to defined
		    * global TEXT symbols external so ld can
		    * substitute the overlay thunk */
static long expr(int *segp);

static long
term(int *segp)
{
	int t = lex();
	long v;
	struct sym *sp;
	exsym = 0;
	*segp = SABS;
	termreg = 0;
	termkw = 0;
	if (t == '-') {
		v = term(segp);
		return -v;
	}
	if (t == '+') {
		return term(segp);
	} /* unary plus (e.g. `+2(r0)') */
	if (t == '~') {
		v = term(segp);
		return ~v;
	}
	if (t == '!') {
		v = term(segp);
		return ~v;
	} /* 2BSD as: unary one's complement */
	/* 2BSD as groups sub-expressions with [], NOT () -- `(' is reserved for
	 * addressing modes (index/deferred, handled in getop; the c1 `disp+(reg)'
	 * index idiom is handled in expr()).  Native as errors on `(2+3)', so we
	 * do too: no `(' case here -- it falls through to "bad expression". */
	if (t == '[') {
		v = expr(segp);
		if (lex() != ']')
			aerror("missing ]");
		return v;
	} /* 2BSD as: [] grouping */
	if (t == TNUM)
		return tokval;
	if (t == TID) {
		termkw = tokkw; /* remember a keyword for `^' type-carry */
		if (strcmp(tokname, ".") == 0) {
			*segp = curseg + 1;
			return dot[curseg];
		}
		if (strcmp(tokname, "..") == 0) {
			*segp = SABS;
			return dotdot;
		} /* reloc base constant */
		/* A user-defined symbol OVERRIDES a builtin opcode of the same name:
		 * 2BSD as keeps one symbol table, so `wait = 7.' (sys.s) replaces the
		 * WAIT instruction and `sys wait' becomes sys 7 -- confirmed against
		 * native as.  So check the user symbol BEFORE the opcode value. */
		if (toklocal) { /* `~name' mention: fresh unhashed entry, never a
				 * keyword and never an existing symbol (as14's marker
				 * bypasses the table lookup) */
			sp = tildesym(tokname);
			sp->flags |= SF_TMINT;
			*segp = SEXT;
			exsym = sp;
			return 0;
		}
		sp = find_sym(tokname);
		if (sp && (sp->flags & SF_DEF)) {
			*segp = sp->seg;
			if (sp->flags & SF_REG)
				termreg = 1;
			/* ovas: carry defined global TEXT symbols so the
			 * word-emit path can externalize them (thunks) */
			if (ovasflag && (sp->flags & SF_GLOBL) && sp->seg == STEXT)
				exsym = sp;
			return sp->value;
		}
		/* register (024) and absolute (01) keywords carry their value */
		if (tokkw && (tokkw->type == 024 || tokkw->type == 01)) {
			if (tokkw->type == 024)
				termreg = 1;
			/* a no-operand opcode (reset/bpt/...) used as a VALUE: native as keeps
			 * opcodes in its symbol table, so a later `reset = 0' fills the SAME
			 * entry (interned at first reference).  Intern one too so the symbol
			 * ORDER matches (rkuboot `$reset+go' precedes `reset = 0').  If never
			 * defined, emitsym() drops it -- an undefined opcode-name is just the
			 * builtin, not a real symbol (as26.s `$br'/`$jmp'). */
			else
				lookup(tokname);
			return tokkw->opcode;
		}
		/* a bare opcode mnemonic used as a value resolves to its opcode: 2BSD
		 * as seeds the symbol table with every instruction, so `$br'/`$jmp+37'
		 * (as26.s) are absolute constants.  find_sym (not lookup) above so an
		 * opcode used purely as a value never mints a phantom undefined-external
		 * entry that ld would then reject. */
		if (tokkw) {
			*segp = SABS;
			return tokkw->opcode;
		}
		sp = lookup(tokname);
		*segp = SEXT;
		exsym = sp;
		return 0;
	}
	aerror("bad expression");
	return 0;
}

static long
expr(int *segp)
{
	int seg, seg2;
	long v, v2;
	struct sym *es;
	int reg;
	struct op *reskw = 0;
	v = term(&seg);
	es = exsym;
	reg = termreg; /* a bare register, until an operator applies */
	for (;;) {
		int t = peek();
		if (t == TID || t == TNUM || t == '[') {
			/* JUXTAPOSITION: two operands with no operator between
			 * them add (`the effect is exactly the same as if a +
			 * had appeared' -- the assembler manual; eoprnd in
			 * as17.s pushes `+' as the pending operator, identical
			 * from 1972 through 2.10) */
			t = '+';
			goto juxta;
		}
		if (t == '+' || t == '-' || t == '*' || t == '/' || t == '&' || t == '|' || t == '%' || t == '^' || t == '!' || t == TLSH || t == TRSH) {
			lex(); /* consume the operator */
		juxta:;
			/* c1 writes the index form `disp(reg)' as `disp+(reg)' (e.g.
			 * `clr 2+(r0)' = 2(r0), the 2nd word of a long).  A `+' right
			 * before `(' is that index separator, NOT addition -- native as
			 * reads it as index mode.  Stop the expression and hand the
			 * `+(reg)' back to getop, which takes (reg) as the index reg. */
			if (t == '+' && peek() == '(') {
				pushtok('+', 0, 0, 0);
				break;
			}
			v2 = term(&seg2);
			reg = 0;
			reskw = 0;
			switch (t) {
			/* the segment checks fire only in pass 2: a forward reference is an
			 * undefined SEXT in pass 1 but may resolve to an absolute symbol
			 * defined later (e.g. boot blocks' `$preset+go', preset/go = ...) */
			case '+':
				v += v2;
				if (seg == SABS) {
					seg = seg2;
					es = exsym;
				}
				else if (seg2 != SABS && pass == 2)
					aerror("non-abs in +");
				break;
			case '-':
				v -= v2;
				if (seg == seg2 && seg != SEXT) {
					seg = SABS;
					es = 0;
				}
				else if (seg2 != SABS && pass == 2)
					aerror("bad -");
				break;
			case '*':
				v *= v2;
				break;
			case '/':
				if (v2)
					v /= v2;
				break;
			case '%':
				if (v2)
					v %= v2;
				break;
			case '&':
				v &= v2;
				break;
			case '|':
				v |= v2;
				break;
			/* ^ = "value of left, type of right": the result VALUE is the left
			 * operand unchanged, and the right operand's keyword is carried out
			 * as the result's type (custom opcode: `stst = 170300^tst' makes
			 * stst a single-op instruction; `error = 104000^sys' in px makes
			 * error a sys-format trap with base 104000).  Do NOT combine the
			 * values -- term() returns an opcode keyword's OPCODE as its value
			 * (sys -> 104400), so the old `v^=v2' XORed 104000^104400 = 0400
			 * and mis-emitted `error 16' as `br'+16 instead of 104016. */
			case '^':
				reskw = termkw;
				seg = seg2;
				break; /* the RESULT takes the
					* type of the RIGHT operand (genuine as:
					* `a ^ b' = value of a, mode of b) */
			case '!':
				v += ~v2;
				break; /* 2BSD as: a!b = a + ~b */
			case TLSH:
				v <<= (v2 & 037);
				break; /* \< left shift */
			case TRSH:
				v = (unsigned long)v >> (v2 & 037);
				break; /* \> right shift (logical) */
			}
		}
		else
			break;
	}
	*segp = seg;
	exsym = es;
	termreg = reg;
	exprkw = reskw;
	return v;
}

/* ---------------- emission ---------------- */
static void
ensure(int seg, int n)
{
	if (dot[seg] + n > segcap[seg]) {
		int nc = (segcap[seg] ? segcap[seg] * 2 : 1024);
		while (dot[seg] + n > nc)
			nc *= 2;
		segbuf[seg] = realloc(segbuf[seg], nc);
		relbuf[seg] = realloc(relbuf[seg], nc);
		memset(segbuf[seg] + segcap[seg], 0, nc - segcap[seg]);
		memset(relbuf[seg] + segcap[seg], 0, nc - segcap[seg]);
		segcap[seg] = nc;
	}
}

/* relocation kind for a value's segment */
static int
relkind(int seg)
{
	switch (seg) {
	case STEXT:
		return RTEXT;
	case SDATA:
		return RDATA;
	case SBSS:
		return RBSS;
	default:
		return RABS;
	}
}

/* Within an object the address space is unified: text@0, data@txtsize,
 * bss@txtsize+datsize (the authentic as's datbase/bssbase, as21.s).  Symbol
 * values and internal references are emitted in this unified space; ld's
 * cdrel/cbrel back out the bias when combining objects.  Set after pass 1. */
int txtsize, datsize;

static int
segbase(int seg)
{
	switch (seg) {
	case SDATA:
		return txtsize;
	case SBSS:
		return txtsize + datsize;
	default:
		return 0;
	}
}

static void
emitword(int w, int seg, struct sym *sym, int pcrel)
{
	int s = curseg;
	int rel;
	if (pass == 2) {
		ensure(s, 2);
		segbuf[s][dot[s]] = w & 0377;
		segbuf[s][dot[s] + 1] = (w >> 8) & 0377;
		if (seg == SEXT && sym && ovasflag && !(sym->flags & SF_GLOBL) && !(sym->flags & SF_DEF))
			/* ovas: an undefined ref that was NOT .globl'd is resolved by the
			 * overlay linker via its n_type-0 symbol (see the symtab path), so
			 * its relocation is plain absolute (pcrel kept), NOT an external
			 * REXT with a symbol index -- matching native ovas. */
			rel = relkind(SABS) | (pcrel ? RPCREL : 0);
		else if (seg == SEXT && sym)
			rel = (sym->index << 4) | REXT | (pcrel ? RPCREL : 0);
		else
			rel = relkind(seg) | (pcrel ? RPCREL : 0);
		relbuf[s][dot[s]] = rel & 0377;
		relbuf[s][dot[s] + 1] = (rel >> 8) & 0377;
	}
	dot[s] += 2;
}

static void
emitbyte(int b)
{
	int s = curseg;
	if (pass == 2) {
		ensure(s, 1);
		segbuf[s][dot[s]] = b & 0377;
		relbuf[s][dot[s]] = 0;
	}
	dot[s]++;
}

/* emit an instruction's extra (offset) word from an operand.  Defined
 * data/bss targets are biased into the unified object address space; the
 * current location (for pc-relative) is biased by the current segment. */
static void
emitextra(int xval, int xseg, struct sym *xsym, int pcrel)
{
	long v;
	if (ovasflag && xsym && (xsym->flags & (SF_GLOBL | SF_DEF)) == (SF_GLOBL | SF_DEF) && xseg == STEXT && !(xsym->flags & SF_REG)) {
		/* ovas: the reference stays external (REXT) even though the
		 * symbol is defined here, so every overlay-function use goes
		 * through ld's thunk substitution. */
		xval -= xsym->value;
		xseg = SEXT;
	}
	v = xval + segbase(xseg);
	/* `..' relocation base: the program's run-time origin (0 for normal code,
	 * which ld relocates; nonzero for boot blocks that run at a fixed address,
	 * e.g. hkuboot's `.. = ENDCORE-512.-SZFLAGS').  A relocatable (text/data/bss)
	 * target carries `..' pre-added into its stored value; the text/data/bss
	 * relocation is still emitted -- native as does the same. */
	if (xseg == STEXT || xseg == SDATA || xseg == SBSS)
		v += dotdot;
	if (pcrel) {
		/* X(pc): stored = target - (run-time address of the next word).  The
		 * instruction RUNS at offset+`..', so the pc term carries `..' too.  For
		 * a relocatable target `..' then cancels; for an ABSOLUTE target (e.g.
		 * `mov ENDCORE-2,r4') it does not -- native as computes exactly this. */
		/* EXTERNAL pcrel is NOT `..'-biased: genuine outw's ext path skips
		 * the dotdot adjustments entirely (1972 printf.o's `jsr pc,bswitch'
		 * stores exactly -(pc+2) despite `..'=40000) */
		v = v - (dot[curseg] + segbase(curseg + 1) + 2 + (xseg == SEXT ? 0 : dotdot));
		emitword(v & 0177777, (xseg == SABS ? SABS : xseg), xsym, 1);
	}
	else {
		emitword(v & 0177777, xseg, xsym, 0);
	}
}

/* ---------------- operand parsing ----------------
 * Fills mode (6-bit), and (if any) an extra word.
 */
struct operand {
	int mode;
	int hasx;
	long xval;
	int xseg;
	struct sym *xsym;
	int pcrel;
};

static int
regof()
{
	if (tok == TID && tokkw && tokkw->type == 024)
		return tokkw->opcode; /* r0..r5/sp/pc */
	if (tok == TID && !tokkw) {   /* a symbol aliased to a register */
		struct sym *sp = lookup(tokname);
		if ((sp->flags & (SF_DEF | SF_REG)) == (SF_DEF | SF_REG))
			return sp->value & 7;
	}
	return -1;
}

static void
getop(struct operand *o)
{
	int t, defer = 0, reg;
	/* the first operand token, saved so peek() below cannot clobber it */
	int s_tok;
	long s_val;
	char s_name[64];
	struct op *s_kw;
	o->mode = 0;
	o->hasx = 0;
	o->xval = 0;
	o->xseg = SABS;
	o->xsym = 0;
	o->pcrel = 0;
	t = lex();
	if (t == '*') {
		defer = 1;
		t = lex();
	}
	if (t == '$') { /* immediate / absolute */
		o->mode = defer ? 037 : 027;
		o->hasx = 1;
		o->xval = expr(&o->xseg);
		o->xsym = exsym;
		return;
	}
	s_tok = t;
	s_val = tokval;
	strcpy(s_name, tokname);
	s_kw = tokkw;
	if (t == '(') {
		t = lex();
		reg = regof();
		if (reg < 0) {
			aerror("bad register");
			reg = 0;
		}
		if (lex() != ')')
			aerror("missing )");
		if (peek() == '+') {
			lex();
			o->mode = (defer ? 3 : 2) * 010 + reg;
		}
		else if (defer) {
			/* *(rn) means M[M[rn]] with NO side effect.  There is no
			 * single-word mode for that; it is mode 7 (index deferred)
			 * with a zero index word -- *0(rn).  Encoding it as mode 3
			 * (@(rn)+) wrongly auto-increments rn (corrupted putc). */
			o->mode = 7 * 010 + reg;
			o->hasx = 1;
			o->xval = 0;
			o->xseg = SABS;
		}
		else
			o->mode = 1 * 010 + reg; /* (rn) = mode 1 = M[rn] */
		return;
	}
	if (t == '-' && peek() == '(') {
		lex();
		t = lex();
		reg = regof();
		if (reg < 0) {
			aerror("bad register");
			reg = 0;
		}
		if (lex() != ')')
			aerror("missing )");
		o->mode = (defer ? 5 : 4) * 010 + reg;
		return;
	}
	{
		int rr = -1;
		if (t == TID && s_kw && s_kw->type == 024)
			rr = s_kw->opcode;    /* r0..r5/sp/pc */
		else if (t == TID && !s_kw) { /* symbol aliased to a register */
			/* `lp = r5' (px's interpreter): lp is a user symbol with SF_REG, not a
			 * register keyword, so s_kw is 0.  Without this, `mov x,lp' took the
			 * expression path and emitted an absolute memory operand (extra offset
			 * word) instead of register-direct -- native as gives mode 0/1. */
			struct sym *sp = find_sym(s_name);
			if (sp && (sp->flags & (SF_DEF | SF_REG)) == (SF_DEF | SF_REG))
				rr = sp->value & 7;
		}
		if (rr >= 0 && peek() != '(') {
			o->mode = (defer ? 1 : 0) * 010 + rr;
			return;
		} /* Rn ; *Rn=(Rn) */
	}
	/* expression: index X(rn) or pc-relative reference.  Push the saved
	 * first token back (NOT unlex, which would re-push whatever peek last
	 * looked at) so expr() sees the real start of the operand. */
	pushtok(s_tok, s_val, s_tok == TID ? s_name : 0, s_kw);
	{
		int seg;
		long v;
		struct sym *sym;
		v = expr(&seg);
		sym = exsym;
		if (peek() == '+')
			lex(); /* c1's `disp+(reg)' index separator (expr() stopped here) */
		if (peek() == '(') {
			lex();
			t = lex();
			reg = regof();
			if (reg < 0) {
				aerror("bad register");
				reg = 0;
			}
			if (lex() != ')')
				aerror("missing )");
			o->mode = (defer ? 7 : 6) * 010 + reg;
			o->hasx = 1;
			o->xval = v;
			o->xseg = seg;
			o->xsym = sym;
			return;
		}
		o->mode = (defer ? 7 : 6) * 010 + 7; /* X(pc) relative */
		o->hasx = 1;
		o->xval = v;
		o->xseg = seg;
		o->xsym = sym;
		o->pcrel = 1;
	}
}

static void
putop(struct operand *o)
{
	if (o->hasx)
		emitextra(o->xval, o->xseg, o->xsym, o->pcrel);
}

/* ---------------- statement assembly ---------------- */
/* Switching segments word-aligns the segment being LEFT: native as pads .text/
 * .data/.bss to even on every switch-out, so interleaved sections stay aligned
 * (e.g. mch.s's odd-length `<...>' string in .data before a `.text').  For a
 * single-section segment this is identical to the end-of-segment rounding, so
 * only interleaved segments differ -- and compiler output never interleaves. */
static void
setseg(int s)
{
	if (s != curseg) {
		if (dot[curseg] & 1)
			emitbyte(0);
		curseg = s;
	}
}

/* size of one operand's extra word (0 or 2) -- known from syntax in pass1 */
/* we just emit; dot advances identically in both passes */

static void
doublop(int base)
{
	struct operand s, d;
	getop(&s);
	if (lex() != ',')
		aerror("missing ,");
	getop(&d);
	emitword(base | ((s.mode & 077) << 6) | (d.mode & 077), SABS, 0, 0);
	putop(&s);
	putop(&d);
}

static void
singlop(int base)
{
	struct operand d;
	getop(&d);
	emitword(base | (d.mode & 077), SABS, 0, 0);
	putop(&d);
}

static void
jsrop(int base)
{
	struct operand r, d;
	getop(&r);
	if (lex() != ',')
		aerror("missing ,");
	getop(&d);
	emitword(base | ((r.mode & 07) << 6) | (d.mode & 077), SABS, 0, 0);
	putop(&d);
}

/* FP11 floating-point operand forms.  A float accumulator (ac0-ac3, written
 * r0-r3) lands in bits 6-7; the other operand is a general addressing mode in
 * bits 0-5 (with its extra word, if any). */
static void
flop14(int base)
{
	struct operand s, d; /* `op fsrc,freg' (addf/cmpf/movof/...) */
	getop(&s);
	if (lex() != ',')
		aerror("missing ,");
	getop(&d);
	emitword(base | ((d.mode & 07) << 6) | (s.mode & 077), SABS, 0, 0);
	putop(&s);
	putop(&d);
}

static void
flop5(int base)
{
	struct operand s, d; /* `op freg,dst' (movfo/movfi/movei) */
	getop(&s);
	if (lex() != ',')
		aerror("missing ,");
	getop(&d);
	emitword(base | ((s.mode & 07) << 6) | (d.mode & 077), SABS, 0, 0);
	putop(&s);
	putop(&d);
}

static void
movfop(int base)
{
	struct operand s, d; /* movf: LDF mem,freg | STF freg,mem */
	getop(&s);
	if (lex() != ',')
		aerror("missing ,");
	getop(&d);
	if ((s.mode & 077) < 4) /* source is a float register -> STF */
		emitword(0174000 | ((s.mode & 07) << 6) | (d.mode & 077), SABS, 0, 0);
	else /* source is memory -> LDF */
		emitword(base | ((d.mode & 07) << 6) | (s.mode & 077), SABS, 0, 0);
	putop(&s);
	putop(&d);
}

static void
rtsop(int base)
{
	struct operand r;
	getop(&r);
	emitword(base | (r.mode & 07), SABS, 0, 0);
}

/* sys/trap: the operand is the trap number encoded in the low 6 bits.  It must
 * be an absolute constant; if it is a relocatable/external symbol, carry that
 * segment+symbol so the word is relocated (native as does -- e.g. the vestigial
 * lfstat.s does `sys lfstat' but sys.s renamed lfstat->qfstat, leaving lfstat an
 * undefined external), AND flag it -- native as reports it as an error too. */
static void
sysop(int base)
{
	int seg;
	long v = expr(&seg);
	struct sym *es = exsym;
	if (pass == 2 && seg != SABS)
		aerror("sys operand not absolute");
	/* the sys/trap CODE is the low 8 bits (0..0377), not 6: lib/jobs stubs do
	 * `sys read+200' (a job-control flag in bit 0200), which a &077 mask would
	 * truncate.  Plain syscalls are <64 so only the +200 forms exposed this. */
	emitword(base | (v & 0377), seg, es, 0);
}

/* emt [code] -- the operand is optional (bare `emt' == `emt 0'); 8-bit code */
static void
emtop(int base)
{
	int seg, t = peek();
	long v = 0;
	if (t != TNL && t != ';' && t != TEOF)
		v = expr(&seg);
	emitword(base | (v & 0377), SABS, 0, 0);
}

static void
branchop(int base)
{
	int seg;
	long v = expr(&seg);
	long off = (v - (dot[curseg] + 2)) / 2;
	if (pass == 2 && (off < -128 || off > 127))
		aerror("branch out of range");
	emitword(base | (off & 0377), SABS, 0, 0);
}

static void
eisop(int base)
{
	struct operand s, d;
	getop(&s);
	if (lex() != ',')
		aerror("missing ,");
	getop(&d);
	/* src first then reg: opcode | reg<<6 | src */
	emitword(base | ((d.mode & 07) << 6) | (s.mode & 077), SABS, 0, 0);
	putop(&s);
}

static void
sobop(int base)
{
	struct operand r;
	int seg;
	long v;
	getop(&r);
	if (lex() != ',')
		aerror("missing ,");
	v = expr(&seg);
	{
		long off = (dot[curseg] + 2 - v) / 2;
		emitword(base | ((r.mode & 07) << 6) | (off & 077), SABS, 0, 0);
	}
}

/* record this span item's geometry; a span is short-eligible only when its
 * operand is a pc-relative reference (the form a branch can take) to a text
 * label, and the code itself is in text */
static int
spanrec(struct operand *d)
{
	int sidx = spanidx++, shortok = (d->mode == 067 && d->pcrel && curseg == 0);
	if (sidx < MAXSPAN) {
		spanloc[sidx] = dot[curseg];
		spantarg[sidx] = d->xval;
		spanseg[sidx] = shortok ? d->xseg : SABS;
		if (!shortok)
			spanlong[sidx] = 1;
		else if (estimating) {
			/* as16.s pass-1 (opl35/opl36): a forward target is still undefined
			 * when the branch is reached (exsym set) -> long; a backward target
			 * is short iff its byte displacement is in [-254,0) (bge/cmp $-376). */
			if (d->xsym)
				spanlong[sidx] = 1;
			else {
				int disp = d->xval - dot[curseg];
				spanlong[sidx] = (disp >= 0 || disp < -254);
			}
		}
		else if (deciding) {
			/* as26.s setbr: r0 = target - dot; forward (r0>0) gets the
			 * brdelt drift correction; then `betwen -254. 256.' picks short.
			 * betwen (as22.s) is INCLUSIVE of both bounds, so short iff
			 * -254 <= r0 <= 256 -- a br reaches target-dot == 256 exactly
			 * (offset +127 words = dot+2+254).  (Was `> 255', an off-by-one
			 * that over-promoted an exactly-256 forward branch to jmp, e.g.
			 * /bin/nm's L62; verified against native as under apsim -- all 28
			 * rogue modules + nm assemble byte-identically with `> 256'.)
			 * A forward target still carries its estimate-layout value
			 * here (its label hasn't been reached yet this pass). */
			int r0 = d->xval - dot[curseg];
			if (r0 > 0)
				r0 -= brdelt;
			spanlong[sidx] = (r0 < -254 || r0 > 256);
			if (getenv("AS_SPAN_DEBUG"))
				fprintf(stderr, "decide %d loc=%o targ=%lo brdelt=%d r0=%d %s\n",
					sidx, dot[curseg], d->xval, brdelt, r0, spanlong[sidx] ? "LONG" : "short");
		}
	}
	return sidx;
}

/* a short branch's 8-bit displacement; flagged if it ever falls out of range
 * in the emit pass (the relaxation should have grown it to long first) */
static int
brdisp(int targ)
{
	int off = (targ - (dot[curseg] + 2)) / 2;
	if (pass == 2 && (off < -128 || off > 127))
		aerror("span branch out of range");
	return off & 0377;
}

/* The long (out-of-range) form of a span branch is an absolute jmp *$target
 * (mode 037), not jmp target(pc) (067): the short form is a PC-relative br, the
 * long form an absolute jmp -- matching authentic 2BSD as.  (rogue's encrypt /
 * _doprnt carry these where a jgt/jbr fell out of short-branch range.) */
static void
emit_longjmp(struct operand *d)
{
	emitword(0000100 | 037, SABS, 0, 0);
	emitextra(d->xval, d->xseg, d->xsym, 0);
}

/* jbr addr -> br addr (1 word) when near, else jmp addr */
static void
jbrop()
{
	struct operand d;
	int sidx;
	getop(&d);
	sidx = spanrec(&d);
	if (sidx < MAXSPAN && !spanlong[sidx])
		emitword(000400 | brdisp(d.xval), SABS, 0, 0); /* br */
	else
		emit_longjmp(&d); /* jmp */
}

/* jxxx addr -> b<cond> addr (1 word) when near, else b<not-cond> .+6 ; jmp addr */
static void
jxxxop(int brbase)
{
	struct operand d;
	int sidx, comp = brbase ^ 0400;
	getop(&d);
	sidx = spanrec(&d);
	if (sidx < MAXSPAN && !spanlong[sidx])
		emitword(brbase | brdisp(d.xval), SABS, 0, 0); /* b<cond> */
	else {
		emitword(comp | 2, SABS, 0, 0); /* b<not-cond> .+6 */
		emit_longjmp(&d);
	} /* jmp addr */
}

static void
dobyte()
{
	int seg;
	for (;;) {
		if (peek() == TSTR) {
			lex();
			int i;
			for (i = 0; i < tokslen; i++)
				emitbyte(tokstr[i]);
		}
		else {
			long v = expr(&seg);
			emitbyte(v & 0377);
		}
		if (peek() == ',')
			lex();
		else
			break;
	}
}

static void
doascii()
{
	if (lex() == TSTR) {
		int i;
		for (i = 0; i < tokslen; i++)
			emitbyte(tokstr[i]);
	}
	else
		aerror("bad .ascii");
}

static void
doglobl()
{
	for (;;) {
		if (peek() != TID)
			return; /* bare .globl is a no-op separator */
		/* `.globl ~name' mints a FRESH UNHASHED global (the `~' marker
		 * bypasses the table): an undefined external whose name is an
		 * OPCODE (m11's `div'/`mul') must not shadow the instruction */
		lex();
		(toklocal ? tildesym(tokname) : lookup(tokname))->flags |= SF_GLOBL;
		if (peek() == ',')
			lex();
		else
			break;
	}
}

static void
docomm()
{
	struct sym *sp;
	int seg;
	long sz;
	if (lex() != TID) {
		aerror(".comm name");
		return;
	}
	sp = lookup(tokname);
	if (lex() != ',')
		aerror(".comm ,");
	sz = expr(&seg);
	sp->flags |= SF_GLOBL;
	if (!(sp->flags & SF_DEF)) {
		sp->value = sz;
	}
}

static void
even()
{
	if (dot[curseg] & 1) {
		emitbyte(0);
	}
}

static void
doword(long v, int seg, struct sym *sym)
{
	if (ovasflag && sym && (sym->flags & (SF_GLOBL | SF_DEF)) == (SF_GLOBL | SF_DEF) && seg == STEXT && !(sym->flags & SF_REG)) {
		/* ovas: data words naming global text symbols (e.g. function-
		 * pointer tables) also stay external so ld thunks them */
		v -= sym->value;
		seg = SEXT;
	}
	/* `..' origin: a data word naming a relocatable symbol carries the run-time
	 * origin pre-added, same as an instruction operand (emitextra) -- boot-block
	 * address tables (hkuboot's `defnm', `end') need this; 0 for normal code. */
	if (seg == STEXT || seg == SDATA || seg == SBSS)
		v += dotdot;
	emitword((v + segbase(seg)) & 0177777, seg, sym, 0);
}

int ifdepth, ifoff; /* .if nesting depth; depth at which assembly is switched off (0=on) */

static void
assemble()
{
	int t;
	ifdepth = 0;
	ifoff = 0;
	for (;;) {
		t = lex();
		if (t == TEOF)
			break;
		if (t == TNL) {
			lineno++;
			continue;
		}
		if (t == ';')
			continue;
		/* conditional assembly: inside a false .if branch, consume each statement
		 * (still tracking nested .if/.endif) until the matching .endif */
		if (ifoff && !(t == TID && tokkw && (tokkw->type == 021 || tokkw->type == 022))) {
			/* native as's lexer interns every identifier even inside a skipped
			 * .if branch, so a symbol first referenced there (then defined/used
			 * later) takes its symbol-table ORDER from that reference -- e.g.
			 * rluboot's `jsr pc,putc' under a false `.if prompt' precedes putc's
			 * definition.  Intern non-keyword identifiers here (emitting nothing);
			 * they end up defined/undefined exactly as native, just ordered right. */
			if (t == TID && !tokkw && !toklocal)
				lookup(tokname);
			while (peek() != TNL && peek() != TEOF) {
				t = lex();
				if (t == TID && !tokkw && !toklocal)
					lookup(tokname);
			}
			continue;
		}
		if (t == TSTR) {
			int i;
			for (i = 0; i < tokslen; i++)
				emitbyte(tokstr[i]);
			continue;
		} /* bare string = .ascii */
		if (t == TID) {
			char name[64];
			struct op *kw, ckw;
			int t2;
			int nmloc;
			strcpy(name, tokname);
			kw = tokkw;
			nmloc = toklocal;
			t2 = lex(); /* the token after the identifier */
			/* label?  name:   */
			if (t2 == ':') {
				if (strcmp(name, ".") != 0) {
					/* a `~'-local (here a `~~funcname:' sdb label) is non-hashed:
					 * a fresh entry per occurrence, never deduplicated (as14.s) */
					struct sym *sp = nmloc ? tildesym(name) : lookup(name);
					/* a genuine dup is two definitions in the SAME pass; the
					 * relaxation re-defines every label each pass (not an error),
					 * so the dup test keys on passno, not SF_DEF.  `~'-locals are
					 * fresh per occurrence, so they never dup-collide -- skip. */
					if (sp->defpass == passno && !nmloc)
						aerror("redefined symbol");
					sp->defpass = passno;
					sp->flags |= SF_DEF;
					sp->seg = curseg + 1;
					/* brdelt (2.8/2.9 as drift corrector): text/data labels
					 * use est-value minus dot.  A BSS label's estimate reads
					 * as ZERO in the native's brdelt calc (verified against
					 * the real 2.9 as run under apsim: brdelt = -dot[bss]
					 * after any bss dot-advance; this is what makes forward
					 * branches after a function's `static' block go LONG in
					 * deep files -- rogue io.c doadd/status). */
					brdelt = (deciding && curseg == 2) ? -dot[curseg]
									   : sp->value - dot[curseg];
					sp->value = dot[curseg];
				}
				continue;
			}
			/* assignment?  name = expr  */
			if (t2 == '=') {
				int seg;
				long v;
				struct sym *sp = 0;
				/* intern the LHS symbol BEFORE evaluating the RHS: native as
				 * reads the target name (creating its symbol) first, so when the
				 * RHS mints a new symbol (`~~MAIN__ = _MAIN__') the LHS precedes
				 * it in the object symbol table.  `.'/`..' are the location
				 * counter / reloc base, not symbols -- skip them. */
				if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
					sp = nmloc ? tildesym(name) : lookup(name);
				v = expr(&seg);
				if (strcmp(name, "..") == 0) {
					dotdot = v;
					continue;
				} /* reloc base, not a symbol */
				if (strcmp(name, ".") == 0) {
					/* set the location counter.  `.=.+N' reserves N bytes
					 * (e.g. the uninitialised tail of a partly-initialised
					 * array); emit zero fill so following data isn't placed
					 * on top of the reserved space. */
					while (dot[curseg] < v)
						emitbyte(0);
					if (dot[curseg] > v)
						dot[curseg] = v; /* backward (rare) */
				}
				else {
					sp->flags |= SF_DEF;
					sp->seg = seg;
					sp->value = v;
					if (termreg)
						sp->flags |= SF_REG;
					else
						sp->flags &= ~SF_REG;		/* `lp = r5' */
					sp->kwtype = exprkw ? exprkw->type : 0; /* `stst = N^tst' */
				}
				continue;
			}
			unlex();				 /* push t2 back: it starts the operands/expression */
			if (!kw) {				 /* a symbol defined as a custom instruction (`stst = N^tst') */
				struct sym *sp = find_sym(name); /* must not create one (e.g. `..') */
				if (sp && sp->kwtype) {
					ckw.name = sp->name;
					ckw.type = sp->kwtype;
					ckw.opcode = sp->value;
					kw = &ckw;
				}
			}
			/* a no-operand instruction (type 01) is just an opcode word; when one
			 * is followed by a binary operator it is a bare expression, not the
			 * instruction -- `sec|sev|sez|sen' ORs the four cc-set opcodes into a
			 * single word (set C,V,Z,N).  Fall through to the expression path. */
			if (kw && kw->type == 01) {
				int p = peek();
				if (p == '|' || p == '+' || p == '-' || p == '*' || p == '/' || p == '&' || p == '^' || p == '%' || p == '!' || p == TLSH || p == TRSH) {
					pushtok(TID, 0, name, kw);
					{
						int seg;
						long v = expr(&seg);
						doword(v, seg, exsym);
					}
					continue;
				}
			}
			/* a register name (type 024) as a statement is an expression, not an
			 * instruction: a register is a symbol with value 0..7, so a bare `r0'
			 * emits `.word 0'.  V6's Fortran compiler passes register numbers this
			 * way -- `jsr r5,code; <fmt>; r0; r3' (code reads the inline args). */
			if (kw && kw->type == 024) {
				pushtok(TID, 0, name, kw);
				{
					int seg;
					long v = expr(&seg);
					doword(v, seg, exsym);
				}
				continue;
			}
			/* a name that is ALSO a .globl'd or defined symbol shadows a
			 * same-named opcode: as a statement it is a bare expression (a data
			 * word of the symbol), not the instruction.  The kernel's `.globl
			 * emt' then l.s's trap vector `emt; br7+5.' emits the emt HANDLER
			 * address (external), not the emt instruction.  (Re-lex with kw=0 so
			 * the expression path resolves the symbol, not the opcode.) */
			if (kw) {
				struct sym *us = find_sym(name);
				if (us && (us->flags & (SF_DEF | SF_GLOBL)) && !us->kwtype) {
					/* NOT a `^' instruction-alias (`ldfps = 170100^tst', which
					 * IS still an instruction) -- those keep dispatching below. */
					pushtok(TID, 0, name, 0);
					{
						int seg;
						long v = expr(&seg);
						doword(v, seg, exsym);
					}
					continue;
				}
			}
			if (kw) {
				switch (kw->type) {
				case 01: /* absolute no-operand insn: setd/clc/sec/cfcc... */
					emitword(kw->opcode, SABS, 0, 0);
					break;
				case 013:
					doublop(kw->opcode);
					break;
				case 015:
					singlop(kw->opcode);
					break;
				case 05:
					flop5(kw->opcode);
					break; /* movfo freg,dst */
				case 012:
					movfop(kw->opcode);
					break; /* movf (ld/st) */
				case 014:
					flop14(kw->opcode);
					break; /* addf fsrc,freg */
				case 07:
					if (strcmp(name, "xor") == 0) { /* xor R,dst : R (1st operand) in
									 * bits 6-8, dst (2nd, may have an index word) in bits 0-5 */
						struct operand s, d;
						getop(&s);
						if (lex() != ',')
							aerror(",");
						getop(&d);
						emitword(kw->opcode | ((s.mode & 07) << 6) | (d.mode & 077), SABS, 0, 0);
						putop(&d);
					}
					else
						jsrop(kw->opcode);
					break;
				case 010:
					rtsop(kw->opcode);
					break;
				case 011:
					sysop(kw->opcode);
					break;
				case 041:
					emtop(kw->opcode);
					break;
				case 06:
					branchop(kw->opcode);
					break;
				case 030:
					eisop(kw->opcode);
					break;
				case 031:
					sobop(kw->opcode);
					break;
				case 035:
					jbrop();
					break;
				case 036:
					jxxxop(kw->opcode);
					break;
				case 016:
					dobyte();
					break;
				case 017:
					doascii();
					break;
				case 020:
					even();
					break;
				case 021: /* .if expr : assemble the body only when expr != 0 */
					ifdepth++;
					if (ifoff == 0) {
						int seg;
						long v = expr(&seg);
						if (v == 0)
							ifoff = ifdepth;
					}
					else
						while (peek() != TNL && peek() != TEOF && peek() != ';')
							lex();
					break;
				case 022: /* .endif : close the innermost .if */
					if (ifoff && ifoff == ifdepth)
						ifoff = 0;
					if (ifdepth > 0)
						ifdepth--;
					break;
				case 023:
					doglobl();
					break;
				case 025:
					setseg(0);
					break;
				case 026:
					setseg(1);
					break;
				case 027:
					setseg(2);
					break;
				case 032:
					docomm();
					break;
				case 024:
					aerror("register used as instruction");
					break;
				default:
					aerror("unsupported directive");
					break;
				}
				continue;
			}
			/* bare identifier expression -> emit a word.  t2 is already
			 * pushed back; push `name' in front of it so expr() sees the
			 * identifier first. */
			pushtok(TID, 0, name, kw);
			{
				int seg;
				long v = expr(&seg);
				doword(v, seg, exsym);
			}
			continue;
		}
		if (t == TNUM) {
			long saved = tokval; /* peek() below clobbers the token state */
			/* numeric local label definition: `N:' (digit 0..9 then colon) */
			if (saved >= 0 && saved <= 9 && peek() == ':') {
				struct sym *sp;
				lex(); /* consume ':' */
				sp = lookup(locname(saved, ++loccnt[saved]));
				sp->flags |= SF_DEF;
				sp->seg = curseg + 1;
				/* brdelt (2.8/2.9 as drift corrector): text/data labels
				 * use est-value minus dot.  A BSS label's estimate reads
				 * as ZERO in the native's brdelt calc (verified against
				 * the real 2.9 as run under apsim: brdelt = -dot[bss]
				 * after any bss dot-advance; this is what makes forward
				 * branches after a function's `static' block go LONG in
				 * deep files -- rogue io.c doadd/status). */
				brdelt = (deciding && curseg == 2) ? -dot[curseg]
								   : sp->value - dot[curseg];
				sp->value = dot[curseg];
				continue;
			}
			/* bare numeric expression -> word(s).  peek() (if it ran) left
			 * the following token on the pushback stack; push the number in
			 * front of it so expr() sees the number first. */
			pushtok(TNUM, saved, 0, 0);
			{
				int seg;
				long v = expr(&seg);
				doword(v, seg, exsym);
			}
			continue;
		}
		/* bare expression statement -> word(s) */
		unlex();
		{
			int seg;
			long v = expr(&seg);
			doword(v, seg, exsym);
		}
	}
}

/* ---------------- output ---------------- */
static void
putw_(FILE *f, int w)
{
	putc(w & 0377, f);
	putc((w >> 8) & 0377, f);
}

static void
writeout()
{
	FILE *f;
	struct sym *sp;
	int i, nsym = 0, ssize;
	/* segments are even-sized in the file (data must start at txtsize); the
	 * pad bytes are already zero (segbuf/relbuf are zeroed on growth) */
	int tsize = (dot[0] + 1) & ~1, dsize = (dot[1] + 1) & ~1, bsize = (dot[2] + 1) & ~1;
	ensure(0, 2);
	ensure(1, 2);
	/* assign symbol indices: globals + defined locals (skip undefined-but-unreferenced).
	 * Walk the insertion-order chain, not the hash buckets: 2BSD as emits its
	 * symbol table in first-creation order, and ld assigns common/bss addresses
	 * in that order, so the emission order is load-bearing, not cosmetic. */
	int idx = 0;
	for (sp = symhead; sp; sp = sp->onext) {
		if (emitsym(sp)) {
			sp->index = idx++;
			nsym++;
		}
	}
	ssize = nsym * (newsym ? 8 : 12);
	if (!(f = fopen(outfile, "w"))) {
		perror(outfile);
		exit(1);
	}
	if (v1fmt) {
		/* FIRST EDITION object format (a.out(V), 11/3/71): 6-word
		 * header [br .+14 | a_text incl header | symtab size |
		 * relocation-bits size | data area (the era's bss) | 0],
		 * then text (data merged: addresses are already contiguous),
		 * symbol table with V1 FLAGS (00 undef / 01 abs / 02 register
		 * / 03 relocatable, |40 global), then the 2-bit relocation
		 * stream MSB-first in 16-bit words: 00 absolute, 01
		 * relocatable, 10 pc-rel external, 1101 absolute external
		 * (1100/1110 + 16-bit extension carry an addend).  External
		 * reference words hold the SYMBOL TABLE OFFSET (12*index). */
		int nw = (tsize + dsize) / 2, w, bit = 0;
		unsigned char *body = malloc(tsize + dsize ? tsize + dsize : 1);
		/* worst case is 20 bits/word (4-bit code + 16-bit addend extension
		 * for an external-with-addend), i.e. ~2.5 bytes/word; the old
		 * nw/2+8 assumed ~2 bits/word and overran on addend-heavy objects.
		 * fwrite below uses the computed rsz, so oversizing is harmless. */
		unsigned char *stream = calloc((size_t)nw * 3 + 16, 1);
		memcpy(body, segbuf[0], tsize);
		memcpy(body + tsize, segbuf[1], dsize);
		for (w = 0; w < nw; w++) {
			unsigned char *rb = (2 * w < tsize) ? relbuf[0] + 2 * w : relbuf[1] + 2 * w - tsize;
			int r = rb[0] | (rb[1] << 8), code = -1, ext = -1;
			if ((r & 016) == 0)
				code = 0; /* absolute (incl abs-pcrel) */
			else if ((r & 016) != REXT)
				code = (r & 1) ? 0 : 1; /* internal: pcrel is
							 * position-independent */
			else {				/* external: word := symtab offset, addend -> extension */
				int idx = r >> 4, ad = body[2 * w] | (body[2 * w + 1] << 8);
				body[2 * w] = (12 * idx) & 0xff;
				body[2 * w + 1] = (12 * idx) >> 8;
				if (r & 1)
					code = ad ? 014 : 2; /* 1100 / 10 */
				else
					code = ad ? 016 : 015; /* 1110 / 1101 */
				if (ad)
					ext = ad;
			}
			{
				int nb = (code < 2) ? 2 : (code == 2) ? 2
								      : 4,
				    k;
				int bits = (code < 2) ? code : (code == 2) ? 2
									   : code;
				for (k = nb - 1; k >= 0; k--) {
					int bv = (bits >> k) & 1;
					if (bv)
						stream[(bit / 16) * 2 + 1 - (bit % 16) / 8] |= 0x80 >> ((bit % 8));
					bit++;
				}
				if (ext >= 0)
					for (k = 15; k >= 0; k--) {
						if ((ext >> k) & 1)
							stream[(bit / 16) * 2 + 1 - (bit % 16) / 8] |= 0x80 >> ((bit % 8));
						bit++;
					}
			}
		}
		{
			int rsz = ((bit + 15) / 16) * 2;
			ssize = nsym * 12;
			putw_(f, 0405);
			putw_(f, 12 + tsize + dsize);
			putw_(f, ssize);
			putw_(f, rsz);
			putw_(f, bsize);
			putw_(f, 0);
			fwrite(body, 1, tsize + dsize, f);
			for (sp = symhead; sp; sp = sp->onext) {
				int nt, v1t;
				if (!emitsym(sp))
					continue;
				if (sp->flags & SF_REG)
					nt = N_REG;
				else if (sp->flags & SF_DEF) {
					switch (sp->seg) {
					case STEXT:
						nt = N_TEXT;
						break;
					case SDATA:
						nt = N_DATA;
						break;
					case SBSS:
						nt = N_BSS;
						break;
					default:
						nt = N_ABS;
					}
				}
				else
					nt = N_UNDF;
				v1t = nt == N_UNDF ? 0 : nt == N_ABS ? 1
						 : nt == N_REG	     ? 2
								     : 3;
				if ((sp->flags & SF_GLOBL) || (!(sp->flags & SF_DEF) && !v7flag))
					v1t |= 040;
				fwrite(sp->name, 1, 8, f);
				putc(v1t, f);
				putc(0, f);
				putw_(f, (sp->value + segbase(sp->seg)) & 0177777);
			}
			fwrite(stream, 1, rsz, f);
		}
		fclose(f);
		free(body);
		free(stream);
		return;
	}
	/* header (8 words): magic, tsize, dsize, bsize, ssize, entry, unused, relflg */
	putw_(f, 0407);
	putw_(f, tsize);
	putw_(f, dsize);
	putw_(f, bsize);
	putw_(f, ssize);
	putw_(f, 0);
	putw_(f, 0);
	putw_(f, 0); /* relflg 0 => reloc present */
	fwrite(segbuf[0], 1, tsize, f);
	fwrite(segbuf[1], 1, dsize, f);
	fwrite(relbuf[0], 1, tsize, f);
	fwrite(relbuf[1], 1, dsize, f);
	/* symbols: name[8], n_type (16-bit, 2.9BSD), value.  We write the type
	 * low byte then a 0 high byte -- byte-identical to 2.8's n_type+n_ovly=0. */
	{
		long strx = 4;
		for (sp = symhead; sp; sp = sp->onext) {
			int nt;
			/* numeric local labels (\001N_M) resolve in-memory like 2BSD's
			 * curfb table -- they are never written to the .o symbol table */
			if (!emitsym(sp))
				continue;
			if (sp->kwtype)
				nt = sp->kwtype; /* emitted `^' alias of a non-opcode: n_type is the
						  * carried opcode type (`error=104000^sys' -> 011), not N_ABS */
			else if (sp->flags & SF_REG)
				nt = N_REG; /* `rer = r3' -> register name (024), as 2BSD as emits */
			else if (sp->flags & SF_DEF) {
				switch (sp->seg) {
				case STEXT:
					nt = N_TEXT;
					break;
				case SDATA:
					nt = N_DATA;
					break;
				case SBSS:
					nt = N_BSS;
					break;
				default:
					nt = N_ABS;
				}
			}
			else
				nt = N_UNDF;
			/* An undefined symbol that is referenced is an external reference, so
			 * mark it external even without an explicit .globl -- EXCEPT in overlay
			 * (ovas, -V) mode: there an undefined ref that was NOT .globl'd stays
			 * N_UNDF (n_type 0) for the overlay linker to resolve by name, so a
			 * `jsr _f' to a non-declared _f is 0, not N_EXT.  (An explicit .globl or
			 * .comm keeps N_EXT.)  Native 2.9 ovas does this; it's why compiler
			 * output -- which never .globl's the functions it calls -- differs from
			 * hand-written overlay .s that .globl their externals. */
			if ((sp->flags & SF_GLOBL) || (!(sp->flags & SF_DEF) && !ovasflag && !v7flag))
				nt |= N_EXT;
			if (newsym) {
				/* 2.11 `Newsym' entry: off_t strx (PDP-11 long, HIGH word
				 * first), type word, value word; the strings follow the
				 * table, offsets sequential from 4 */
				putw_(f, (strx >> 16) & 0xffff);
				putw_(f, strx & 0xffff);
				strx += strlen(sp->name) + 1;
			}
			else
				fwrite(sp->name, 1, 8, f);
			putc(nt, f);
			putc(0, f);
			/* emit the value in the unified object address space (data biased
			 * by txtsize, bss by txtsize+datsize), like the authentic as */
			putw_(f, (sp->value + segbase(sp->seg)) & 0177777);
		}
		if (newsym) {
			/* the string table: total length (PDP-11 long, includes itself) */
			long tl = strx;
			putw_(f, (tl >> 16) & 0xffff);
			putw_(f, tl & 0xffff);
			for (sp = symhead; sp; sp = sp->onext) {
				if (!emitsym(sp))
					continue;
				fwrite(sp->name, 1, strlen(sp->name) + 1, f);
			}
		}
	}
	fclose(f);
}

int
main(int argc, char **argv)
{
	int i;
	long flen;
	FILE *f;
	/* collect input files: as concatenates several (e.g. the syscall stubs
	 * are assembled as `as -o x.o /usr/include/sys.s x.s`) */
	{
		char *files[64];
		long flens[64];
		int nf = 0;
		long total = 0;
		char *p;
		for (i = 1; i < argc; i++) {
			if (argv[i][0] == '-') {
				if (argv[i][1] == '-' && !strncmp(argv[i], "--isa=", 6)) {
					/* rungs named like --aout: v1 = the original
					 * table (attested by the 1972 source; the
					 * lost 1971 assembler is presumed identical
					 * -- the 1971 manual matches it point for
					 * point), v4 = the set born in V4 and
					 * unchanged through 2.10, bsd211 = 2.11's
					 * table = the complete J-11 processor set
					 * (every tab211 addition is a DCJ11
					 * instruction; FIS/CIS/MED/XFC, which the
					 * J-11 lacks, stay in extended), extended =
					 * the full hardware line */
					char *t = argv[i] + 6;
					if (!strcmp(t, "v4") || !strcmp(t, "common") || !strcmp(t, "unix"))
						isa = 0;
					else if (!strcmp(t, "v1") || !strcmp(t, "v2") || !strcmp(t, "v3") || !strcmp(t, "1972"))
						isa = 1972;
					else if (!strcmp(t, "bsd211") || !strcmp(t, "newbsd") || !strcmp(t, "211"))
						isa = 211;
					else if (!strcmp(t, "extended"))
						jflag = 1;
					else {
						fprintf(stderr, "as: --isa= takes v1,v4,bsd211,extended\n");
						return 1;
					}
				}
				else if (argv[i][1] == '-' && !strncmp(argv[i], "--sys=", 6)) {
					char *t = argv[i] + 6;
					if (!strcmp(t, "none"))
						sysnames = 0;
					else if (!strcmp(t, "v1") || !strcmp(t, "1972"))
						sysnames = 1972;
					else if (!strcmp(t, "v6"))
						sysnames = 6;
					else {
						fprintf(stderr, "as: --sys= takes none,v1,v6\n");
						return 1;
					}
				}
				else if (argv[i][1] == '-' && !strncmp(argv[i], "--aout=", 7)) {
					/* three formats the family ever had:
					 *   v1   First Edition 12-byte (magic 0405)
					 *   v2   the 16-byte 0407 of V2 through 2.10
					 *   v2+  the same 0407 with 2.11's string-table
					 *        symbols (32-char names) -- no magic of
					 *        its own, hence the `+' */
					char *t = argv[i] + 7;
					if (!strcmp(t, "v1") || !strcmp(t, "405"))
						v1fmt = 1;
					else if (!strcmp(t, "v2") || !strcmp(t, "407")) {
						v1fmt = 0;
						newsym = 0;
					}
					else if (!strcmp(t, "v2+") || !strcmp(t, "newsym") || !strcmp(t, "bsd211")) {
						newsym = 1;
						ncps = 32;
					}
					else {
						fprintf(stderr, "as: --aout= takes v1,v2,v2+\n");
						return 1;
					}
				}
				else if (argv[i][1] == '-' && !strncmp(argv[i], "--std=", 6)) {
					/* era standards, comma-composable.
					 * Every historical as was STRICT when
					 * flagless (undefined symbols stay
					 * local); auto-externalizing was always
					 * the -u flag, which cc supplied.  So
					 * --std=<era> selects that era's
					 * flagless behavior and -u composes on
					 * top, exactly as it did historically.
					 * j11/cis is a HARDWARE level, not an
					 * assembler era: no as ever had those
					 * mnemonics. */
					char *t = argv[i] + 6;
					int understood;
					while (*t) {
						char tok[16];
						int n = 0;
						while (*t && *t != ',' && n < 15)
							tok[n++] = *t++;
						tok[n] = 0;
						if (*t == ',')
							t++;
						understood = 0;
						/* --std presets over the three axes
						 * (v3: manual survives, assembler
						 * lost; 1972 = documented bound) */
						if (!strcmp(tok, "v1") || !strcmp(tok, "v2") || !strcmp(tok, "v3")) {
							isa = 1972;
							sysnames = 1972;
							v7flag = 1;
							understood = 1;
							if (tok[1] == '1')
								v1fmt = 1;
						}
						if (!strcmp(tok, "v4") || !strcmp(tok, "v5") || !strcmp(tok, "v6")) {
							isa = 0;
							sysnames = 6;
							v7flag = 1;
							understood = 1;
						}
						if (!strcmp(tok, "v7") || !strcmp(tok, "bsd")) {
							isa = 0;
							sysnames = 0;
							v7flag = 1;
							understood = 1;
						}
						if (!strcmp(tok, "newbsd")) {
							isa = 211;
							sysnames = 0;
							v7flag = 1;
							newsym = 1;
							ncps = 32;
							understood = 1;
						}
						if (!strcmp(tok, "extended")) {
							jflag = 1;
							understood = 1;
						}
						if (!understood) {
							fprintf(stderr, "as: unknown --std token '%s' (v1,v2,v3,v4,v5,v6,v7,bsd,newbsd,extended)\n", tok);
							return 1;
						}
					}
				}
				else if (argv[i][1] == 'o' && i + 1 < argc)
					outfile = argv[++i];
				else if (argv[i][1] == 'u')
					uflag = 1; /* undefined->external.  In the
						    * GENUINE as (V7 and 2.9 identically), -u sets
						    * `unglob', pass 1 passes `-g' to as2, and
						    * doreloc ORs `defund'=040 into type-0 symbols;
						    * the genuine DEFAULT leaves them local.  cc
						    * always invokes `as -u' (cc.c av[1]), which is
						    * the behavior this port hard-wires as ITS
						    * default -- correct for every cc-built object. */
				else if (argv[i][1] == 'V')
					ovasflag = 1; /* ovas mode */
				else if (argv[i][1] == 'n') {
					newsym = 1;
					ncps = 32;
				} /* 2.11 string-table format */
				else if (argv[i][1] == 'j')
					jflag = 1; /* late-hardware mnemonics */
				else if (argv[i][1] == '7')
					v7flag = 1; /* the genuine no-'-u'
						     * default: undefined non-.globl symbols stay
						     * LOCAL (type 0), as hand-assembled kernel
						     * objects have them (V7 mch.o, invoked without
						     * -u by the kernel makefile).  Misnamed `-7'
						     * before the archaeology showed V7 and 2.9 as
						     * are identical here.  No other effect. */
				else if (argv[i][1] == '-' && !argv[i][2]) {
					/* `--': read standard input (2.11 as0.s).
					 * A LONE `-' stays the flag spelling of -u
					 * (its meaning in the option position of
					 * every as, V1 through 2.11). */
					if (nf < 64)
						files[nf++] = "--";
				}
				else { /* other flags ignored (incl. lone `-' = -u) */
				}
			}
			else if (nf < 64)
				files[nf++] = argv[i];
		}
		/* an explicit -u composes over any --std, as it did on the
		 * real assemblers (every era's flagless default was strict) */
		if (uflag)
			v7flag = 0;
		if (nf == 0) {
			fprintf(stderr, "as: no input file\n");
			return 1;
		}
		infile = files[0];
		/* read every file (exact bytes -- c1 output has embedded NULs),
		 * separated by a newline, into one buffer */
		for (i = 0; i < nf; i++) {
			if (!strcmp(files[i], "--")) { /* standard input */
				size_t cap = 8192, len = 0, got;
				p = xalloc(cap);
				while ((got = fread(p + len, 1, cap - len, stdin)) > 0) {
					len += got;
					if (len == cap) {
						char *np = xalloc(cap * 2);
						memcpy(np, p, len);
						p = np;
						cap *= 2;
					}
				}
				files[i] = p;
				flens[i] = len;
				total += len + 1;
				continue;
			}
			if (!(f = fopen(files[i], "r"))) {
				perror(files[i]);
				return 1;
			}
			fseek(f, 0, 2);
			flen = ftell(f);
			fseek(f, 0, 0);
			p = xalloc(flen + 1);
			flen = fread(p, 1, flen, f);
			fclose(f);
			files[i] = p;
			flens[i] = flen;
			total += flen + 1;
		}
		ibuf = xalloc(total + 1);
		p = ibuf;
		for (i = 0; i < nf; i++) {
			memcpy(p, files[i], flens[i]);
			p += flens[i];
			*p++ = '\n';
		}
		*p = 0;
		ibufstart = ibuf;
		ibufend = p;
	}

	/* span relaxation, reproducing the 2BSD as1+as2 pass structure:
	 *  (A) minimal layout to learn addresses and which refs are forward;
	 *  (B) the as1 estimate: every forward text branch long, the rest by
	 *      distance, settled (symbols end holding estimate-layout values);
	 *  (C) the as2 sizing pass, run ONCE over that estimate: each branch
	 *      decides inline (spanrec) from r0 = target - dot, forward refs
	 *      corrected by brdelt (the most recent label's estimate value
	 *      minus its dot this pass -- ANY segment, as23.s).  A data/bss
	 *      label zeroes the correction and following forward branches see
	 *      the full upstream drift: faithfully long, like 2.8.
	 * pass 2 then emits with the recorded spanlong[] choices.
	 * pass stays 1 (no bytes emitted); passno keeps passes distinct for dup-labels. */
	relaxing = 1;
	{
		/* (A+B) the as1 estimate (as16.s opl35/opl36): a SINGLE top-to-bottom
		 * pass, branches sized INLINE in spanrec().  A forward branch's target is
		 * still undefined when the branch is reached -> long; a backward branch is
		 * short iff its displacement is in [-254,0) bytes.  dot evolves exactly as
		 * in as16.s.  NOT iterated to a relaxation fixpoint -- that over-sizes
		 * backward branches and inflates the brdelt the as2 sizing pass reads
		 * (the rogue light() jbr divergence).  Symbols end holding estimate-layout
		 * values, which the deciding pass (C) reads via brdelt. */
		memset(spanlong, 0, sizeof spanlong);
		estimating = 1;
		passno++;
		pass = 1;
		ip = ibufstart;
		curseg = 0;
		dot[0] = dot[1] = dot[2] = 0;
		lineno = 1;
		pbsp = 0;
		memset(loccnt, 0, sizeof loccnt);
		spanidx = 0;
		tildeidx = 0;
		assemble();
		nspan = spanidx;
		estimating = 0;
		/* (C) the as2 sizing pass, done ONCE: each branch decides inline in
		 * spanrec() from r0 = target - dot (forward corrected by brdelt), and
		 * is sized immediately, so dot evolves with the decisions just as in
		 * the real as2 pass 0.  Forward targets still hold their estimate
		 * values when the branch is reached; label defs update them (and
		 * brdelt) as the pass advances.  Decisions are final (brtab). */
		memset(spanlong, 0, sizeof spanlong);
		deciding = 1;
		brdelt = 0;
		passno++;
		pass = 1;
		ip = ibufstart;
		curseg = 0;
		dot[0] = dot[1] = dot[2] = 0;
		lineno = 1;
		pbsp = 0;
		memset(loccnt, 0, sizeof loccnt);
		spanidx = 0;
		tildeidx = 0;
		assemble();
		deciding = 0;
	}
	relaxing = 0;
	/* segment sizes are now known (even-rounded, as ld rounds them); the
	 * data/bss base used to bias values in the unified object space */
	txtsize = (dot[0] + 1) & ~1;
	datsize = (dot[1] + 1) & ~1;
	/* assign symbol-table indices now, BEFORE pass 2 emits relocation
	 * words: emitword() stamps sym->index into the external relocation
	 * word, so the index must already match the symbol's position in the
	 * symbol table writeout() emits (same iteration order/predicate). */
	{
		struct sym *sp;
		int idx = 0;
		for (sp = symhead; sp; sp = sp->onext)
			if (emitsym(sp))
				sp->index = idx++;
	}
	/* pass 2: emit, using the relaxed span sizes */
	passno++;
	pass = 2;
	ip = ibufstart;
	curseg = 0;
	dot[0] = dot[1] = dot[2] = 0;
	lineno = 1;
	pbsp = 0;
	memset(loccnt, 0, sizeof loccnt);
	spanidx = 0;
	tildeidx = 0;
	assemble();
	/* native 2BSD as writes the object EVEN when there were errors (it does not
	 * unlink it), then exits nonzero -- e.g. lfstat.s' undefined `sys' operand
	 * still yields a complete, relocated .o alongside the diagnostic.  Emit
	 * first, then report/return the error status. */
	writeout();
	if (errors) {
		fprintf(stderr, "as: %d error(s)\n", errors);
		return 1;
	}
	return 0;
}
