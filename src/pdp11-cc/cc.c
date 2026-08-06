static	char	sccsid[] = "@(#)cc.c	2.6";	/*	SCCS id keyword	*/
#
# include <stdio.h>
# include <ctype.h>
# include <signal.h>
# include <stdlib.h>
# include <string.h>
# include <fcntl.h>
# include <sys/types.h>
# include <sys/wait.h>
# include <whoami.h>
# include "universe.h"

/* OS entry points that live in <unistd.h>, declared individually: cc supplies
 * its own execvp/execlp below whose signatures are incompatible with libc's,
 * so <unistd.h> (which would prototype those) cannot be included.  Real
 * prototypes here keep the pointer returns from being truncated under LP64. */
ssize_t	readlink(const char *, char *, size_t);
int	close(int);
pid_t	fork(void);
int	execv(const char *, char *const *);
unsigned int sleep(unsigned int);
int	unlink(const char *);

/* forward prototypes for the file-local helpers; cc's own execvp/execlp keep
 * external linkage so they shadow libc. */
static char	*tmpdir_(void);
static void	setup_tools(char *av0);
static void	resolve_universe(void);
static void	idexit(int sig);
static void	dexit(void);
static void	error(char *s, char *x);
static int	getsuf(char as[]);
static char	*setsuf(char *as, int ch);
static int	callsys(char f[], char *v[]);
static char	*copy(char *as);
static int	nodup(char **l, char *os);
static void	cunlink(char *f);
static char	*execat(char *s1, char *s2, char *si);
int	execvp(char *name, char **argv);

/* cc command */

# define UCB_UQTEMP
				/*
				 * note: The UNIQUE_TEMPORARY fix was done
				 * to allow simultaneous C compiles by
				 * processes with super-user priviledges
				 */
# define MAXINC 10
# define MAXFIL 100
# define MAXLIB 100
# define MAXOPT 100
char	*tmp0;
char	*tmp1;
char	*tmp2;
char	*tmp3;
char	*tmp4;
char	*tmp5;
int	tmpdig = 8;	/* index of the pass digit in tmp0 ("/tmp/ctm0.."); recomputed
			 * for $TMPDIR since the prefix length varies */
char	*outfile;
/* temp directory: $TMPDIR else /tmp.  The 2.8 cc hardcoded /tmp; honouring
 * TMPDIR lets the build avoid a flaky/namespaced /tmp (WSL drvfs, systemd
 * PrivateTmp).  Only the compiler-pass scratch files live here -- it does not
 * affect the emitted object. */
static char	*tmpdir_(void){ char *t=getenv("TMPDIR"); return (t&&*t)?t:"/tmp"; }
# define CHSPACE 1000
char	ts[CHSPACE+50];
char	*tsa = ts;
char	*tsp = ts;
char	*av[50];
char	*clist[MAXFIL];
char	*llist[MAXLIB];
int	pflag;
int	sflag;
int	cflag;
int	eflag;
int	vflag;
int	exflag;
int	oflag;
int	proflag;
#ifdef MENLO_OVLY
int	ovlyflag;
#endif /* MENLO_OVLY */
int	noflflag;
char	*chpass ;
char	*npassname ;
char	pass0[2048] = "../lib/c0";
char	pass1[2048] = "../lib/c1";
char	pass2[2048] = "../lib/c2";
char	passp[2048] = "../lib/cpp";
char	asname[2048] = "as";	/* resolved by setup_tools() */
char	ldname[2048] = "ld";	/* resolved by setup_tools() */
char	prefbuf[1024];
char	*pref = "/lib/crt0.o";
char	*crtname = "crt0.o";	/* which startup file; composed into pref */
char	libroot[1024];		/* ".../lib/" next to the bin dir, or "" */
char	*universe;		/* resolved universe name (--universe / -u /
				 * $PDP11_UNIVERSE, default bsd29) */

