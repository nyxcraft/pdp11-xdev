/*	@(#)c02.c	2.2	SCCS id keyword	*/
#
/* C compiler
 *
 *
 */

#include "c0.h"

/*
 * Process a single external definition
 */
void extdef(void)
{
	register int o;
	int sclass, scflag, *cb;
	struct hshtab typer;
	register struct hshtab *ds;

	if(((o=symbol())==EOFC) || o==SEMI)
		return;
	peeksym = o;
	sclass = 0;
	blklev = 0;
	if (getkeywords(&sclass, &typer)==0) {
		sclass = EXTERN;
		if (peeksym!=NAME)
			goto syntax;
	}
	scflag = 0;
	if (sclass==DEFXTRN) {
		scflag++;
		sclass = EXTERN;
	}
	if (sclass!=EXTERN && sclass!=STATIC && sclass!=TYPEDEF)
		error("Illegal storage class");
	do {
		defsym = 0;
		paraml = 0;
		parame = 0;
		if (sclass==TYPEDEF) {
			decl1(TYPEDEF, &typer, 0, NULL);
			continue;
		}
		decl1(EXTERN, &typer, 0, NULL);
		if ((ds=defsym)==0)
			return;
		funcsym = ds;
		if ((ds->htype&XTYPE)==FUNC) {
			if ((peeksym=symbol())==LBRACE || peeksym==KEYW
			 || (peeksym==NAME && csym->hclass==TYPEDEF)) {
				funcblk.type = decref(ds->htype);
				funcblk.strp = ds->hstrp;
				setinit(ds);
				outcode("BS", SYMDEF, sclass==EXTERN?ds->name:"");
				cfunc();
				return;
			}
			if (paraml)
				error("Inappropriate parameters");
		} else if ((o=symbol())==COMMA || o==SEMI) {
			peeksym = o;
			o = (length((struct tnode *)ds)+ALIGN) & ~ALIGN;
			if (sclass==STATIC) {
				setinit(ds);
				outcode("BSBBSBN", SYMDEF, "", BSS, NLABEL, ds->name, SSPACE, o);
			} else if (scflag)
				outcode("BSN", CSPACE, ds->name, o);
		} else {
			if (o!=ASSIGN)
				peeksym = o;
			setinit(ds);
			if (sclass==EXTERN)
				outcode("BS", SYMDEF, ds->name);
			outcode("BBS", DATA, NLABEL, ds->name);
			cb = (int *)funcbase;
			if (cinit(ds, 1, sclass) & ALIGN)
				outcode("B", EVEN);
			if (maxdecl > (char *)cb)
				cb = (int *)maxdecl;
			funcbase = (char *)cb;
		}
	} while ((o=symbol())==COMMA);
	if (o==SEMI)
		return;
syntax:
	if (o==RBRACE) {
		error("Too many }'s");
		peeksym = 0;
		return;
	}
	error("External definition syntax");
	errflush(o);
	statement();
}

/*
 * Process a function definition.
 */
void cfunc(void)
{
	register int *cb;
	register int sloc;

	sloc = isn;
	isn += 2;
	outcode("BBS", PROG, RLABEL, funcsym->name);
	if (proflg)
		outcode("BN", PROFIL, isn++);
	cb = (int *)curbase;
	regvar = 5;
	autolen = STAUTO;
	maxauto = STAUTO;
	blklev = 1;
	declist(ARG);
	outcode("B", SAVE);
	funchead();
	branch(sloc);
	label(sloc+1);
	retlab = isn++;
	blklev = 0;
	if ((peeksym = symbol()) != LBRACE)
		error("Compound statement required");
	statement();
	outcode("BNB", LABEL, retlab, RETRN);
	label(sloc);
	outcode("BN", SETSTK, -maxauto+STAUTO);	/* MENLO_OVLY bug fix */
	branch(sloc+1);
	if ((char *)cb < maxdecl)
		cb = (int *)maxdecl;
	curbase = funcbase = (char *)cb;
}

/*
 * Process the initializers for an external definition.
 */
