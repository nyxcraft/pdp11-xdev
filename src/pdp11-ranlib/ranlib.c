#include	<ar.h>
#include	<a.out.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<stdint.h>
#include	<time.h>
#define	MAGIC	exp.a_magic
#define	BADMAG	MAGIC!=A_MAGIC1 && MAGIC!=A_MAGIC2  \
		&& MAGIC!=A_MAGIC3 && MAGIC!=A_MAGIC4
struct	ar_hdr	arp;
struct	exec	exp;
FILE	*fi, *fo;
long	off, oldoff;
long	ftell();
char	arcmd[1024] = "ar";	/* resolved to this toolchain's ar below */
#define TABSZ	700
struct tab
{	char cname[8];
	int32_t cloc;		/* on-disk byte offset: 4 bytes, so the ranlib
				 * entry is 12 bytes (not 16 on an LP64 host) */
} __attribute__((packed)) tab[TABSZ];
int tnum;
int new;
char	tempnm[] = "__.SYMDEF";
char	firstname[17];
long	offdelta;

/* Resolve this toolchain's ar (e.g. .../usr/bin/pdp11-bsd29-ar) relative to
 * the ranlib binary, so `ar rlb' invokes the matching ar -- the same
 * /proc/self/exe scheme cc and ld use. */
setup_ar()
{
	static char self[1024];
	int n = readlink("/proc/self/exe", self, sizeof self - 1);
	if (n > 0) {
		char *sl; self[n] = '\0';
		for (sl = self+n; sl > self && sl[-1] != '/'; sl--);
		/* "<dir>/<prefix>ranlib" -> "<dir>/<prefix>ar" */
		if (sl-self >= 6 && strcmp(sl, "ranlib") != 0) {
			char *r = sl;
			while ((r = strstr(r, "ranlib")) != 0) {
				char tail[1024]; strcpy(tail, r+6);
				strcpy(r, "ar"); strcat(r, tail); break;
			}
			strcpy(arcmd, self);
		} else if (sl-self >= 6 && strcmp(sl, "ranlib")==0) {
			strcpy(sl, "ar"); strcpy(arcmd, self);
		}
	}
}

main(argc, argv)
char **argv;
{
	char buf[256];

	setup_ar();
	--argc;
	while(argc--) {
		fi = fopen(*++argv,"r");
		if (fi == NULL) {
			fprintf(stderr, "nm: cannot open %s\n", *argv);
			continue;
		}
		off = sizeof(exp.a_magic);
		fread((char *)&exp, 1, sizeof(MAGIC), fi);	/* get magic no. */
		if ((unsigned short)MAGIC != ARMAG)
		{	fprintf(stderr, "not archive: %s\n", *argv);
			continue;
		}
		fseek(fi, 0L, 0);
		new = tnum = 0;
		if(nextel(fi) == 0)
		{	fclose(fi);
			continue;
		}
		do {
			long o;
			register n;
			struct nlist sym;

			fread((char *)&exp, 1, sizeof(struct exec), fi);
			if (BADMAG)		/* archive element not in  */
				continue;	/* proper format - skip it */
			o = (long)exp.a_text + exp.a_data;
			if ((exp.a_flag & 01) == 0)
				o *= 2;
			fseek(fi, o, 1);
			n = exp.a_syms / sizeof(struct nlist);
			if (n == 0) {
				fprintf(stderr, "nm: %s-- no name list\n", arp.ar_name);
				continue;
			}
			while (--n >= 0) {
				fread((char *)&sym, 1, sizeof(sym), fi);
				if ((sym.n_type&N_EXT)==0)
					continue;
				switch (sym.n_type&N_TYPE) {

				case N_UNDF:
					continue;

				default:
					stash(&sym);
					continue;
				}
			}
		} while(nextel(fi));
		new = fixsize();
		fclose(fi);
		fo = fopen(tempnm, "w");
		if(fo == NULL)
		{	fprintf(stderr, "can't create temporary\n");
			exit(1);
		}
		{ int i; for(i=0;i<tnum;i++) tab[i].cloc = PDPL(tab[i].cloc); }
		fwrite((char *)tab, tnum, sizeof(struct tab), fo);
		fclose(fo);
		if(new)
			sprintf(buf, "%s rlb %s %s %s\n", arcmd, firstname, *argv, tempnm);
		else	sprintf(buf, "%s rl %s %s\n", arcmd, *argv, tempnm);
		if(system(buf))
			fprintf(stderr, "can't execute %s\n", buf);
		else fixdate(*argv);
		unlink(tempnm);
	}
	exit(0);
}

nextel(af)
FILE *af;
{
	register r;

	oldoff = off;
	fseek(af, off, 0);
	r = fread((char *)&arp, 1, sizeof(struct ar_hdr), af);  /* read archive header */
	if (r == sizeof(struct ar_hdr)) {	/* PDP-11 middle-endian -> host */
		arp.ar_size = PDPL(arp.ar_size);
		arp.ar_date = PDPL(arp.ar_date);
	}
	if (r <= 0)
		return(0);
	if (arp.ar_size & 1)
		++arp.ar_size;
	off = ftell(af) + arp.ar_size;	/* offset to next element */
	return(1);
}

stash(s) struct nlist *s;
{	int i;
	if(tnum >= TABSZ)
	{	fprintf(stderr, "symbol table overflow\n");
		exit(1);
	}
	for(i=0; i<8; i++)
		tab[tnum].cname[i] = s->n_name[i];
	tab[tnum].cloc = oldoff;
	tnum++;
}

fixsize()
{	int i;
	offdelta = tnum * sizeof(struct tab) + sizeof(arp);
	off = sizeof(MAGIC);
	nextel(fi);
	if(strncmp(arp.ar_name, tempnm, 14) == 0)
	{	new = 0;
		offdelta -= sizeof(arp) + arp.ar_size;
	}
	else
	{	new = 1;
		strncpy(firstname, arp.ar_name, 14);
	}
	for(i=0; i<tnum; i++)
		tab[i].cloc += offdelta;
	return(new);
}

/* patch time */
fixdate(s) char *s;
{	int32_t timex;
	int fd;
	fd = open(s, 1);
	if(fd < 0)
	{	fprintf(stderr, "can't reopen %s\n", s);
		return;
	}
	/* The __.SYMDEF member's date must be >= the archive's mtime or ld treats
	 * the table as out of date and falls back to a single-pass scan.  On the
	 * PDP-11 this was time()+5; on the host (where filesystem clock skew is
	 * possible) use a far-future date so the table is always honoured.  Write
	 * exactly 4 bytes -- the on-disk ar_date is 4, not host sizeof(long)=8. */
	timex = PDPL(0x7fffffff);	/* PDP-11 middle-endian on-disk ar_date */
	lseek(fd, (long)sizeof(exp.a_magic) + ((char *)&arp.ar_date-(char *)&arp), 0);
	write(fd, (char *)&timex, 4);
	close(fd);
}