/*
 * Resolve the compiler-pass binaries relative to this cc's own location,
 * so the toolchain is relocatable.  If cc is .../usr/bin/<prefix>-cc, the
 * passes are .../usr/bin/<prefix>-{cpp,c0,c1,c2,as,ld} and crt0.o is in
 * .../usr/lib/.  Mirrors the VAX project's scheme.
 */
static void
setup_tools(char *av0)
{
	static char self[1024], base[1024], prefix[64];
	char *p, *last, *name, *dash, *bin;

	if (strchr(av0, '/') == 0) {		/* bare name: use /proc/self/exe */
		int n = readlink("/proc/self/exe", self, sizeof self - 1);
		if (n > 0) { self[n] = 0; av0 = self; }
	}
	for (last = 0, p = av0; *p; p++)
		if (*p == '/') last = p;
	if (last == 0)
		return;				/* keep the default paths */
	name = last + 1;
	/* base = everything up to and including the final "/" */
	{ int len = name - av0; strncpy(base, av0, len); base[len] = 0; }
	/* prefix = cc's name up to and including the last '-' */
	prefix[0] = 0;
	for (dash = 0, p = name; *p; p++)
		if (*p == '-') dash = p;
	if (dash) { int n = dash - name + 1; strncpy(prefix, name, n); prefix[n] = 0; }

	bin = base;				/* passes live alongside cc */
	sprintf(passp, "%s%scpp", bin, prefix);
	sprintf(pass0, "%s%sc0", bin, prefix);
	sprintf(pass1, "%s%sc1", bin, prefix);
	sprintf(pass2, "%s%sc2", bin, prefix);
	sprintf(asname, "%s%sas", bin, prefix);
	sprintf(ldname, "%s%sld", bin, prefix);
	/* one universal libc lives FLAT under .../lib/ if cc is under
	 * .../bin; remember the lib root and compose the crt0 path in
	 * resolve_universe (the universe selects __univ, not a directory) */
	if ((last = strstr(base, "bin/")) != 0) {
		int len = last - base;
		strncpy(libroot, base, len);
		strcpy(libroot + len, "lib/");
	}
}

/*
 * Resolve the target universe: --universe/-u beats $PDP11_UNIVERSE beats the
 * default (bsd29).  The name is validated against the generated universe
 * table; only "full" universes have headers and a C library to compile
 * against.  The resolved name is exported so cpp (include dir) and ld
 * (library dir) pick the same era without their own flags.
 */
