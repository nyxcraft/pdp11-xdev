/*
 *  link editor
 *  modified to be a overlayed segmentation register loader by wnj 6/79
 *  touched up by bill jolitz 9/79.
 *  working with emt trap thunk fmt 5/80 bill jolitz.
 *  8-byte thunks, more touch-ups, mjk 9/81.
 */

#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>	/* fixed-width on-disk a.out/ar fields on LP64 host */
#include <unistd.h>	/* readlink for relocatable -l lib path */
#include <stdlib.h>	/* getenv for PDP11_UNIVERSE */
#include <string.h>	/* strcmp for the universe name->id map */
#include <fcntl.h>	/* open/creat for the object/library I/O */
#include <ar.h>		/* PDPL: PDP-11 middle-endian on-disk ar longs */
#include "universe.h"	/* era names for the lib/<universe>/ search + __univ id */

/* ld defines its own putw(word, struct buf *) for buffered output; the host's
 * <stdio.h> (which 2.9BSD's ld.c includes) also declares putw(int, FILE *),
 * a mismatch the LP64 host rejects.  Rename ours -- 2.9BSD's PDP-11 libc had
 * no such collision.  (ld reads via get(), so getw needs no shim.) */
#define putw ld_putw

/* The on-disk ar header (archdr) is a packed, middle-endian PDP-11 record; ld
 * reads/writes it word-wise via mget() through explicit (uint16_t *) casts as
 * part of the LP64 16-bit-I/O port.  Once mget() has a prototype (C99) the host
 * flags that packed->uint16_t* cast for possible unaligned access; the cast is
 * deliberate and the layout is fixed, so silence the note here.  archdr is the
 * only packed struct in this file, and x86-64 handles the 16-bit access. */
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

/*	Layout of a.out file :
 *
 *	header of 8 words	magic number 405, 407, 410, 411
 *				text size	)
 *				data size	) in bytes but even
 *				bss size	)
 *				symbol table size
 *				entry point
 *				{unused}
 *				flag set if no relocation
 *
 *
 *	header:		0
 *	text:		16
 *	data:		16+textsize
 *	relocation:	16+textsize+datasize
 *	symbol table:	16+2*(textsize+datasize) or 16+textsize+datasize
 *
 */
#define TRUE	1
#define FALSE	0


#define	ARCMAGIC 0177545
#define OMAGIC	0405
#define	FMAGIC	0407
#define	NMAGIC	0410
#define	IMAGIC	0411

#define	EXTERN	040
#define	UNDEF	00
#define	ABS	01
#define	TEXT	02
#define	DATA	03
#define	BSS	04
#define	COMM	05	/* internal use only */

#define	RABS	00
#define	RTEXT	02
#define	RDATA	04
#define	RBSS	06
#define	REXT	010

#define NOVLY	16
#define	RELFLG	01
#define	NROUT	256
#define	NSYM	1103
#define	NSYMPR	1000

char	premeof[] = "Premature EOF";
char	goodnm[] = "__.SYMDEF";

/* table of contents stuff */
#define TABSZ	700
struct tab
{	char cname[8];
	int32_t cloc;			/* on-disk __.SYMDEF entry: 12 bytes = 6 words */
} __attribute__((packed)) tab[TABSZ];
int tnum;


/* overlay management */
int	vindex;
struct overlay {
	int	argsav;
	int	symsav;
	struct liblist	*libsav;
	char	*vname;
	int	ctsav, cdsav, cbsav;
	int	offt, offd, offb, offs;
} vnodes[NOVLY];

/* input management */
struct page {
	int	nuser;
	int	bno;
	int	nibuf;
	uint16_t	buff[256];
} page[2];

struct {
	int	nuser;
	int	bno;
} fpage;

struct stream {
	uint16_t	*ptr;
	int	bno;
	int	nibuf;
	int	size;
	struct page	*pno;
};

struct stream text;
struct stream reloc;

struct {
	char	aname[14];
	int32_t	atime;
	char	auid, agid;
	int16_t	amode;
	int32_t	asize;
} __attribute__((packed)) archdr;		/* V7 ar header, 26 bytes = 13 words */

struct {
	uint16_t	fmagic;
	uint16_t	tsize;
	uint16_t	dsize;
	uint16_t	bsize;
	uint16_t	ssize;
	uint16_t	entry;
	uint16_t	pad;
	uint16_t	relflg;
} filhdr;					/* 8 words = 16 bytes */


/* one entry for each archive member referenced;
 * set in first pass; needs restoring for overlays
 */
struct liblist {
	long	loc;
};

struct liblist	liblist[NROUT];
struct liblist	*libp = liblist;


/* symbol management */
struct symbol {
	char	sname[8];
	char	stype;
	char	sovly;
	uint16_t	svalue;		/* on-disk 16-bit value */
	int	sovalue;		/* in-memory only (overlay); not written */
};

struct xsymbol {
	char	sname[8];
	char	stype;
	char	sovly;
	uint16_t	svalue;		/* sizeof cursym == 12 (on-disk symbol) */
};
struct local {
	int locindex;		/* index to symbol in file */
	struct symbol *locsymbol;	/* ptr to symbol table */
};

struct xsymbol	cursym;			/* current symbol */
struct symbol	symtab[NSYM];		/* actual symbols */
struct symbol	**symhash[NSYM];	/* ptr to hash table entry */
struct symbol	*lastsym;		/* last symbol entered */
int	symindex;		/* next available symbol table entry */
struct symbol	*hshtab[NSYM+2];	/* hash table for symbols */
struct local	local[NSYMPR];

/* internal symbols */
struct symbol	*p_etext;
struct symbol	*p_edata;
struct symbol	*p_end;
struct symbol	*entrypt;

int	trace;
/* flags */
int	xflag;		/* discard local symbols */
int	Xflag;		/* discard locals starting with 'L' */
int	Sflag;		/* discard all except locals and globals*/
int	rflag;		/* preserve relocation bits, don't define common */
int	arflag;		/* original copy of rflag */
int	sflag;		/* discard all symbols */
int	nflag;		/* pure procedure */
int	Oflag;		/* set magic # to 0405 (overlay) */
int	dflag;		/* define common even with rflag */
int	iflag;		/* I/D space separated */
int	vflag;		/* overlays used */