int cinit(struct hshtab *anp, int flex, int sclass)
{
	register struct phshtab *np;
	register int nel, ninit;
	int width, isarray, o, brace, realtype, *cb;
	struct tnode *s;

	cb = (int *)funcbase;
	np = (struct phshtab *)gblock(sizeof(*np));
	funcbase = curbase;
	cpysymb(np, (struct phshtab *)anp);
	realtype = np->htype;
	isarray = 0;
	if ((realtype&XTYPE) == ARRAY)
		isarray++;
	else
		flex = 0;
	width = length((struct tnode *)np);
	nel = 1;
	/*
	 * If it's an array, find the number of elements.
	 * temporarily modify to look like kind of thing it's
	 * an array of.
	 */
	if (sclass==AUTO)
		if (isarray || realtype==STRUCT)
			error("No auto. aggregate initialization");
	if (isarray) {
		np->htype = decref(realtype);
		np->hsubsp++;
		if (width==0 && flex==0)
			error("0-length row: %.8s", anp->name);
		o = length((struct tnode *)np);
		/* nel = ldiv(0, width, o); */
		nel = (unsigned)width/o;
		width = o;
	}
	brace = 0;
	if ((peeksym=symbol())==LBRACE && (isarray || np->htype!=STRUCT)) {
		peeksym = -1;
		brace++;
	}
	ninit = 0;
	do {
		if ((o=symbol())==RBRACE)
			break;
		peeksym = o;
		if (o==STRING && realtype==ARRAY+CHAR) {
			if (sclass==AUTO)
				error("No strings in automatic");
			peeksym = -1;
			putstr(0, flex?10000:nel);
			ninit += nchstr;
			o = symbol();
			break;
		} else if (np->htype==STRUCT) {
			strinit((struct tnode *)np, sclass);
		} else if ((np->htype&ARRAY)==ARRAY || peeksym==LBRACE)
			cinit((struct hshtab *)np, 0, sclass);
		else {
			initflg++;
			s = tree();
			initflg = 0;
			if (np->hflag&FFIELD)
				error("No field initialization");
			*cp++ = nblock((struct hshtab *)np);
			*cp++ = s;
			build(ASSIGN);
			if (sclass==AUTO||sclass==REG)
				rcexpr(*--cp);
			else if (sclass==ENUMCON) {
				if (s->op!=CON)
					error("Illegal enum constant for %.8s", anp->name);
				anp->hoffset = s->value;
			} else
				rcexpr(block(INIT,np->htype,NULL,NULL,(*--cp)->tr2, NULL));
		}
		ninit++;
		if ((ninit&077)==0 && sclass==EXTERN)
			outcode("BS", SYMDEF, "");
	} while ((o=symbol())==COMMA && (ninit<nel || brace || flex));
	if (brace==0 || o!=RBRACE)
		peeksym = o;
	/*
	 * If there are too few initializers, allocate
	 * more storage.
	 * If there are too many initializers, extend
	 * the declared size for benefit of "sizeof"
	 */
	if (ninit<nel && sclass!=AUTO)
		outcode("BN", SSPACE, (nel-ninit)*width);
	else if (ninit>nel) {
		if (flex && nel==0) {
			np->hsubsp[-1] = ninit;
		} else
			error("Too many initializers: %.8s", anp->name);
		nel = ninit;
	}
	curbase = funcbase = (char *)cb;
	return(nel*width);
}

/*
 * Initialize a structure
 */
void strinit(struct tnode *np, int sclass)
{
	register struct hshtab **mlp;
	static int zerloc;
	register int o, brace;

	if ((mlp = np->strp->memlist)==NULL) {
		mlp = (struct hshtab **)&zerloc;
		error("Undefined structure initialization");
	}
	brace = 0;
	if ((o = symbol()) == LBRACE)
		brace++;
	else
		peeksym = o;
	do {
		if ((o=symbol()) == RBRACE)
			break;
		peeksym = o;
		if (*mlp==0) {
			error("Too many structure initializers");
			cinit((struct hshtab *)&funcblk, 0, sclass);
		} else
			cinit(*mlp++, 0, sclass);
		if (*mlp ==  &structhole) {
			outcode("B", EVEN);
			mlp++;
		}
	} while ((o=symbol())==COMMA && (*mlp || brace));
	if (sclass!=AUTO && sclass!=REG) {
		if (*mlp)
			outcode("BN", SSPACE, np->strp->ssize - (*mlp)->hoffset);
		outcode("B", EVEN);
	}
	if (o!=RBRACE || brace==0)
		peeksym = o;
}

