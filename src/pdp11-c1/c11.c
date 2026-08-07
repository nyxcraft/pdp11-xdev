/*	@(#)c11.c	2.2	SCCS id keyword */
#
/*
 *  C compiler
 */

#include "c1.h"
#include <stdlib.h>
#include <stdarg.h>

int
max(int a, int b)
{
	if (a > b)
		return (a);
	return (b);
}

int
degree(struct tnode *at)
{
	register struct tnode *t, *t1;

	if ((t = at) == 0 || t->op == 0)
		return (0);
	if (t->op == CON)
		return (-3);
	if (t->op == AMPER)
		return (-2);
	if (t->op == ITOL) {
		if ((t1 = isconstant(t)) && (t1->value >= 0 || t1->type == UNSIGN))
			return (-2);
		if ((t1 = t->tr1)->type == UNSIGN && opdope[t1->op] & LEAF)
			return (-1);
	}
	if ((opdope[t->op] & LEAF) != 0) {
		if (t->type == CHAR || t->type == FLOAT)
			return (1);
		return (0);
	}
	return (t->degree);
}

void
pname(struct tnode *ap, int flag)
{
	register int i;
	register struct tnode *p;

	p = ap;
loop:
	switch (p->op) {
	case LCON:
		/* A long lays high word first on the PDP-11: flag<=10 is the word
		 * at dst+0 (high), flag>10 the word at dst+2 (low).  Mask to 16
		 * bits so %o doesn't sign-extend (e.g. low 0x86A0 must print as
		 * 0103240, not 037777703240). */
		printf("$%o", flag > 10 ? (int)(p->lvalue & 0177777)
					: (int)((p->lvalue >> 16) & 0177777));
		return;

	case SFCON:
	case CON:
		printf("$");
		psoct(p->value);
		return;

	case FCON:
		printf("L%d", (p->value > 0 ? p->value : -p->value));
		return;

	case NAME:
		i = p->offset;
		if (flag > 10)
			i += 2;
		if (i) {
			psoct(i);
			if (p->class != OFFS)
				putchar('+');
			if (p->class == REG)
				regerr();
		}
		switch (p->class) {
		case SOFFS:
		case XOFFS:
			pbase(p);

		case OFFS:
			printf("(r%d)", p->regno);
			return;

		case EXTERN:
		case STATIC:
			pbase(p);
			return;

		case REG:
			printf("r%d", p->nloc);
			return;
		}
		error("Compiler error: pname");
		return;

	case AMPER:
		putchar('$');
		p = p->tr1;
		if (p->op == NAME && p->class == REG)
			regerr();
		goto loop;

	case AUTOI:
		printf("(r%d)%c", p->nloc, flag == 1 ? 0 : '+');
		return;

	case AUTOD:
		printf("%c(r%d)", flag == 2 ? 0 : '-', p->nloc);
		return;

	case STAR:
		p = p->tr1;
		putchar('*');
		goto loop;
	}
	error("pname called illegally");
}

void
regerr(void)
{
	error("Illegal use of register");
}

void
pbase(struct tnode *ap)
{
	register struct tnode *p;

	p = ap;
	if (p->class == SOFFS || p->class == STATIC)
		printf("L%d", p->nloc);
	else
		printf("%.8s", (char *)&(p->nloc));
}

int
xdcalc(struct tnode *ap, int nrleft)
{
	register struct tnode *p;
	register int d;

	p = ap;
	d = dcalc(p, nrleft);
	/* dcalc tolerates p==0 (returns 0); guard p->type the same way --
	   the PDP-11 read *0 as benign, an LP64 host faults (cf. c2 dualop). */
	if (d < 20 && p != 0 && p->type == CHAR) {
		if (nrleft >= 1)
			d = 20;
		else
			d = 24;
	}
	return (d);
}