int	ofilfnd;
char	*ofilename = "l.out";
int	infil;
char	*filname;

/* cumulative sizes set in pass 1 */
int	tsize;
int	dsize;
int	bsize;
int	ssize;

/* symbol relocation; both passes */
int	ctrel;
int	cdrel;
int	cbrel;

int	errlev;
int	delarg	= 4;
char	tfname[] = "/tmp/ldaXXXXXX";	/* 6 X's: modern mktemp requires it */


/* output management */
struct buf {
	int	fildes;
	int	nleft;
	uint16_t	*xnext;
	uint16_t	iobuf[256];
};
struct buf	toutb;
struct buf	doutb;
struct buf	troutb;
struct buf	droutb;
struct buf	soutb;

/* wnj added for text overlay register */

#define NOVL		7	/* max number of overlays; this defines
				 * the header format and must agree with
				 * the kernel limit. */
#define THUNKSIZ	8

int	torgwas;		/* Saves torigin while doing overlays */
int	tsizwas;		/* Saves tsize while doing overlays */
int	numov;			/* Total number of overlays */
int	curov;			/* Overlay being worked on just now */
int	inov;			/* 1 if working on an overlay */
int	ovsize[NOVL+1];	/* The sizes of the overlays */

#define	max	ovsize[0]

struct buf	voutb;		/* Overlay text goes here */
/*
struct buf	dummyb;
*/

				/* Kernel overlays have a special
				   subroutine to do the switch */
struct	xsymbol ovhndlr =
	{ "ovhndlr1", EXTERN+UNDEF, 0, 0 };
#define	HNDLR_NUM 7	/* position of ov number in ovhndlr.sname[] */
int	ovbase;			/* The base address of the overlays */
/* end overlay stuff */

/* forward declarations -- C99 prototypes; every function here is file-local
 * (nothing in the separately-linked ucbpath objects references them) so they
 * are static.  openl/openlp/_concat are NOT used by this ld and so absent. */
static void	delexit(int sig);
static void	endload(int argc, char **argv);
static void	roundov(void);
static void	record(int c, char *nam);
static void	restore(int vscan);
static void	load1arg(char *acp);
static int	step(long nloc);
static int	ldrand(void);
static int	add(int a, int b, char *s);
static int	load1(int libflg, long loc);
static void	middle(void);
static void	ldrsym(struct symbol *asp, int val, int type);
static void	setupout(void);
static void	tcreat(struct buf *buf, int tempflg);
static void	load2arg(char *acp);
static void	load2(long loc);
static void	load2td(struct local *lp, int creloc, struct buf *b1, struct buf *b2);
static void	finishout(void);
static int	adrof(char *s);
static void	copy(struct buf *buf);
static void	mkfsym(char *s);
static void	mget(uint16_t *aloc, int an);
static void	mput(struct buf *buf, uint16_t *aloc, int an);
static void	dseek(struct stream *asp, long aloc, int s);
static int	half(int i);
static int	get(struct stream *asp);
static int	getfile(char *acp);
static struct symbol	**lookup(void);
static struct symbol	**slookup(char *s);
static int	enter(struct symbol **hp);
static void	symreloc(void);
static void	error(int n, char *s);
static struct symbol	*lookloc(struct local *alp, int r);
static void	readhdr(long loc);
static void	cp8c(char *from, char *to);
static int	eq(char *s1, char *s2);
static void	putw(int w, struct buf *b);
static void	flush(struct buf *b);

static void
delexit(int sig)
{
	unlink("l.out");
	if (delarg==0)
		chmod(ofilename, 0777 & ~umask(0));
	exit(delarg);
}

int
main(int argc, char **argv)
{
	register int c, i;
	int num;
	register char *ap, **p;
	int found; 
	int vscan; 
	char save;

	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		signal(SIGINT, delexit);
	if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
		signal(SIGTERM, delexit);
	if (argc == 1)
		exit(4);
	p = argv+1;

	/* scan files once to find symdefs */
	for (c=1; c<argc; c++) {
		if (trace) printf("%s:\n", *p);
		filname = 0;
		ap = *p++;

		if (*ap == '-') {
			for (i=1; ap[i]; i++) {
			switch (ap[i]) {
			case 'o':
				if (++c >= argc)
					error(2, "Bad output file");
				ofilename = *p++;
				ofilfnd++;
				continue;

			case 'u':
			case 'e':
				if (++c >= argc)
					error(2, "Bad 'use' or 'entry'");
				enter(slookup(*p++));
				if (ap[i]=='e')
					entrypt = lastsym;
				continue;

			case 'v':
				if (++c >= argc)
					error(2, "-v: arg missing");
				vflag=TRUE;
				vscan = vindex; 
				found=FALSE;
				while (--vscan>=0 && found==FALSE)
					found = eq(vnodes[vscan].vname, *p);
				if (found) {
					endload(c, argv);
					restore(vscan);
				} else
					record(c, *p);
				p++;
				continue;

			case 'D':
				if (++c >= argc)
					error(2, "-D: arg missing");
				num = atoi(*p++);
				if (dsize>num)
					error(2, "-D: too small");
				dsize = num;
				continue;

			case 'l':
				save = ap[--i]; 
				ap[i]='-';
				load1arg(&ap[i]); 
				ap[i]=save;
				break;

			case 'x':
				xflag++;
				continue;

			case 'X':
				Xflag++;
				continue;

			case 'S':
				Sflag++; 
				continue;

			case 'r':
				rflag++;
				arflag++;
				continue;

			case 's':
				sflag++;
				xflag++;
				continue;

			case 'n':
				nflag++;
				continue;

			case 'd':
				dflag++;
				continue;

			case 'i':
				iflag++;
				continue;

			case 'O':
				Oflag++;
				continue;

			case 't':
				trace++;
				continue;

/* wnj added for overlay text registers */
			case 'Z':
				if (!inov) {
					tsizwas = tsize;
					if (numov == 0) {
						cursym = ovhndlr;
						enter(lookup());
					}
				} else {
					ovsize[curov] = tsize;
					if (trace)
					printf("overlay %d size %d\n", curov, ovsize[curov]);
				}
				tsize = 0;
				inov = 1;
				numov++;
				if (numov > NOVL)
					error(2, "Too many overlays, limit 7");
				curov++;
				continue;

			case 'L':
				if (inov == 0)
					error(2, "-L: Not in overlay");
				ovsize[curov] = tsize;
				if (trace)
				printf("overlay %d size %d\n", curov, ovsize[curov]);
				curov = inov = 0;
				tsize = tsizwas;
				continue;
/* end overlay text addition */

			default:
				error(2, "bad flag");
			} /*endsw*/
			break;
			} /*endfor*/
		} else
			load1arg(ap);
	}
	endload(argc, argv);
}