/*
 * Mark already initialized
 */
void setinit(struct hshtab *anp)
{
	register struct hshtab *np;

	np = anp;
	if (np->hflag&FINIT)
		error("%s multiply defined", np->name);
	np->hflag |= FINIT;
}

/*
 * Process one statement in a function.
 */
/* statement() reuses o1 (declared struct hshtab *) as an int label holder in
 * the keyword branches -- the original PDP-11 "everything is a 16-bit word"
 * idiom, where a symbol pointer and a small label number occupy one register
 * interchangeably.  On an LP64 host that mixes int and pointer widths; the
 * value always round-trips (labels are small and never alias the pointer
 * uses), so the emitted intermediate code is byte-identical.  Scope the
 * int<->pointer diagnostic off for this one function rather than double-cast
 * every use. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-conversion"
void statement(void)
{
	register int o, o2;
	register struct hshtab *o1;
	int o3;
	struct tnode *np;
	int sauto, sreg;

stmt:
	switch(o=symbol()) {

	case EOFC:
		error("Unexpected EOF");
	case SEMI:
		return;

	case LBRACE:
		sauto = autolen;
		sreg = regvar;
		blockhead();
		while (!eof) {
			if ((o=symbol())==RBRACE) {
				autolen = sauto;
				if (sreg!=regvar)
					outcode("BN", SETREG, sreg);
				regvar = sreg;
				blkend();
				return;
			}
			peeksym = o;
			statement();
		}
		error("Missing '}'");
		return;

	case KEYW:
		switch(cval) {

		case GOTO:
			if (o1 = simplegoto())
				branch(o1);
			else 
				dogoto();
			goto semi;

		case RETURN:
			doret();
			goto semi;

		case IF:
			np = pexpr();
			o2 = 0;
			if ((int)(long)(o1=symbol())==KEYW) switch (cval) {
			case GOTO:
				if (o2=simplegoto())
					goto simpif;
				cbranch(np, o2=isn++, 0);
				dogoto();
				label(o2);
				goto hardif;

			case RETURN:
				if (nextchar()==';') {
					o2 = retlab;
					goto simpif;
				}
				cbranch(np, o1=isn++, 0);
				doret();
				label(o1);
				o2++;
				goto hardif;

			case BREAK:
				o2 = brklab;
				goto simpif;

			case CONTIN:
				o2 = contlab;
			simpif:
				chconbrk(o2);
				cbranch(np, o2, 1);
			hardif:
				if ((o=symbol())!=SEMI)
					goto syntax;
				if ((int)(long)(o1=symbol())==KEYW && cval==ELSE)
					goto stmt;
				peeksym = o1;
				return;
			}
			peeksym = o1;
			cbranch(np, o1=isn++, 0);
			statement();
			if ((o=symbol())==KEYW && cval==ELSE) {
				o2 = isn++;
				branch(o2);
				label(o1);
				statement();
				label(o2);
				return;
			}
			peeksym = o;
			label(o1);
			return;

		case WHILE:
			o1 = contlab;
			o2 = brklab;
			label(contlab = isn++);
			cbranch(pexpr(), brklab=isn++, 0);
			statement();
			branch(contlab);
			label(brklab);
			contlab = o1;
			brklab = o2;
			return;

		case BREAK:
			chconbrk(brklab);
			branch(brklab);
			goto semi;

		case CONTIN:
			chconbrk(contlab);
			branch(contlab);
			goto semi;

		case DO:
			o1 = contlab;
			o2 = brklab;
			contlab = isn++;
			brklab = isn++;
			label(o3 = isn++);
			statement();
			label(contlab);
			contlab = o1;
			if ((o=symbol())==KEYW && cval==WHILE) {
				cbranch(tree(), o3, 1);
				label(brklab);
				brklab = o2;
				goto semi;
			}
			goto syntax;

		case CASE:
			o1 = conexp();
			if ((o=symbol())!=COLON)
				goto syntax;
			if (swp==0) {
				error("Case not in switch");
				goto stmt;
			}
			if(swp>=swtab+SWSIZ) {
				error("Switch table overflow");
			} else {
				swp->swlab = isn;
				(swp++)->swval = o1;
				label(isn++);
			}
			goto stmt;

		case SWITCH:
			o1 = brklab;
			brklab = isn++;
			np = pexpr();
			chkw(np, -1);
			rcexpr(block(RFORCE,0,NULL,NULL,np, NULL));
			pswitch();
			brklab = o1;
			return;

		case DEFAULT:
			if (swp==0)
				error("Default not in switch");
			if (deflab)
				error("More than 1 'default'");
			if ((o=symbol())!=COLON)
				goto syntax;
			label(deflab = isn++);
			goto stmt;

		case FOR:
			o1 = contlab;
			o2 = brklab;
			contlab = isn++;
			brklab = isn++;
			if (o=forstmt())
				goto syntax;
			label(brklab);
			contlab = o1;
			brklab = o2;
			return;

		case ELSE:
			error("Inappropriate 'else'");
			statement();
			return;
		}
		error("Unknown keyword");
		goto syntax;

	case NAME:
		if (nextchar()==':') {
			peekc = 0;
			o1 = csym;
			if (o1->hclass>0) {
				if (o1->hblklev==0) {
					pushdecl((struct phshtab *)o1);
					o1->hoffset = 0;
				} else {
					defsym = o1;
					redec();
					goto stmt;
				}
			}
			o1->hclass = STATIC;
			o1->htype = ARRAY;
			o1->hflag |= FLABL;
			if (o1->hoffset==0)
				o1->hoffset = isn++;
			label(o1->hoffset);
			goto stmt;
		}
	}
	peeksym = o;
	rcexpr(tree());

semi:
	if ((o=symbol())==SEMI)
		return;
syntax:
	error("Statement syntax");
	errflush(o);
}
#pragma GCC diagnostic pop

/*
 * Process a for statement.
 */