int
dcalc(struct tnode *ap, int nrleft)
{
	register struct tnode *p, *p1;

	if ((p = ap) == 0)
		return (0);
	switch (p->op) {
	case NAME:
		if (p->class == REG)
			return (9);

	case AMPER:
	case FCON:
	case LCON:
	case AUTOI:
	case AUTOD:
		return (12);

	case CON:
	case SFCON:
		if (p->value == 0)
			return (4);
		if (p->value == 1)
			return (5);
		if (p->value > 0)
			return (8);
		return (12);

	case STAR:
		p1 = p->tr1;
		if (p1->op == NAME || p1->op == CON || p1->op == AUTOI || p1->op == AUTOD)
			if (p->type != LONG)
				return (12);
	}
	if (p->type == LONG)
		nrleft--;
	return (p->degree <= nrleft ? 20 : 24);
}

int
notcompat(struct tnode *ap, int ast, int op)
{
	register int at, st;
	register struct tnode *p;

	p = ap;
	at = p->type;
	st = ast;
	if (st == 0) /* word, byte */
		return (at != CHAR && at != INT && at != UNSIGN && at < PTR);
	if (st == 1) /* word */
		return (at != INT && at != UNSIGN && at < PTR);
	if (st == 9 && (at & XTYPE))
		return (0);
	st -= 2;
	if ((at & (~(TYPE + XTYPE))) != 0)
		at = 020;
	if ((at & (~TYPE)) != 0)
		at = at & TYPE | 020;
	if (st == FLOAT && at == DOUBLE)
		at = FLOAT;
	if (p->op == NAME && p->class == REG && op == ASSIGN && st == CHAR)
		return (0);
	return (st != at);
}

void
prins(int op, int c, struct instab *itable)
{
	register struct instab *insp;
	register char *ip;

	for (insp = itable; insp->iop != 0; insp++) {
		if (insp->iop == op) {
			ip = c ? insp->str2 : insp->str1;
			if (ip == 0)
				break;
			printf("%s", ip);
			return;
		}
	}
	error("No match' for op %d", op);
}

int
collcon(struct tnode *ap)
{
	register int op;
	register struct tnode *p;

	p = ap;
	if (p->op == STAR) {
		if (p->type == LONG + PTR) /* avoid *x(r); *x+2(r) */
			return (0);
		p = p->tr1;
	}
	if (p->op == PLUS) {
		op = p->tr2->op;
		if (op == CON || op == AMPER)
			return (1);
	}
	return (0);
}

int
isfloat(struct tnode *at)
{
	register struct tnode *t;

	t = at;
	if ((opdope[t->op] & RELAT) != 0)
		t = t->tr1;
	if (t->type == FLOAT || t->type == DOUBLE) {
		nfloat = 1;
		return ('f');
	}
	return (0);
}

int
oddreg(struct tnode *t, int areg)
{
	register int reg;

	reg = areg;
	if (!isfloat(t)) {
		if (opdope[t->op] & RELAT) {
			if (t->tr1->type == LONG)
				return ((reg + 1) & ~01);
			return (reg);
		}
		switch (t->op) {
		case LLSHIFT:
		case ASLSHL:
			return ((reg + 1) & ~01);

		case DIVIDE:
		case MOD:
		case ASDIV:
		case ASMOD:
		case PTOI:
		case ULSH:
		case ASULSH:
			reg++;

		case TIMES:
		case ASTIMES:
			return (reg | 1);
		}
	}
	return (reg);
}

int
arlength(int t)
{
	if (t >= PTR)
		return (2);
	switch (t) {
	case INT:
	case CHAR:
	case UNSIGN:
		return (2);

	case LONG:
		return (4);

	case FLOAT:
	case DOUBLE:
		return (8);
	}
	return (1024);
}

/*
 * Strings for switch code.
 */

/*
 * Modified Memorial day May 80 to uniquely identify switch tables
 * (as on Vax) so a shell script can optionally include them in RO code.
 * This is useful in overlays to reduce the size of data space load.
 * wfj 5/80
 */
#ifdef MENLO_OVLY
char dirsw[] = {"\
cmp	r0,$%o\n\
jhi	L%d\n\
asl	r0\n\
jmp	*L%d(r0)\n\
\t.data\n\
L%d:\
"};

char hashsw[] = {"\
mov	r0,r1\n\
clr	r0\n\
div	$%o,r0\n\
asl	r1\n\
jmp	*L%d(r1)\n\
\t.data\n\
L%d:\
"};
#else
char dirsw[] = {"\
cmp	r0,$%o\n\
jhi	L%d\n\
asl	r0\n\
jmp	*L%d(r0)\n\
.data\n\
L%d:\
"};