/* used after pass 1 */
int	nsym;
int	torigin;
int	dorigin;
int	borigin;

static void
endload(int argc, char **argv)
{
	register int c, i;
	int dnum;
	register char *ap, **p;

	if (numov)
		rflag = 0;
	filname = 0;
	middle();
	setupout();
	p = argv+1;
	libp = liblist;
	for (c=1; c<argc; c++) {
		ap = *p++;
		if (trace) printf("%s:\n", ap);
		if (*ap == '-') {
			for (i=1; ap[i]; i++) {
			switch (ap[i]) {
			case 'D':
				for (dnum = atoi(*p); dorigin<dnum; dorigin += 2) {
					putw(0, &doutb);
					if (rflag)
						putw(0, &droutb);
				}
			case 'u':
			case 'e':
			case 'o':
			case 'v':
				++c; 
				++p;

			default:
				continue;

			case 'l':
				ap[--i]='-'; 
				load2arg(&ap[i]);
				break;

/* wnj added for overlay text segmentation registers */
			case 'Z':
				if (inov == 0)
					torgwas = torigin;
				else
					roundov();
				torigin = ovbase;
				inov = 1;
				curov++;
				continue;

			case 'L':
				roundov();
				inov = 0;
				if (trace)
					printf("end overlay generation\n");
				torigin = torgwas;
				continue;
/* end wnj added for text overlay registers */
			} /*endsw*/
			break;
			} /*endfor*/
		} else
			load2arg(ap);
	}
	finishout();
}

static void
roundov(void)
{

	while (torigin & 077) {
		putw(0, &voutb);
		torigin += 2;		/* a PDP-11 word (LP64: sizeof(int)==4 looped forever) */
	}
}

static void
record(int c, char *nam)
{
	register struct overlay *v;

	v = &vnodes[vindex++];
	v->argsav = c;
	v->symsav = symindex;
	v->libsav = libp;
	v->vname = nam;
	v->offt = tsize; 
	v->offd = dsize; 
	v->offb = bsize; 
	v->offs = ssize;
	v->ctsav = ctrel; 
	v->cdsav = cdrel; 
	v->cbsav = cbrel;
}

static void
restore(int vscan)
{
	register struct overlay *v;
	register int saved;

	v = &vnodes[vscan];
	vindex = vscan+1;
	libp = v->libsav;
	ctrel = v->ctsav; 
	cdrel = v->cdsav; 
	cbrel = v->cbsav;
	tsize = v->offt; 
	dsize = v->offd; 
	bsize = v->offb; 
	ssize = v->offs;
	saved = v->symsav;
	while (symindex>saved)
		*symhash[--symindex]=0;
}

/* scan file to find defined symbols */
static void
load1arg(char *acp)
{
	register char *cp;
	long nloc;

	cp = acp;
	switch ( getfile(cp)) {
	case 0:
		load1(0, 0L);
		break;

	/* regular archive */
	case 1:
		nloc = 1;
		while ( step(nloc))
			nloc += (archdr.asize + sizeof(archdr) + 1) >> 1;
		break;

	/* table of contents */
	case 2:
		tnum = archdr.asize / sizeof(struct tab);
		if (tnum >= TABSZ) {
			error(2, "fast load buffer too small");
		}
		lseek(infil, (long)(sizeof(filhdr.fmagic)+sizeof(archdr)), 0);
		if (read(infil, (char *)tab, tnum * sizeof(struct tab))) {}
		{ int _i; for(_i=0;_i<tnum;_i++) tab[_i].cloc = PDPL(tab[_i].cloc); }
		while (ldrand());
		libp->loc = -1;
		libp++;
		break;
	/* out of date table of contents */
	case 3:
		error(0, "out of date (warning)");
		for(nloc = 1+((archdr.asize+sizeof(archdr)+1) >> 1); step(nloc);
			nloc += (archdr.asize + sizeof(archdr) + 1) >> 1);
		break;
	}
	close(infil);
}

static int
step(long nloc)
{
	dseek(&text, nloc, sizeof archdr);
	if (text.size <= 0) {
		libp->loc = -1;
		libp++;
		return(0);
	}
	mget((uint16_t *)&archdr, sizeof archdr);
	archdr.asize = PDPL(archdr.asize); archdr.atime = PDPL(archdr.atime);
	if (load1(1, nloc + (sizeof archdr) / 2)) {
		libp->loc = nloc;
		libp++;
	}
	return(1);
}

static int
ldrand(void)
{
	int i;
	struct symbol *sp, **pp;
	struct liblist *oldp = libp;
	for(i = 0; i<tnum; i++) {
		if ((pp = slookup(tab[i].cname)) == 0)
			continue;
		sp = *pp;
		if (sp == 0)		/* empty hash slot: this __.SYMDEF symbol is
					 * not referenced.  On the PDP-11 the next line
					 * read address 0 harmlessly; guard it here. */
			continue;
		if (sp->stype != EXTERN+UNDEF)
			continue;
		step(tab[i].cloc >> 1);
	}
	return(oldp != libp);
}

static int
add(int a, int b, char *s)
{
	long r;

	r = (long)(unsigned)a + (unsigned)b;
	if (r >= 0200000)
		error(1,s);
	return(r);
}