int forstmt(void)
{
	register int l, o, sline;
	int sline1, *ss;
	struct tnode *st;

	if ((o=symbol()) != LPARN)
		return(o);
	if ((o=symbol()) != SEMI) {		/* init part */
		peeksym = o;
		rcexpr(tree());
		if ((o=symbol()) != SEMI)
			return(o);
	}
	label(contlab);
	if ((o=symbol()) != SEMI) {		/* test part */
		peeksym = o;
		cbranch(tree(), brklab, 0);
		if ((o=symbol()) != SEMI)
			return(o);
	}
	if ((peeksym=symbol()) == RPARN) {	/* incr part */
		peeksym = -1;
		statement();
		branch(contlab);
		return(0);
	}
	l = contlab;
	contlab = isn++;
	st = tree();
	sline = line;
	if ((o=symbol()) != RPARN)
		return(o);
	ss = (int *)funcbase;
	funcbase = curbase;
	statement();
	sline1 = line;
	line = sline;
	label(contlab);
	rcexpr(st);
	line = sline1;
	if ((char *)ss < maxdecl)
		ss = (int *)maxdecl;
	curbase = funcbase = (char *)ss;
	branch(l);
	return(0);
}

/*
 * A parenthesized expression,
 * as after "if".
 */
struct tnode *
pexpr(void)
{
	register int o;
	register struct tnode *t;	/* tree() returns a pointer, not int */

	if ((o=symbol())!=LPARN)
		goto syntax;
	t = tree();
	if ((o=symbol())!=RPARN)
		goto syntax;
	return(t);
syntax:
	error("Statement syntax");
	errflush(o);
	return(0);
}

/*
 * The switch statement, which involves collecting the
 * constants and labels for the cases.
 */
void pswitch(void)
{
	register struct swtab *cswp, *sswp;
	int dl, swlab;

	cswp = sswp = swp;
	if (swp==0)
		cswp = swp = swtab;
	branch(swlab=isn++);
	dl = deflab;
	deflab = 0;
	statement();
	branch(brklab);
	label(swlab);
	if (deflab==0)
		deflab = brklab;
	outcode("BNN", SWIT, deflab, line);
	for (; cswp < swp; cswp++)
		outcode("NN", cswp->swlab, cswp->swval);
	outcode("0");
	label(brklab);
	deflab = dl;
	swp = sswp;
}

/*
 * funchead is called at the start of each function
 * to process the arguments, which have been linked in a list.
 * This list is necessary because in
 * f(a, b) float b; int a; ...
 * the names are seen before the types.
 */
/*
 * Structure resembling a block for a register variable.
 */