char hashsw[] = {"\
mov	r0,r1\n\
clr	r0\n\
div	$%o,r0\n\
asl	r1\n\
jmp	*L%d(r1)\n\
.data\n\
L%d:\
"};

#endif
/*
 * If the unsigned casts below won't compile,
 * try using the calls to lrem and ldiv.
 */

void
pswitch(struct swtab *afp, struct swtab *alp, int deflab)
{
	int ncase, i, j, tabs, worst, best, range;
	register struct swtab *swp, *fp, *lp;
	int *poctab;

	fp = afp;
	lp = alp;
	if (fp == lp) {
		printf("jbr	L%d\n", deflab);
		return;
	}
	isn++;
	if (sort(fp, lp))
		return;
	ncase = lp - fp;
	lp--;
	range = lp->swval - fp->swval;
	/* direct switch */
	if (range > 0 && range <= 3 * ncase) {
		if (fp->swval)
			printf("sub	$%o,r0\n", fp->swval);
		printf(dirsw, range, deflab, isn, isn);
		isn++;
		for (i = fp->swval;; i++) {
			if (i == fp->swval) {
				printf("L%d\n", fp->swlab);
				if (fp == lp)
					break;
				fp++;
			}
			else
				printf("L%d\n", deflab);
		}
		printf(".text\n");
		return;
	}
	/* simple switch */
	if (ncase < 10) {
		for (fp = afp; fp <= lp; fp++)
			breq(fp->swval, fp->swlab);
		printf("jbr	L%d\n", deflab);
		return;
	}
	/* hash switch */
	best = 077777;
	poctab = (int *)getblk(((ncase + 2) / 2) * sizeof(*poctab));
	for (i = ncase / 4; i <= ncase / 2; i++) {
		for (j = 0; j < i; j++)
			poctab[j] = 0;
		for (swp = fp; swp <= lp; swp++)
			/* lrem(0, swp->swval, i) */
			poctab[(unsigned)swp->swval % i]++;
		worst = 0;
		for (j = 0; j < i; j++)
			if (poctab[j] > worst)
				worst = poctab[j];
		if (i * worst < best) {
			tabs = i;
			best = i * worst;
		}
	}
	i = isn++;
	printf(hashsw, tabs, i, i);
	isn++;
	for (i = 0; i < tabs; i++)
		printf("L%d\n", isn + i);
	printf(".text\n");
	for (i = 0; i < tabs; i++) {
		printf("L%d:", isn++);
		for (swp = fp; swp <= lp; swp++) {
			/* lrem(0, swp->swval, tabs) */
			if ((unsigned)swp->swval % tabs == i) {
				/* ldiv(0, swp->swval, tabs) */
				breq((unsigned)swp->swval / tabs, swp->swlab);
			}
		}
		printf("jbr	L%d\n", deflab);
	}
}

void
breq(int v, int l)
{
	if (v == 0)
		printf("tst	r0\n");
	else
		printf("cmp	r0,$%o\n", v);
	printf("jeq	L%d\n", l);
}

int
sort(struct swtab *afp, struct swtab *alp)
{
	register struct swtab *cp, *fp, *lp;
	int intch, t;

	fp = afp;
	lp = alp;
	while (fp < --lp) {
		intch = 0;
		for (cp = fp; cp < lp; cp++) {
			if (cp->swval == cp[1].swval) {
				error("Duplicate case (%d)", cp->swval);
				return (1);
			}
			if (cp->swval > cp[1].swval) {
				intch++;
				t = cp->swval;
				cp->swval = cp[1].swval;
				cp[1].swval = t;
				t = cp->swlab;
				cp->swlab = cp[1].swlab;
				cp[1].swlab = t;
			}
		}
		if (intch == 0)
			break;
	}
	return (0);
}

int
ispow2(struct tnode *atree)
{
	register int d;
	register struct tnode *tree;

	tree = atree;
	if (!isfloat(tree) && tree->tr2->op == CON) {
		d = tree->tr2->value;
		if (d > 1 && (d & (d - 1)) == 0)
			return (d);
	}
	return (0);
}