/* single file or archive member */
static int
load1(int libflg, long loc)
{
	register struct symbol *sp;
	int savindex;
	int ndef, nloc, type, mtype;

	readhdr(loc);
	ctrel = tsize;
	cdrel += dsize;
	cbrel += bsize;
	ndef = 0;
	nloc = sizeof cursym;
	savindex = symindex;
	if ((filhdr.relflg&RELFLG)==1) {
		error(1, "No relocation bits");
		return(0);
	}
	loc += (sizeof filhdr)/2 + filhdr.tsize + filhdr.dsize;
	dseek(&text, loc, filhdr.ssize);
	while (text.size > 0) {
		mget((uint16_t *)&cursym, sizeof cursym);
		type = cursym.stype;
		if (Sflag) {
			mtype = type&037;
			if (mtype==1 || mtype>4) {
				continue;
			}
		}
		if ((type&EXTERN)==0) {
			if (Xflag==0 || cursym.sname[0]!='L')
				nloc += sizeof cursym;
			continue;
		}
		symreloc();
		if (enter(lookup()))
			continue;
		if ((sp = lastsym)->stype != EXTERN+UNDEF)
			continue;
		if (cursym.stype == EXTERN+UNDEF) {
			if (cursym.svalue > sp->svalue)
				sp->svalue = cursym.svalue;
			continue;
		}
		if (sp->svalue != 0 && cursym.stype == EXTERN+TEXT)
			continue;
		ndef++;
		sp->stype = cursym.stype;
		sp->svalue = cursym.svalue;
		if ((sp->stype &~ EXTERN) == TEXT)
			sp->sovly = curov;
		if (trace)
		printf("found %8.8s in overlay %d at %d\n", sp->sname, sp->sovly, sp->svalue);
	}
	if (libflg==0 || ndef) {
		tsize = add(tsize,filhdr.tsize,"text overflow");
		dsize = add(dsize,filhdr.dsize,"data overflow");
		bsize = add(bsize,filhdr.bsize,"bss overflow");
		ssize = add(ssize,nloc,"symbol table overflow");
		return(1);
	}
	/*
	 * No symbols defined by this library member.
	 * Rip out the hash table entries and reset the symbol table.
	 */
	while (symindex>savindex)
		*symhash[--symindex]=0;
	return(0);
}

/*
 * Map the active PDP11_UNIVERSE name to its numeric era id (see universe.h).
 * ld stamps this value as the absolute symbol __univ so that ONE universal
 * libc.a can pick an era's behaviour at run time: crt0 copies __univ into a
 * data cell the library reads, and any routine whose ABI moved branches on it.
 * The era chosen at LINK is thus recorded in the executable, deterministically,
 * with no kernel probing.  Unset/unknown falls back to the default universe.
 */
static int
p11_univ_id()
{
	char *u = getenv("PDP11_UNIVERSE");

	if (u == 0 || *u == 0)
		u = PDP11_UNIV_DEFAULT_NAME;
#define X(nm, uid, status, desc)	if (strcmp(u, #nm) == 0) return (uid);
	PDP11_UNIVERSES(X)
#undef X
	return (PDP11_UNIV_BSD29);
}

/* The First Edition family -- v1 and v2 -- shares the 0405 a.out format
 * (loaded whole at 040000, code at 040014) and the 1971 inline-arg traps, so
 * ld emits that format for both.  (v3+ are V5/V6-personality, the V7 line.) */
static int
p11_firsted()
{
	int id = p11_univ_id();
	return id == PDP11_UNIV_V1 || id == PDP11_UNIV_V2;
}

static void
middle(void)
{
	register struct symbol *sp, *symp;
	register int t, csize;
	int nund, corigin;
	int ttsize;

	/* First Edition (0405) links whole at core 040000 with a 12-byte header,
	 * so text runs from 040014; every text/data address is that origin + off.
	 * (v1out is set in setupout from the active universe.) */
	torigin = p11_firsted() ? 040014 : 0;
	dorigin=0;
	borigin=0;

	p_etext = *slookup("_etext");
	p_edata = *slookup("_edata");
	p_end = *slookup("_end");
	/*
	 * Stamp the linked-in universe as an absolute __univ, but ONLY in a
	 * final link (not an explicit -r) and BEFORE the undefined scan below:
	 * a referenced __univ must already be a defined absolute, or that scan
	 * would treat it as a straggler and force the link relocatable.  Defined
	 * only if referenced (by crt0); a disagreeing input definition trips
	 * ldrsym's "Multiply defined".
	 */
	if (rflag == 0)
		ldrsym(*slookup("__univ"), p11_univ_id(), EXTERN+ABS);
	/*
	 * If there are any undefined symbols, save the relocation bits.
	 * (Unless we are overlaying.)
	 */
	symp = &symtab[symindex];
	if (rflag==0 && !numov) {
		for (sp = symtab; sp<symp; sp++)
			if (sp->stype==EXTERN+UNDEF && sp->svalue==0
				&& sp!=p_end && sp!=p_edata && sp!=p_etext) {
				rflag++;
				dflag = 0;
				break;
			}
	}
	if (rflag)
		nflag = sflag = iflag = Oflag = 0;
	/*
	 * Assign common locations.
	 */
	csize = 0;
	if (dflag || rflag==0) {
		ldrsym(p_etext, tsize, EXTERN+TEXT);
		ldrsym(p_edata, dsize, EXTERN+DATA);
		ldrsym(p_end, bsize, EXTERN+BSS);
		for (sp = symtab; sp<symp; sp++)
			if (sp->stype==EXTERN+UNDEF && (t = sp->svalue)!=0) {
				t = (t+1) & ~01;
				sp->svalue = csize;
				sp->stype = EXTERN+COMM;
				csize = add(csize, t, "bss overflow");
			}
	}
/* wnj added for overlay text */
	if (numov) {
		for (sp = symtab; sp < symp; sp++) {
			if (trace)
			printf("%8.8s stype %o svalue %o sovalue %o sovly %d\n",
			   sp->sname, sp->stype, sp->svalue, sp->sovalue, sp->sovly);
			if (sp->sovly && sp->stype == EXTERN+TEXT) {
				sp->sovalue = sp->svalue;
				sp->svalue = tsize;
				tsize += THUNKSIZ;
				if (trace)
					printf("relocating %s in overlay %d from %o to %o\n",
					    sp->sname, sp->sovly,
					    sp->sovalue, sp->svalue);
			}
		}
	}
/* end wnj added */
	/*
	 * Now set symbols to their final value
	 */
	if (nflag || iflag)
		tsize = (tsize + 077) & ~077;
/* wnj added */
	ttsize = tsize;
	if (numov) {
		register int i;

		ovbase = (tsize + 017777) &~ 017777;
		if (trace)
			printf("overlay base is %d.\n", ovbase);
		for (sp = symtab; sp < symp; sp++)
			if (sp->sovly && sp->stype == EXTERN+TEXT) {
				sp->sovalue += ovbase;
				if (trace)
					printf("%.8s at %d overlay %d\n", sp->sname, sp->sovalue, sp->sovly);
			}
		for (i = 1; i < 8; i++) {
			ovsize[i] = (ovsize[i] + 077) &~ 077;
			if (ovsize[i] > max)
				max = ovsize[i];
		}
		if (trace)
			printf("maximum overlay size is %d.\n", max);
		ttsize = ovbase + max;
		ttsize = (ttsize + 017777) &~ 017777;
		if (trace)
			printf("overlays end before %u.\n", ttsize);
	}
/* end wnj added */
	dorigin = ttsize + torigin;	/* +torigin: 0 normally, 040014 for First Edition */
	if (nflag)
		dorigin = (ttsize+017777) & ~017777;
	if (iflag)
		dorigin = 0;
	corigin = dorigin + dsize;
	borigin = corigin + csize;
	nund = 0;
	for (sp = symtab; sp<symp; sp++) switch (sp->stype) {
	case EXTERN+UNDEF:
		errlev |= 01;
		if (arflag==0 && sp->svalue==0) {
			if (nund==0)
				printf("Undefined:\n");
			nund++;
			printf("%.8s\n", sp->sname);
		}
		continue;

	case EXTERN+ABS:
	default:
		continue;

	case EXTERN+TEXT:
		sp->svalue += torigin;
		continue;

	case EXTERN+DATA:
		sp->svalue += dorigin;
		continue;

	case EXTERN+BSS:
		sp->svalue += borigin;
		continue;

	case EXTERN+COMM:
		sp->stype = EXTERN+BSS;
		sp->svalue += corigin;
		continue;
	}
	if (sflag || xflag)
		ssize = 0;
	bsize = add(bsize, csize, "bss overflow");
	nsym = ssize / (sizeof cursym);
}