static void
resolve_universe(void)
{
	int ok = 0;

	if (universe == 0 || universe[0] == 0)
		universe = getenv("PDP11_UNIVERSE");
	if (universe == 0 || universe[0] == 0)
		universe = PDP11_UNIV_DEFAULT_NAME;
	/* Accept the same alias spellings apsim does (1bsd->bsd1, 2.9->bsd29,
	 * ...) and normalize to the canonical name, so the exported
	 * PDP11_UNIVERSE cpp/ld read is always canonical. */
#define X(alias, canon) \
	if (strcmp(universe, alias) == 0) universe = canon;
	PDP11_UNIVERSE_ALIASES(X)
#undef X
#define X(nm, id, status, desc) \
	if (strcmp(universe, #nm) == 0 && status[0] == 'f') ok = 1;
	PDP11_UNIVERSES(X)
#undef X
	if (!ok) {
		fprintf(stderr, "cc: unknown or unbuildable universe `%s'; valid:", universe);
#define X(nm, id, status, desc) \
		if (status[0] == 'f') fprintf(stderr, " %s", #nm);
		PDP11_UNIVERSES(X)
#undef X
		fprintf(stderr, "\n");
		exit(8);
	}
	setenv("PDP11_UNIVERSE", universe, 1);
	/* ONE universal libc: crt0 (and libc.a, headers) live FLAT in lib/,
	 * not per-universe -- the universe now selects only __univ, which ld
	 * stamps into the executable and crt0 records for the library. */
	if (libroot[0])
		sprintf(prefbuf, "%s%s", libroot, crtname);
	else
		sprintf(prefbuf, "/lib/%s", crtname);
	pref = prefbuf;
}
int main(int argc, char **argv)
{
	char *t;
	char *savetsp;
	char *assource;
	char **pv, *ptemp[MAXOPT], **pvt;
	int nc, nl, i, j, c, f20, nxo, na;

	i = nc = nl = f20 = nxo = 0;
	setup_tools(argv[0]);
	setbuf(stdout, (char *)NULL);
	pv = ptemp;
	while(++i < argc) {
		if (strncmp(argv[i], "--universe=", 11) == 0) {
			universe = argv[i] + 11;
			continue;
		}
		if (strcmp(argv[i], "--universe") == 0 || strcmp(argv[i], "-u") == 0) {
			if (++i < argc)
				universe = argv[i];
			continue;
		}
		if(*argv[i] == '-') switch (argv[i][1]) {
		default:
			goto passa;
		case 'S':
			sflag++;
			cflag++;
			break;
		case 'o':
			if (++i < argc) {
				outfile = argv[i];
				if ((c=getsuf(outfile))=='c'||c=='o') {
					error("Would overwrite %s", outfile);
					exit(8);
				}
			}
			break;
		case 'O':
			oflag++;
			break;
		case 'p':
			proflag++;
			break;
#ifdef MENLO_OVLY
		case 'V':
			ovlyflag++;
			break;
#endif /* MENLO_OVLY */
		case 'E':
			exflag++;
		case 'P':
			pflag++;
			*pv++ = argv[i];
		case 'c':
			cflag++;
			break;

		case 'f':
			noflflag++;
			if (npassname || chpass)
				error("-f overwrites earlier option", (char *)NULL);
			npassname = "../lib/f";
			chpass = "1";
			break;

		case '2':
			if(argv[i][2] == '\0')
				crtname = "crt2.o";
			else {
				crtname = "crt20.o";
				f20 = 1;
			}
			break;
		case 'D':
		case 'I':
		case 'U':
		case 'C':
			*pv++ = argv[i];
			if (pv >= ptemp+MAXOPT) {
				error("Too many DIUC options", (char *)NULL);
				--pv;
			}
			break;
		case 't':
			if (chpass)
				error("-t overwrites earlier option", (char *)NULL);
			chpass = argv[i]+2;
			if (chpass[0]==0)
				chpass = "012p";
			break;

		case 'B':
			if (npassname)
				error("-B overwrites earlier option", (char *)NULL);
			npassname = argv[i]+2;
			if (npassname[0]==0)
				npassname = "/usr/src/cmd/c/o";
			break;

		case 'v':
			vflag++;
			break;
		} 
		else {
passa:
			t = argv[i];
			if((c=getsuf(t))=='c' || c=='s'|| exflag) {
				clist[nc++] = t;
				if (nc>=MAXFIL) {
					error("Too many source files", (char *)NULL);
					exit(1);
				}
				t = setsuf(t, 'o');
			}
			if (nodup(llist, t)) {
				llist[nl++] = t;
				if (nl >= MAXLIB) {
					error("Too many object/library files", (char *)NULL);
					exit(1);
				}
				if (getsuf(t)=='o')
					nxo++;
			}
		}
	}
	if (npassname && chpass ==0)
		chpass = "012p";
	if (chpass && npassname==0)
		npassname = "/usr/src/cmd/c/";
	if (chpass)
		for (t=chpass; *t; t++) {
			switch (*t) {
			case '0':
				strcpy (pass0, npassname);
				strcat (pass0, "c0");
				continue;
			case '1':
				strcpy (pass1, npassname);
				strcat (pass1, "c1");
				continue;
			case '2':
				strcpy (pass2, npassname);
				strcat (pass2, "c2");
				continue;
			case 'p':
				strcpy (passp, npassname);
				strcat (passp, "cpp");
				continue;
			}
		}
	if (noflflag)
		crtname = proflag ? "fmcrt0.o" : "fcrt0.o";
	else if (proflag)
		crtname = "mcrt0.o";
	resolve_universe();
	if(nc==0)
		goto nocom;
	if (pflag==0) {
		char tb[1024]; char *td = tmpdir_();
		tmpdig = strlen(td) + 4;		/* the '0' in "<td>/ctm0..." */
# ifdef UCB_UQTEMP
		int FD;		/* a file descriptor, not a char */

		sprintf(tb, "%s/ctm0XXXXXX", td);
		tmp0 = copy(tb);
		FD = mkstemp (tmp0);	/* POSIX: makes the name AND the 0600 file */
		if (FD < 0)
		{
			error("cc: cannot create temp", NULL);
			exit(1);
		}
		close (FD);
# else
		sprintf(tb, "%s/ctm0a", td);
		tmp0 = copy(tb);
		while (access(tmp0, 0)==0)
			tmp0[tmpdig+1]++;
		while((creat(tmp0, 0400))<0) {
			if (tmp0[tmpdig+1]=='z') {
				error("cc: cannot create temp", NULL);
				exit(1);
			}
			tmp0[tmpdig+1]++;
		}
# endif
	}
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		signal(SIGINT, idexit);
	if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
		signal(SIGTERM, idexit);
	/* tmp0 is only created when pflag==0 (see above); the tmp1..tmp5 names
	 * are derived from it via copy(tmp0).  For -E/-P (pflag) tmp0 is NULL and
	 * these compile-pass temporaries are unused (tmp4 is set from the source
	 * name at the cpp step below), so guard them -- otherwise copy(NULL)
	 * dereferences a null pointer and cc crashes. */
	if (pflag==0) {
		(tmp1 = copy(tmp0))[tmpdig] = '1';
		(tmp2 = copy(tmp0))[tmpdig] = '2';
		(tmp3 = copy(tmp0))[tmpdig] = '3';
		if (oflag)
			(tmp5 = copy(tmp0))[tmpdig] = '5';
		(tmp4 = copy(tmp0))[tmpdig] = '4';
	}
	pvt = pv;
	for (i=0; i<nc; i++) {
		if (nc>1 || vflag)
			printf("%s:\n", clist[i]);
		if (getsuf(clist[i])=='s') {
			assource = clist[i];
			goto assemble;
		} 
		else
			assource = tmp3;
		if (pflag)
			tmp4 = setsuf(clist[i], 'i');
		savetsp = tsp;
		av[0] = "cpp";
		av[1] = clist[i];
		av[2] = exflag ? "-" : tmp4;
		na = 3;
		/* the native PDP-11 cpp predefined these; ours does not */
		av[na++] = "-Dunix";
		av[na++] = "-Dpdp11";
#ifdef MENLO_OVLY
		if (ovlyflag)
			av[na++] = "-DC_OVERLAY";	/* force definition */
#endif
		for(pv=ptemp; pv <pvt; pv++)
			av[na++] = *pv;
		av[na++]=0;
		if(vflag) {
			printf("  Preprocessing\n");
			fflush(stdout);
		}
		if (callsys(passp, av)) {
			cflag++;
			eflag++;
			continue;
		}
		av[1] = tmp4;
		tsp = savetsp;
		av[0]= "c0";
		if (pflag) {
			cflag++;
			continue;
		}
		na = 2;
		av[na++] = tmp1;
		av[na++] = tmp2;
		if (proflag)
			av[na++] = "-P";
#ifdef MENLO_OVLY
		if (ovlyflag)
			av[na++] = "-V";
#endif
		av[na++] = 0;
		if(vflag) {
			printf("  Pass 0\n");
			fflush(stdout);
		}
		if (callsys(pass0, av)) {
			cflag++;
			eflag++;
			continue;
		}
		av[0] = "c1";
		av[1] = tmp1;
		av[2] = tmp2;
		if (sflag)
			assource = tmp3 = setsuf(clist[i], 's');
		av[3] = tmp3;
		if (oflag)
			av[3] = tmp5;
		na = 4;
#ifdef MENLO_OVLY
		if (ovlyflag)
			av[na++] = "-V";
#endif
		av[na++] = 0;
		if(vflag) {
			printf("  Pass 1\n");
			fflush(stdout);
		}
		if(callsys(pass1, av)) {
			cflag++;
			eflag++;
			continue;
		}
		if (oflag) {
			av[0] = "c2";
			av[1] = tmp5;
			av[2] = tmp3;
			av[3] = 0;
			if(vflag) {
				printf("  Pass 2\n");
				fflush(stdout);
			}
			if (callsys(pass2, av)) {
				unlink(tmp3);
				tmp3 = assource = tmp5;
			} 
			else
				unlink(tmp5);
		}
		if (sflag)
			continue;
assemble:
		av[0] = "as";
		av[1] = "-u";
		j = 2;
#ifdef MENLO_OVLY
		if (ovlyflag)
			av[j++] = "-V";	/* ovas mode: extern refs to defined
					 * global text symbols (thunks) */
#endif
		av[j++] = "-o";
		av[j++] = setsuf(clist[i], 'o');
		av[j++] = assource;
		av[j] = 0;
		cunlink(tmp1);
		cunlink(tmp2);
		cunlink(tmp4);
		if(vflag) {
			printf("  Assembling\n");
			fflush(stdout);
		}
		if (callsys(
#ifdef MENLO_OVLY
				/* our as IS the overlay as (no separate ovas) */
				ovlyflag ? asname :
#endif /* MENLO_OVLY */
				asname, av) > 1) {
			cflag++;
			eflag++;
			continue;
		}
	}
nocom:
	if (cflag==0 && nl!=0) {
		i = 0;
		av[0] = "ld";
		av[1] = "-X";
		av[2] = pref;
		j = 3;
		if (noflflag) {
			j = 4;
			av[3] = "-lfpsim";
		}
		if (outfile) {
			av[j++] = "-o";
			av[j++] = outfile;
		}
		while(i<nl)
			av[j++] = llist[i++];
		if(f20)
			av[j++] = "-l2";
		else {
			av[j++] =
#ifdef MENLO_OVLY
				ovlyflag ? "-lovc" :
#endif /* MENLO_OVLY */
				"-lc";
		}
		av[j++] = 0;
		if(vflag) {
			printf("Loading\n");
			fflush(stdout);
		}
		eflag |= callsys(
#ifdef MENLO_OVLY
				/* our ld IS the overlay ld (no separate ovld) */
				ovlyflag ? ldname :
#endif /* MENLO_OVLY */
				ldname, av);
		if (nc==1 && nxo==1 && eflag==0)
			cunlink(setsuf(clist[0], 'o'));
	}
	dexit();
}

static void
idexit(int sig)
{
	eflag = 100;
	dexit();
}

static void
dexit(void)
{
	if (!pflag) {
		cunlink(tmp1);
		cunlink(tmp2);
		if (sflag==0)
			cunlink(tmp3);
		cunlink(tmp4);
		cunlink(tmp5);
		cunlink(tmp0);
	}
	exit(eflag);
}

static void
error(char *s, char *x)
{
	fprintf(exflag?stderr:stdout, s, x);
	putc('\n', exflag? stderr : stdout);
	cflag++;
	eflag++;
}




static int
getsuf(char as[])
{
	register int c;
	register char *s;
	register int t;

	s = as;
	c = 0;
	while(t = *s++)
		if (t=='/')
			c = 0;
		else
			c++;
	s -= 3;
	if (c<=14 && c>2 && *s++=='.')
		return(*s);
	return(0);
}

static char *
setsuf(char *as, int ch)
{
	register char *s, *s1;

	s = s1 = copy(as);
	while(*s)
		if (*s++ == '/')
			s1 = s;
	s[-1] = ch;
	return(s1);
}

static int
callsys(char f[], char *v[])
{
	int t, status;

	if ((t=fork())==0) {
		execvp(f, v);
		printf("Can't find %s\n", f);
		exit(100);
	} else
		if (t == -1) {
			printf("Try again\n");
			return(100);
		}
	while(t!=wait(&status))
		;
	if (t = status&0377) {
		if (t!=SIGINT) {
			printf("Fatal error in %s\n", f);
			eflag = 8;
		}
		dexit();
	}
	return((status>>8) & 0377);
}

static char *
copy(char *as)
{
	register char *otsp, *s;

	otsp = tsp;
	s = as;
	while (*tsp++ = *s++)
		;
	if (tsp > tsa+CHSPACE) {
		tsp = tsa = malloc(CHSPACE+50);
		if (tsp==NULL) {
			error("no space for file names", (char *)NULL);
			dexit();
		}
	}
	return(otsp);
}

static int
nodup(char **l, char *os)
{
	register char *t, *s;
	register int c;

	s = os;
	if (getsuf(s) != 'o')
		return(1);
	while(t = *l++) {
		while(c = *s++)
			if (c != *t++)
				break;
		if (*t=='\0' && c=='\0')
			return(0);
		s = os;
	}
	return(1);
}

static void
cunlink(char *f)
{
	if (f==NULL)
		return;
	unlink(f);
}

/*
 *	The following version is like the standard one, but does
 * 	not check against slashes in the name.
 *
 *	execlp(name, arg,...,0)	(like execl, but does path search)
 *	execvp(name, argv)	(like execv, but does path search)
 */
#include <errno.h>

static	char shell[] =	"/bin/sh";

/* deliberately K&R-simple, forwarding to execvp; its two fixed args cannot
 * match libc's variadic execlp builtin, so the builtin-mismatch warning is
 * suppressed only around this definition. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
int execlp(char *name, char *argv)
{
	return(execvp(name, &argv));
}
#pragma GCC diagnostic pop

int execvp(char *name, char **argv)
{
	char *pathstr;
	register char *cp;
	char fname[1024];
	char *newargs[256];
	int i;
	register unsigned etxtbsy = 1;
	register int eacces = 0;

	/* a name containing '/' is used directly -- no $PATH search.  cc now
	 * passes absolute pass paths, and searching PATH would both be wrong
	 * and overflow fname. */
	if (strchr(name, '/')) {
		execv(name, argv);
		return(-1);
	}
	if ((pathstr = getenv("PATH")) == NULL)
		pathstr = ":/bin:/usr/bin";
	cp = pathstr;

	do {
		cp = execat(cp, name, fname);
	retry:
		execv(fname, argv);
		switch(errno) {
		case ENOEXEC:
			newargs[0] = "sh";
			newargs[1] = fname;
			for (i=1; newargs[i+1]=argv[i]; i++) {
				if (i>=254) {
					errno = E2BIG;
					return(-1);
				}
			}
			execv(shell, newargs);
			return(-1);
		case ETXTBSY:
			if (++etxtbsy > 5)
				return(-1);
			sleep(etxtbsy);
			goto retry;
		case EACCES:
			eacces++;
			break;
		case ENOMEM:
		case E2BIG:
			return(-1);
		}
	} while (cp);
	if (eacces)
		errno = EACCES;
	return(-1);
}

static char *
execat(register char *s1, register char *s2, char *si)
{
	register char *s;

	s = si;
	while (*s1 && *s1 != ':' && *s1 != '-')
		*s++ = *s1++;
	if (si != s)
		*s++ = '/';
	while (*s2)
		*s++ = *s2++;
	*s = '\0';
	return(*s1? ++s1: 0);
}