struct tnode *
pow2(struct tnode *atree)
{
	register int d, i;
	register struct tnode *tree;

	tree = atree;
	if (d = ispow2(tree)) {
		for (i = 0; (d >>= 1) != 0; i++)
			;
		tree->tr2->value = i;
		switch (tree->op) {
		case TIMES:
			tree->op = LSHIFT;
			break;

		case ASTIMES:
			tree->op = ASLSH;
			break;

		case DIVIDE:
			tree->op = ULSH;
			tree->tr2->value = -i;
			break;

		case ASDIV:
			tree->op = ASULSH;
			tree->tr2->value = -i;
			break;

		case MOD:
			tree->op = AND;
			tree->tr2->value = (1 << i) - 1;
			break;

		case ASMOD:
			tree->op = ASAND;
			tree->tr2->value = (1 << i) - 1;
			break;

		default:
			error("pow2 botch");
		}
		tree = optim(tree);
	}
	return (tree);
}

void
cbranch(struct tnode *atree, int albl, int cond, int areg)
{
	int l1, op;
	register int lbl, reg;
	register struct tnode *tree;

	lbl = albl;
	reg = areg;
again:
	if ((tree = atree) == 0)
		return;
	switch (tree->op) {
	case LOGAND:
		if (cond) {
			cbranch(tree->tr1, l1 = isn++, 0, reg);
			cbranch(tree->tr2, lbl, 1, reg);
			label(l1);
		}
		else {
			cbranch(tree->tr1, lbl, 0, reg);
			cbranch(tree->tr2, lbl, 0, reg);
		}
		return;

	case LOGOR:
		if (cond) {
			cbranch(tree->tr1, lbl, 1, reg);
			cbranch(tree->tr2, lbl, 1, reg);
		}
		else {
			cbranch(tree->tr1, l1 = isn++, 1, reg);
			cbranch(tree->tr2, lbl, 0, reg);
			label(l1);
		}
		return;

	case EXCLA:
		cbranch(tree->tr1, lbl, !cond, reg);
		return;

	case SEQNC:
		rcexpr(tree->tr1, efftab, reg);
		atree = tree->tr2;
		goto again;

	case ITOL:
		tree = tree->tr1;
		break;
	}
	op = tree->op;
	if (opdope[op] & RELAT && tree->tr1->op == ITOL && tree->tr2->op == ITOL) {
		tree->tr1 = tree->tr1->tr1;
		tree->tr2 = tree->tr2->tr1;
		if (op >= LESSEQ && op <= GREAT && (tree->tr1->type == UNSIGN || tree->tr2->type == UNSIGN))
			tree->op = op = op + LESSEQP - LESSEQ;
	}
	if (tree->type == LONG || opdope[op] & RELAT && tree->tr1->type == LONG) {
		longrel(tree, lbl, cond, reg);
		return;
	}
	rcexpr(tree, cctab, reg);
	op = tree->op;
	if ((opdope[op] & RELAT) == 0)
		op = NEQUAL;
	else {
		l1 = tree->tr2->op;
		if ((l1 == CON || l1 == SFCON) && tree->tr2->value == 0)
			op += 200; /* special for ptr tests */
		else
			op = maprel[op - EQUAL];
	}
	if (isfloat(tree))
		printf("cfcc\n");
	branch(lbl, op, !cond);
}

/* Called with either two or three arguments: the third (c) is read only when
 * aop is nonzero, which the K&R callers guaranteed by passing it exactly then.
 * A variadic prototype preserves every call site unchanged (cf. error). */
void
branch(int lbl, int aop, ...)
{
	register int op;

	if (op = aop) {
		va_list ap;
		int c;
		va_start(ap, aop);
		c = va_arg(ap, int);
		va_end(ap);
		prins(op, c, branchtab);
	}
	else
		printf("jbr");
	printf("\tL%d\n", lbl);
}