static void
ldrsym(struct symbol *asp, int val, int type)
{
	register struct symbol *sp;

	if ((sp = asp) == 0)
		return;
	if (sp->stype != EXTERN+UNDEF || sp->svalue) {
		printf("%.8s: ", sp->sname);
/* 		if (trace) */
			printf("svalue %o ", sp->svalue);
		error(1, "Multiply defined");
		return;
	}
	sp->stype = type;
	sp->svalue = val;
}

static void
setupout(void)
{
	int v1out = p11_firsted();	/* First Edition (0405) output: v1, v2 */

	if (v1out)
		sflag = 1;		/* First Edition exes here carry no symtab */
	tcreat(&toutb, 0);
	{ int fd = mkstemp(tfname); if (fd >= 0) close(fd); }	/* reserve the name (tcreat makes the file) */
	tcreat(&doutb, 1);
	if (sflag==0 || xflag==0)
		tcreat(&soutb, 1);
	if (rflag) {
		tcreat(&troutb, 1);
		tcreat(&droutb, 1);
	}
/* wnj added */
	if (numov)
		tcreat(&voutb, 1);
/* end wnj added */
	filhdr.fmagic = (Oflag ? OMAGIC :( iflag ? IMAGIC : ( nflag ? NMAGIC : FMAGIC )));
	if (numov) {
		if (filhdr.fmagic == FMAGIC)
			error(2, "-n or -i must be used for overlays");
		filhdr.fmagic |= 020;
	}
	filhdr.tsize = tsize;
	filhdr.dsize = dsize;
	filhdr.bsize = bsize;
	filhdr.ssize = sflag? 0: (ssize + (sizeof cursym)*symindex);
	if (entrypt) {
		if (entrypt->stype!=EXTERN+TEXT)
			error(1, "Entry point not in text");
		else if (entrypt->sovly)
			error(1, "Entry point in overlay");
		else
			filhdr.entry = entrypt->svalue | 01;
	} else
		filhdr.entry=0;
	filhdr.pad = 0;
	filhdr.relflg = (rflag==0);
	if (v1out) {
		/* First Edition 6-word header: word 0 is 0405, which is also the
		 * instruction `br .+14' that jumps over the 12-byte header to the
		 * code at 040014.  [magic | 12+text+data | symtab | reloc | bss | 0];
		 * apsim loads word-1 bytes at core 040000 and enters via the br. */
		uint16_t h[6];
		h[0]=0405; h[1]=12+tsize+dsize; h[2]=0; h[3]=0; h[4]=bsize; h[5]=0;
		mput(&toutb, h, sizeof h);
	} else
		mput(&toutb, (uint16_t *)&filhdr, sizeof filhdr);
/* wnj added */
	if (numov) {
		register int i;
		for (i = 0; i < 8; i++)
			putw(ovsize[i], &toutb);
	}
/* end wnj */
}

static void
tcreat(struct buf *buf, int tempflg)
{
	register int ufd;
	char *nam;
	nam = (tempflg ? tfname : ofilename);
	if ((ufd = creat(nam, 0666)) < 0)
		error(2, tempflg?"cannot create temp":"cannot create output");
	close(ufd); 
	buf->fildes = open(nam, 2);
	if (tempflg)
		unlink(tfname);
	buf->nleft = sizeof(buf->iobuf)/sizeof(*buf->iobuf);
	buf->xnext = buf->iobuf;
}