struct	hshtab	hreg = { REG, 0, 0, NULL, NULL, 0 };
struct	tnode	areg = { NAME, 0, NULL, NULL, (struct tnode *)&hreg};
void funchead(void)
{
	register int pl;
	register struct hshtab *cs;
	struct tnode *bstack[2];

	pl = STARG;
	while(paraml) {
		parame->hpnext = 0;
		cs = paraml;
		paraml = paraml->hpnext;
		if (cs->htype==FLOAT)
			cs->htype = DOUBLE;
		cs->hoffset = pl;
		if ((cs->htype&XTYPE) == ARRAY) {
			cs->htype -= (ARRAY-PTR);	/* set ptr */
			cs->hsubsp++;		/* pop dims */
		}
		pl += rlength((struct tnode *)cs);
		if (cs->hclass==AREG && (hreg.hoffset=goodreg(cs))>=0) {
			bstack[0] = &areg;
			bstack[1] = nblock(cs);
			cp = &bstack[2];
			areg.type = cs->htype;
			cs->hclass = AUTO;
			build(ASSIGN);
			rcexpr(bstack[0]);
			cs->hoffset = hreg.hoffset;
			cs->hclass = REG;
		} else
			cs->hclass = AUTO;
		prste(cs);
	}
	for (cs=hshtab; cs<hshtab+HSHSIZ; cs++) {
		if (cs->name[0] == '\0')
			continue;
		if (cs->hclass == ARG || cs->hclass==AREG)
			error("Not an argument: %.8s", cs->name);
	}
	outcode("BN", SETREG, regvar);
}

void blockhead(void)
{
	register int r;

	r = regvar;
	blklev++;
	declist(0);
	if (r != regvar)
		outcode("BN", SETREG, regvar);
}

/*
 * After the end of a block, delete local
 * symbols; save those that are external.
 * Also complain about undefined labels.
 */
void blkend(void)
{
	register struct hshtab *cs, *ncs;
	struct hshtab *endcs;
	register int i;

	blklev--;
	for (cs=hshtab; cs->name[0] && cs<hshtab+HSHSIZ-1; ++cs)
		;
	endcs = cs;
	do  if (cs->name[0]) {
		if (cs->hblklev <= blklev)
			continue;
		if ((cs->hclass!=EXTERN || blklev!=0)
		 && ((cs->hflag&FLABL)==0 || blklev==0)) {
			if (cs->hclass==0)
				error("%.8s undefined", cs->name);
			if ((ncs = (struct hshtab *)cs->hpdown)==NULL) {
				cs->name[0] = '\0';
				hshused--;
				cs->hflag &= FKEYW;
			} else {
				cpysymb((struct phshtab *)cs, (struct phshtab *)ncs);
			}
			continue;
		}
		/*
		 * Retained name; must rehash.
		 */
		for (i=0; i<NCPS; i++)
			symbuf[i] = cs->name[i];
		mossym = cs->hflag&FMOS;
		lookup();
		if ((ncs=csym) != cs) {
			cs->name[0] = '\0';
			hshused--;
			i = ncs->hflag;
			cpysymb((struct phshtab *)ncs, (struct phshtab *)cs);
			ncs->hflag |= i&FKEYW;
			cs->hflag &= FKEYW;
		}
		if (ncs->hblklev>1 || (ncs->hblklev>0 && ncs->hclass==EXTERN))
			ncs->hblklev--;
	} while ((cs = (cs<&hshtab[HSHSIZ-1])? ++cs: hshtab) != endcs);
}

/*
 * write out special definitions of local symbols for
 * benefit of the debugger.  None of these are used
 * by the assembler except to save them.
 */
void prste(struct hshtab *acs)
{
	register struct hshtab *cs;
	register int nkind;

	cs = acs;
	switch (cs->hclass) {
	case REG:
		nkind = RNAME;
		break;

	case AUTO:
		nkind = ANAME;
		break;

	case STATIC:
		nkind = SNAME;
		break;

	default:
		return;

	}
	outcode("BSN", nkind, cs->name, cs->hoffset);
}

/*
 * In case of error, skip to the next
 * statement delimiter.
 */
void errflush(int ao)
{
	register int o;

	o = ao;
	while(o>RBRACE)	/* ; { } */
		o = symbol();
	peeksym  = o;
}