void
longrel(struct tnode *atree, int lbl, int cond, int reg)
{
	int xl1, xl2, xo, xz;
	register int op, isrel;
	register struct tnode *tree;

	if (reg & 01)
		reg++;
	reorder(&atree, cctab, reg);
	tree = atree;
	isrel = 0;
	if (opdope[tree->op] & RELAT) {
		isrel++;
		op = tree->op;
	}
	else
		op = NEQUAL;
	if (!cond)
		op = notrel[op - EQUAL];
	xl1 = xlab1;
	xl2 = xlab2;
	xo = xop;
	xlab1 = lbl;
	xlab2 = 0;
	xop = op;
	xz = xzero;
	xzero = !isrel || tree->tr2->op == ITOL && tree->tr2->tr1->op == CON && tree->tr2->tr1->value == 0;
	if (tree->op == ANDN) {
		tree->op = TAND;
		tree->tr2 = optim(tnode(COMPL, LONG, tree->tr2));
	}
	if (cexpr(tree, cctab, reg) < 0) {
		reg = rcexpr(tree, regtab, reg);
		printf("ashc	$0,r%d\n", reg);
		branch(xlab1, op, 0);
	}
	xlab1 = xl1;
	xlab2 = xl2;
	xop = xo;
	xzero = xz;
}

/*
 * Tables for finding out how best to do long comparisons.
 * First dimen is whether or not the comparison is with 0.
 * Second is which test: e.g. a>b->
 *	cmp	a,b
 *	bgt	YES		(first)
 *	blt	NO		(second)
 *	cmp	a+2,b+2
 *	bhi	YES		(third)
 *  NO:	...
 * Note some tests may not be needed.
 */
char lrtab[2][3][6] = {
	0,
	NEQUAL,
	LESS,
	LESS,
	GREAT,
	GREAT,
	NEQUAL,
	0,
	GREAT,
	GREAT,
	LESS,
	LESS,
	EQUAL,
	NEQUAL,
	LESSEQP,
	LESSP,
	GREATQP,
	GREATP,

	0,
	NEQUAL,
	LESS,
	LESS,
	GREATEQ,
	GREAT,
	NEQUAL,
	0,
	GREAT,
	0,
	0,
	LESS,
	EQUAL,
	NEQUAL,
	EQUAL,
	0,
	0,
	NEQUAL,
};

int
xlongrel(int f)
{
	register int op, bno;

	op = xop;
	if (f == 0) {
		if (bno = lrtab[xzero][0][op - EQUAL])
			branch(xlab1, bno, 0);
		if (bno = lrtab[xzero][1][op - EQUAL]) {
			xlab2 = isn++;
			branch(xlab2, bno, 0);
		}
		if (lrtab[xzero][2][op - EQUAL] == 0)
			return (1);
	}
	else {
		branch(xlab1, lrtab[xzero][2][op - EQUAL], 0);
		if (xlab2)
			label(xlab2);
	}
	return (0);
}

void
label(int l)
{
	printf("L%d:", l);
}

void
popstk(int a)
{
	switch (a) {
	case 0:
		return;

	case 2:
		printf("tst	(sp)+\n");
		return;

	case 4:
		printf("cmp	(sp)+,(sp)+\n");
		return;
	}
	printf("add	$%o,sp\n", a);
}

/* variadic so char* arguments (e.g. %.8s names) are not truncated to int
 * on LP64 the way the old fixed-parameter form would */
void
error(char *s, ...)
{
	va_list ap;
	nerror++;
	fprintf(stderr, "%d: ", line);
	va_start(ap, s);
	vfprintf(stderr, s, ap);
	va_end(ap);
	putc('\n', stderr);
}

void
psoct(int an)
{
	register int n, sign;

	sign = 0;
	if ((n = an) < 0) {
		n = -n;
		sign = '-';
	}
	if (sign) /* avoid emitting a NUL %c for positives */
		putchar(sign);
	printf("%o", n);
}

/*
 * Read in an intermediate file.
 */
#define STKS 100