static void
load2arg(char *acp)
{
	register char *cp;
	register struct liblist *lp;

	cp = acp;
	if (getfile(cp) == 0) {
		while (*cp)
			cp++;
		while (cp >= acp && *--cp != '/');
		mkfsym(++cp);
		load2(0L);
	} else {	/* scan archive members referenced */
		for (lp = libp; lp->loc != -1; lp++) {
			dseek(&text, lp->loc, sizeof archdr);
			mget((uint16_t *)&archdr, sizeof archdr);
	archdr.asize = PDPL(archdr.asize); archdr.atime = PDPL(archdr.atime);
			mkfsym(archdr.aname);
			load2(lp->loc + (sizeof archdr) / 2);
		}
		libp = ++lp;
	}
	close(infil);
}

static void
load2(long loc)
{
	register struct symbol *sp;
	register struct local *lp;
	register int symno;
	int type, mtype;

	readhdr(loc);
	ctrel = torigin;
	cdrel += dorigin;
	cbrel += borigin;
	/*
	 * Reread the symbol table, recording the numbering
	 * of symbols for fixing external references.
	 */
	lp = local;
	symno = -1;
	loc += (sizeof filhdr)/2;
	dseek(&text, loc + filhdr.tsize + filhdr.dsize, filhdr.ssize);
	while (text.size > 0) {
		symno++;
		mget((uint16_t *)&cursym, sizeof cursym);
		symreloc();
		type = cursym.stype;
		if (Sflag) {
			mtype = type&037;
			if (mtype==1 || mtype>4) continue;
		}
		if ((type&EXTERN) == 0) {
			if (!sflag&&!xflag&&(!Xflag||cursym.sname[0]!='L')) {
				/* preserve overlay number for locals */
				/* mostly for adb.   mjk 7/81 */
				if ((type==TEXT) && inov)
					cursym.sovly = curov;
				mput(&soutb, (uint16_t *)&cursym, sizeof cursym);
			}
			continue;
		}
		if ((sp = *lookup()) == 0)
			error(2, "internal error: symbol not found");
		/*
		 * Bill Shannon's fix to the 'local symbol botch'
		 * message. -wfj 5/80
		 */
		if (cursym.stype == EXTERN+UNDEF || cursym.stype == EXTERN+TEXT)
		{
/*
		if (cursym.stype == EXTERN+UNDEF) {
*/
			if (lp >= &local[NSYMPR])
				error(2, "Local symbol overflow");
			lp->locindex = symno;
			lp++->locsymbol = sp;
			continue;
		}
		if (cursym.stype!=sp->stype
		    || cursym.svalue!=sp->svalue && !sp->sovly
		    || sp->sovly && cursym.svalue!=sp->sovalue) {
			printf("%.8s: ", cursym.sname);
 			if (trace) {
				printf(" sovly %d sovalue %o ", sp->sovly, sp->sovalue);
				printf("new %o hav %o ", cursym.svalue, sp->svalue);
 			}
			error(1, "Multiply defined");
		}
	}
	dseek(&text, loc, filhdr.tsize);
	dseek(&reloc, loc + half(filhdr.tsize + filhdr.dsize), filhdr.tsize);
	load2td(lp, ctrel, inov ? &voutb : &toutb, &troutb);
	dseek(&text, loc+half(filhdr.tsize), filhdr.dsize);
	dseek(&reloc, loc+filhdr.tsize+half(filhdr.dsize), filhdr.dsize);
	load2td(lp, cdrel, &doutb, &droutb);
	torigin += filhdr.tsize;
	dorigin += filhdr.dsize;
	borigin += filhdr.bsize;
}

static void
load2td(struct local *lp, int creloc, struct buf *b1, struct buf *b2)
{
	register int r, t;
	register struct symbol *sp;

	for (;;) {
		/*
			 * The pickup code is copied from "get" for speed.
			 */

		/* next text or data word */
		if (--text.size <= 0) {
			if (text.size < 0)
				break;
			text.size++;
			t = get(&text);
		} else if (--text.nibuf < 0) {
			text.nibuf++;
			text.size++;
			t = get(&text);
		} else
			t = *text.ptr++;

		/* next relocation word */
		if (--reloc.size <= 0) {
			if (reloc.size < 0)
				error(2, "Relocation error");
			reloc.size++;
			r = get(&reloc);
		} else if (--reloc.nibuf < 0) {
			reloc.nibuf++;
			reloc.size++;
			r = get(&reloc);
		} else
			r = *reloc.ptr++;

		switch (r&016) {

		case RTEXT:
			t += ctrel;
			break;

		case RDATA:
			t += cdrel;
			break;

		case RBSS:
			t += cbrel;
			break;

		case REXT:
			sp = lookloc(lp, r);
			if (sp->stype==EXTERN+UNDEF) {
				r = (r&01) + ((nsym+(sp-symtab))<<4) + REXT;
				break;
			}
			t += sp->svalue;
			r = (r&01) + ((sp->stype-(EXTERN+ABS))<<1);
			break;
		}
		if (r&01)
			t -= creloc;
		putw(t, b1);
		if (rflag)
			putw(r, b2);
	}
}

static void
finishout(void)
{
	register int n;
	register uint16_t *p;
	register struct symbol *sp, *symp;

/* wnj added */
	if (numov) {
		int aovno = adrof("__ovno");
		int aovhndlr[NOVL+1];
		for (n=1; n<=numov; n++) {
			ovhndlr.sname[HNDLR_NUM] = '0' + n;
			aovhndlr[n] = adrof(ovhndlr.sname);
		}
		symp = &symtab[symindex];
		for (sp = symtab; sp < symp; sp++)
			if (sp->sovly && (sp->stype & (EXTERN+TEXT))) {
				putw(012701, &toutb);	/* mov $~foo+4, r1*/
				putw(sp->sovalue+4, &toutb);
				putw(04537, &toutb); /* jsr r5,ovhndlrx */
				putw(aovhndlr[sp->sovly], &toutb);
				torigin += THUNKSIZ;
			}
	}
/* end wnj */
	if (nflag||iflag) {
		n = torigin;
		while (n&077) {
			n += 2;
			putw(0, &toutb);
			if (rflag)
				putw(0, &troutb);
		}
	}
	if (numov)
		copy(&voutb);
	copy(&doutb);
	if (rflag) {
		copy(&troutb);
		copy(&droutb);
	}
	if (sflag==0) {
		if (xflag==0)
			copy(&soutb);
		for (p = (uint16_t *)symtab; p < (uint16_t *)&symtab[symindex];) {
/* wnj changed.... this is bad machine dependent code... */
			/* this does the symbol */
			putw(*p++, &toutb); putw(*p++, &toutb); 
			putw(*p++, &toutb); putw(*p++, &toutb); 
			/* these do the flags and value */
			putw(*p++, &toutb); putw(*p++, &toutb); 
			/* skip sovalue (int, 2 host words) -- not on disk */
			p += 2;
		}
/* end wnj changed */
	}
	flush(&toutb);
	close(toutb.fildes);
	if (!ofilfnd) {
		unlink("a.out");
		if (link("l.out", "a.out")) {}
		ofilename = "a.out";
	}
	delarg = errlev;
	delexit(0);
}

/* wnj added for overlay txt regs */
static int
adrof(char *s)
{
	register struct symbol **p = slookup(s);

	if (*p == 0) {
		printf("%.8s: ", s);
		error(1, "Undefined!");
		return (0);
	}
	return ((*p)->svalue);
}
/* end wnj added */

static void
copy(struct buf *buf)
{
	register int f, n;
	register uint16_t *p;

	flush(buf);
	lseek(f = buf->fildes, (long)0, 0);
	while ((n = read(f, (char *)buf->iobuf, sizeof(buf->iobuf))) > 1) {
		n >>= 1;
		p = (uint16_t *)buf->iobuf;
		do
			putw(*p++, &toutb);
		while (--n);
	}
	close(f);
}

static void
mkfsym(char *s)
{

	if (sflag || xflag)
		return;
	cp8c(s, cursym.sname);
	cursym.stype = 037;
	cursym.svalue = torigin;
	mput(&soutb, (uint16_t *)&cursym, sizeof cursym);
}

static void
mget(uint16_t *aloc, int an)
{
	register uint16_t *loc, *p;
	register int n;

	n = an;
	n >>= 1;
	loc = aloc;
	if ((text.nibuf -= n) >= 0) {
		if ((text.size -= n) > 0) {
			p = text.ptr;
			do
				*loc++ = *p++;
			while (--n);
			text.ptr = p;
			return;
		} else
			text.size += n;
	}
	text.nibuf += n;
	do {
		*loc++ = get(&text);
	} 
	while (--n);
}

static void
mput(struct buf *buf, uint16_t *aloc, int an)
{
	register uint16_t *loc;
	register int n;

	loc = aloc;
	n = an>>1;
	do {
		putw(*loc++, buf);
	} 
	while (--n);
}

static void
dseek(struct stream *asp, long aloc, int s)
{
	register struct stream *sp;
	register struct page *p;
	/* register */ long b, o;
	int n;

	b = aloc >> 8;
	o = aloc & 0377;
	sp = asp;
	--sp->pno->nuser;
	if ((p = &page[0])->bno!=b && (p = &page[1])->bno!=b)
		if (p->nuser==0 || (p = &page[0])->nuser==0) {
			if (page[0].nuser==0 && page[1].nuser==0)
				if (page[0].bno < page[1].bno)
					p = &page[0];
			p->bno = b;
			lseek(infil, (aloc & ~0377L) << 1, 0);
			if ((n = read(infil, (char *)p->buff, 512)>>1) < 0)
				n = 0;
			p->nibuf = n;
	} else
		error(2, "No pages");
	++p->nuser;
	sp->bno = b;
	sp->pno = p;
	sp->ptr = p->buff + o;
	if (s != -1)
		sp->size = half(s);
	if ((sp->nibuf = p->nibuf-o) <= 0)
		sp->size = 0;
}

static int
half(int i)
{
	return((i>>1)&077777);
}

static int
get(struct stream *asp)
{
	register struct stream *sp;

	sp = asp;
	if (--sp->nibuf < 0) {
		dseek(sp, (long)(sp->bno + 1) << 8, -1);
		--sp->nibuf;
	}
	if (--sp->size <= 0) {
		if (sp->size < 0)
			error(2, premeof);
		++fpage.nuser;
		--sp->pno->nuser;
		sp->pno = (struct page *)&fpage;
	}
	return(*sp->ptr++);
}

static int
getfile(char *acp)
{
	register char *cp;
	register int c;
	struct stat x;

	cp = acp; 
	infil = -1;
	archdr.aname[0] = '\0';
	filname = cp;
	/* The library-path buffers below form a cyclic sprintf dependency
	 * (libpath -> libdir via "%slib", then libdir -> libpath via
	 * "%s/lib%s.a"), and udir must stay the same size as libdir so the
	 * strcpy(libdir, udir) back-copy cannot overflow.  No capacity-only
	 * enlargement can clear every -Wformat-overflow here (the two ends of
	 * the cycle impose opposite size constraints), and the format strings
	 * and arguments must not change, so scope the diagnostic off for this
	 * block only.  Behaviour and buffer sizes are unchanged. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-overflow"
	if (cp[0]=='-' && cp[1]=='l') {
		static char libpath[1024], libdir[1024];
		if(cp[2] == '\0')
			cp = "-la";
		/* Resolve the library directory relative to the ld binary via
		 * /proc/self/exe, like cc/cpp: .../usr/bin/<prefix>-ld ->
		 * .../usr/lib/lib<x>.a.  (2.9BSD scribbled the name into a string
		 * literal -- "/usr/lib/libxxx..." -- which crashes on modern
		 * read-only-string hosts; build it in a buffer instead.) */
		strcpy(libdir, "/usr/lib");
		{ int n = readlink("/proc/self/exe", libpath, sizeof libpath - 1);
		  if (n > 0) {
			char *sl; libpath[n] = '\0';
			for (sl = libpath+n; sl > libpath && sl[-1] != '/'; sl--);
			if (sl-libpath >= 4 &&
			    sl[-4]=='b' && sl[-3]=='i' && sl[-2]=='n' && sl[-1]=='/') {
				sl[-4] = '\0';
				sprintf(libdir, "%slib", libpath);
			}
		  }
		}
		/* era libraries live in lib/<universe>/ next to the bin dir
		 * (see universe.h); fall back to the flat lib/ if absent */
		{ char *univ = getenv("PDP11_UNIVERSE"); struct stat ub;
		  char udir[1024];
		  if (univ == 0 || *univ == 0)
			univ = PDP11_UNIV_DEFAULT_NAME;
		  if (strlen(libdir) + strlen(univ) + 2 < sizeof udir) {
			sprintf(udir, "%s/%s", libdir, univ);
			if (stat(udir, &ub) == 0)
				strcpy(libdir, udir);
		  }
		}
		sprintf(libpath, "%s/lib%s.a", libdir, cp + 2);
		filname = libpath;
		infil = open(filname, 0);
	}