void
getree(void)
{
	struct tnode *expstack[STKS];
	register struct tnode **sp;
	register int t, op;
	static char s[9];
	struct swtab *swp;
	char numbuf[64];
	struct tnode *np; /* superset: reaches xtname.name and tname.nloc */
	struct xtname *xnp;
	struct ftconst *fp;
	struct lconst *lp;
	struct fasgn *sap;
	int lbl, cond, lbl2, lbl3;

	curbase = funcbase;
	sp = expstack;
	for (;;) {
		if (sp >= &expstack[STKS])
			error("Stack overflow botch");
		op = geti();
		if ((op & 0177400) != 0177000) {
			error("Intermediate file error");
			exit(1);
		}
		lbl = 0;
		switch (op &= 0377) {
		case SINIT:
			printf("%o\n", geti());
			break;

		case EOFC:
			return;

		case BDATA:
			if (geti() == 1) {
				printf(".byte ");
				for (;;) {
					printf("%o", geti());
					if (geti() != 1)
						break;
					printf(",");
				}
				printf("\n");
			}
			break;

		case PROG:
			printf(".text\n");
			break;

		case DATA:
			printf(".data\n");
			break;

		case BSS:
			printf(".bss\n");
			break;

		case SYMDEF:
			outname(s);
			printf(".globl%s%.8s\n", s[0] ? "	" : "", s);
			sfuncr.nloc = 0;
			break;

		case RETRN:
			printf("jmp	cret\n");
			break;

		case CSPACE:
			outname(s);
			printf(".comm	%.8s,%o\n", s, geti());
			break;

		case SSPACE:
			printf(".=.+%o\n", (t = geti()));
			totspace += (unsigned)t;
			break;

		case EVEN:
			printf(".even\n");
			break;

		case SAVE:
			printf("jsr	r5,csv\n");
			break;

		case SETSTK:
#ifdef MENLO_OVLY
			/*
			 * More of Bill Shannon's fix. This actually was a bug,
			 * harmless as it was though. It sort of enforced a STAUTO
			 * of -6 , you see....
			 */
			t = geti();
#else
			t = geti() - 6;
#endif
			if (t == 2)
				printf("tst	-(sp)\n");
			else if (t != 0)
				printf("sub	$%o,sp\n", t & 0177777); /* 16-bit */
			break;

		case PROFIL:
			t = geti();
			printf("mov	$L%d,r0\njsr	pc,mcount\n", t);
			printf(".bss\nL%d:.=.+2\n.text\n", t);
			break;

		case SNAME:
			outname(s);
			printf("~%s=L%d\n", s + 1, geti());
			break;

		case ANAME:
			outname(s);
			/* LP64: geti() sign-extends negative auto offsets; the
			 * native 16-bit c1 printed them as 16-bit octal
			 * (~ce=177766, not 37777777766).  as truncates either
			 * form identically, but match the native listing. */
			printf("~%s=%o\n", s + 1, geti() & 0177777);
			break;

		case RNAME:
			outname(s);
			printf("~%s=r%d\n", s + 1, geti());
			break;

		case SWIT:
			t = geti();
			line = geti();
			/* The switch table is a PACKED array that pswitch() walks with ++.
			 * getblk() pads every block to sizeof(struct tnode) (the node-union
			 * superset) on the host, which would space the swtab entries that far
			 * apart and make pswitch read garbage (0) for every case value -- so
			 * pack them tightly into the per-function arena here instead. */
			curbase = funcbase;
			swp = (struct swtab *)funcbase;
			while (swp->swlab = geti()) {
				swp->swval = geti();
				if ((char *)(++swp + 1) >= coremax) {
					error("Switch table overflow");
					exit(1);
				}
			}
			curbase = (char *)(swp + 1);
			pswitch((struct swtab *)funcbase, swp, t);
			break;

		case C3BRANCH: /* for fortran [sic] */
			lbl = geti();
			lbl2 = geti();
			lbl3 = geti();
			goto xpr;

		case CBRANCH:
			lbl = geti();
			cond = geti();

		case EXPR:
		xpr:
			line = geti();
			if (sp != &expstack[1]) {
				error("Expression input botch");
				exit(1);
			}
			nstack = 0;
			*sp = optim(*--sp);
			if (op == CBRANCH)
				cbranch(*sp, lbl, cond, 0);
			else if (op == EXPR)
				rcexpr(*sp, efftab, 0);
			else {
				if ((*sp)->type == LONG) {
					rcexpr(tnode(RFORCE, (*sp)->type, *sp), efftab, 0);
					printf("ashc	$0,r0\n");
				}
				else {
					rcexpr(*sp, cctab, 0);
					if (isfloat(*sp))
						printf("cfcc\n");
				}
				printf("jgt	L%d\n", lbl3);
				printf("jlt	L%d\njbr	L%d\n", lbl, lbl2);
			}
			curbase = funcbase;
			break;

		case NAME:
			t = geti();
			if (t == EXTERN) {
				np = getblk(sizeof(*xnp));
				np->type = geti();
				outname(np->name);
			}
			else {
				np = getblk(sizeof(*np));
				np->type = geti();
				np->nloc = geti();
			}
			np->op = NAME;
			np->class = t;
			np->regno = 0;
			np->offset = 0;
			*sp++ = np;
			break;

		case CON:
			t = geti();
			/* geti() returns an unsigned 16-bit word; sign-extend a signed int
			 * constant so -1 stays -1 (not 65535) on the LP64 host -- otherwise
			 * the `value<0' negative-constant paths (long==-1 etc.) never fire. */
			*sp++ = tconst(t == INT ? (short)geti() : geti(), t);
			break;

		case LCON:
			geti();	     /* ignore type, assume long */
			t = geti();  /* high word (c0 emits PDP-11 order, high-first) */
			op = geti(); /* low word */
			/* geti() sign-extends to 16 bits (native int semantics); the fold
			 * test below keys on the RAW unsigned halves -- mask, or a long
			 * like 40920L (high 0, low >= 0100000) wrongly folds to a
			 * sign-extended int and -1L (high == 0177777) never folds. */
			t &= 0177777;
			op &= 0177777;
			/* When the long is a sign-extended int -- high word equals the
			 * sign of the low word -- fold to ITOL(CON) so the constant
			 * optimisations (e.g. MINUS->PLUS for `x - 1L') recognise it;
			 * otherwise keep a full LCON.  This is the authentic c11 keying
			 * (high word in t). */
			if ((t == 0 && op < 0100000) || (t == 0177777 && op >= 0100000)) {
				*sp++ = tnode(ITOL, LONG, tconst((short)op, INT));
				break;
			}
			lp = (struct lconst *)getblk(sizeof(*lp));
			lp->op = LCON;
			lp->type = LONG;
			/* t = high word, op = low word -> canonical numeric value
			 * (high<<16)|low. */
			lp->lvalue = ((long)t << 16) | (op & 0177777);
			*sp++ = (struct tnode *)lp;
			break;

		case FCON:
			t = geti();
			outname(numbuf);
			fp = (struct ftconst *)getblk(sizeof(*fp));
			fp->op = FCON;
			fp->type = t;
			fp->value = isn++;
			fp->fvalue = atof(numbuf);
			{
				void softfp_atof(char *, unsigned short *);
				softfp_atof(numbuf, fp->fwords);
			}
			*sp++ = (struct tnode *)fp;
			break;

		case FSEL:
			*sp = tnode(FSEL, geti(), *--sp, NULL);
			t = geti();
			(*sp++)->tr2 = tnode(COMMA, INT, tconst(geti(), INT), tconst(t, INT));
			break;

		case STRASG:
			sap = (struct fasgn *)getblk(sizeof(*sap));
			sap->op = STRASG;
			sap->type = geti();
			sap->mask = geti();
			sap->tr1 = *--sp;
			sap->tr2 = NULL;
			*sp++ = (struct tnode *)sap;
			break;

		case NULLOP:
			*sp++ = tnode(0, 0, NULL, NULL);
			break;

		case LABEL:
			label(geti());
			break;

		case NLABEL:
			outname(s);
			printf("%.8s:\n", s);
			break;

		case RLABEL:
			outname(s);
			printf("%.8s:\n~~%s:\n", s, s + 1);
			break;

		case BRANCH:
			branch(geti(), 0);
			break;

		case SETREG:
			nreg = geti() - 1;
			break;

		default:
			if (opdope[op] & BINARY) {
				if (sp < &expstack[1]) {
					error("Binary expression botch");
					exit(1);
				}
				{
					struct tnode *rt, *lt;
					int ty;
					/* operands are tnode pointers; the original stashed one
					 * in the int `t' and relied on fixed arg-eval order --
					 * both break on LP64 */
					rt = *--sp;
					lt = *--sp;
					ty = geti();
					*sp++ = tnode(op, ty, lt, rt);
				}
			}
			else {
				int ty = geti();
				sp[-1] = tnode(op, ty, sp[-1]);
			}
			break;
		}
	}
}

int
geti(void)
{
	register int i;

	i = getchar();
	i += getchar() << 8;
	return ((short)i); /* LP64: PDP-11 int is 16 bits -- sign-extend, so
			    * e.g. a switch case value 0177776 reads as -2
			    * (else pswitch's range check fails and a dense
			    * negative-min switch falls to a compare chain) */
}

char *
outname(char *s) /* LP64: parameter and return are char*, not int */
{
	register char *p, c;
	register int n;

	p = s;
	n = 0;
	while (c = getchar()) {
		*p++ = c;
		n++;
	}
	do {
		*p++ = 0;
	}
	while (n++ < 8);
	return (s);
}

void
strasg(struct fasgn *atp)
{
	register struct tnode *tp;
	register int nwords, i;

	nwords = atp->mask / SZINT;
	tp = atp->tr1;
	if (tp->op != ASSIGN) {
		if (tp->op == RFORCE) { /* function return */
			if (sfuncr.nloc == 0) {
				sfuncr.nloc = isn++;
				printf(".bss\nL%d:.=.+%o\n.text\n", sfuncr.nloc, nwords * SZINT);
			}
			atp->tr1 = tnode(ASSIGN, STRUCT, &sfuncr, tp->tr1);
			strasg(atp);
			printf("mov	$L%d,r0\n", sfuncr.nloc);
			return;
		}
		if (tp->op == CALL) {
			rcexpr(tp, efftab, 0);
			return;
		}
		error("Illegal structure operation");
		return;
	}
	tp->tr2 = strfunc(tp->tr2);
	if (nwords == 1)
		setype(tp, INT);
	else if (nwords == SZINT)
		setype(tp, LONG);
	else {
		if (tp->tr1->op != NAME && tp->tr1->op != STAR || tp->tr2->op != NAME && tp->tr2->op != STAR) {
			error("unimplemented structure assignment");
			return;
		}
		tp->tr1 = tnode(AMPER, STRUCT + PTR, tp->tr1);
		tp->tr2 = tnode(AMPER, STRUCT + PTR, tp->tr2);
		tp->op = STRSET;
		tp->type = STRUCT + PTR;
		tp = optim(tp);
		rcexpr(tp, efftab, 0);
		if (nwords < 7) {
			for (i = 0; i < nwords; i++)
				printf("mov	(r1)+,(r0)+\n");
			return;
		}
		if (nreg <= 1)
			printf("mov	r2,-(sp)\n");
		printf("mov	$%o,r2\n", nwords);
		printf("L%d:mov	(r1)+,(r0)+\ndec\tr2\njne\tL%d\n", isn, isn);
		isn++;
		if (nreg <= 1)
			printf("mov	(sp)+,r2\n");
		return;
	}
	rcexpr(tp, efftab, 0);
}

void
setype(register struct tnode *p, register int t)
{
	for (;; p = p->tr1) {
		p->type = t;
		if (p->op == AMPER)
			t = decref(t);
		else if (p->op == STAR)
			t = incref(t);
		else if (p->op == ASSIGN)
			setype(p->tr2, t);
		else if (p->op != PLUS)
			break;
	}
}

/*
 * Reduce the degree-of-reference by one.
 * e.g. turn "ptr-to-int" into "int".
 */
int
decref(int at)
{
	register int t;

	t = at;
	if ((t & ~TYPE) == 0) {
		error("Illegal indirection");
		return (t);
	}
	return ((t >> TYLEN) & ~TYPE | t & TYPE);
}

/*
 * Increase the degree of reference by
 * one; e.g. turn "int" to "ptr-to-int".
 */
int
incref(int t)
{
	return (((t & ~TYPE) << TYLEN) | (t & TYPE) | PTR);
}