#pragma GCC diagnostic pop
	if (infil == -1 && (infil = open(filname, 0)) < 0)
		error(2, "cannot open");
	page[0].bno = page[1].bno = -1;
	page[0].nuser = page[1].nuser = 0;
	text.pno = reloc.pno = (struct page *)&fpage;
	fpage.nuser = 2;
	dseek(&text, 0L, 2);
	if (text.size <= 0)
		error(2, premeof);
	if(get(&text) != ARCMAGIC)
		return(0);	/* regualr file */
	dseek(&text, 1L, sizeof archdr);	/* word addressing */
	if(text.size <= 0)
		return(1);	/* regular archive */
	mget((uint16_t *)&archdr, sizeof archdr);
	archdr.asize = PDPL(archdr.asize); archdr.atime = PDPL(archdr.atime);
	if(strncmp(archdr.aname, goodnm, 14) != 0)
		return(1);	/* regular archive */
	else {
		fstat(infil, &x);
		if(x.st_mtime > archdr.atime)
		{
			return(3);
		}
		else return(2);
	}
}

static struct symbol **
lookup(void)
{
	int i;
	int clash;
	register struct symbol **hp;
	register char *cp, *cp1;

	i = 0;
	for (cp = cursym.sname; cp < &cursym.sname[8];)
		i = (i<<1) + *cp++;
	for (hp = &hshtab[(i&077777)%NSYM+2]; *hp!=0;) {
		cp1 = (*hp)->sname; 
		clash=FALSE;
		for (cp = cursym.sname; cp < &cursym.sname[8];)
			if (*cp++ != *cp1++) {
				clash=TRUE; 
				break;
			}
		if (clash) {
			if (++hp >= &hshtab[NSYM+2])
				hp = hshtab;
		} else
			break;
	}
	return(hp);
}

static struct symbol **
slookup(char *s)
{
	cp8c(s, cursym.sname);
	cursym.stype = EXTERN+UNDEF;
	cursym.svalue = 0;
	return(lookup());
}

static int
enter(struct symbol **hp)
{
	register struct symbol *sp;

	if (*hp==0) {
		if (symindex>=NSYM)
			error(2, "Symbol table overflow");
		symhash[symindex] = hp;
		*hp = lastsym = sp = &symtab[symindex++];
		cp8c(cursym.sname, sp->sname);
		sp->stype = cursym.stype;
		sp->svalue = cursym.svalue;
		if (sp->stype == EXTERN+TEXT) {
			sp->sovly = curov;
			if (trace)
				printf("found %8.8s in overlay %d at %d\n",
				    sp->sname, sp->sovly, sp->svalue);
		}
		return(1);
	} else {
		lastsym = *hp;
		return(0);
	}
}

static void
symreloc(void)
{
	switch (cursym.stype) {

	case TEXT:
	case EXTERN+TEXT:
		cursym.svalue += ctrel;
		return;

	case DATA:
	case EXTERN+DATA:
		cursym.svalue += cdrel;
		return;

	case BSS:
	case EXTERN+BSS:
		cursym.svalue += cbrel;
		return;

	case EXTERN+UNDEF:
		return;
	}
	if (cursym.stype&EXTERN)
		cursym.stype = EXTERN+ABS;
}

static void
error(int n, char *s)
{
	if (errlev==0)
		printf("ld:");
	if (filname) {
		printf("%s", filname);
		if (archdr.aname[0])
			printf("(%.14s)", archdr.aname);
		printf(": ");
	}
	printf("%s\n", s);
	if (n > 1)
		delexit(0);
	errlev = n;
}

static struct symbol *
lookloc(struct local *alp, int r)
{
	register struct local *clp, *lp;
	register int sn;

	lp = alp;
	sn = (r>>4) & 07777;
	for (clp = local; clp<lp; clp++)
		if (clp->locindex == sn)
			return(clp->locsymbol);
	error(2, "Local symbol botch");
}

static void
readhdr(long loc)
{
	register int st, sd;

	dseek(&text, loc, sizeof filhdr);
	mget((uint16_t *)&filhdr, sizeof filhdr);
	if (filhdr.fmagic != FMAGIC)
		error(2, "Bad format");
	st = (filhdr.tsize+01) & ~01;
	filhdr.tsize = st;
	cdrel = -st;
	sd = (filhdr.dsize+01) & ~01;
	cbrel = - (st+sd);
	filhdr.bsize = (filhdr.bsize+01) & ~01;
}

static void
cp8c(char *from, char *to)
{
	register char *f, *t, *te;

	f = from;
	t = to;
	te = t+8;
	while ((*t++ = *f++) && t<te);
	while (t<te)
		*t++ = 0;
}

static int
eq(char *s1, char *s2)
{
	while (*s1==*s2++)
		if ((*s1++)==0)
			return(TRUE);
	return(FALSE);
}

static void
putw(int w, register struct buf *b)
{
	*(b->xnext)++ = w;
	if (--b->nleft <= 0)
		flush(b);
}

static void
flush(register struct buf *b)
{
	register int n;

	if ((n = (char *)b->xnext - (char *)b->iobuf) > 0)
		if (write(b->fildes, (char *)b->iobuf, n) != n)
			error(2, "output error");
	b->xnext = b->iobuf;
	b->nleft = sizeof(b->iobuf)/sizeof(*b->iobuf);
}
