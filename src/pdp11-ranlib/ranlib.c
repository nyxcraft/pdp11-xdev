#include <ar.h>
#include <a.out.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#define MAGIC exph.a_magic
#define BADMAG MAGIC != A_MAGIC1 &&MAGIC != A_MAGIC2 &&MAGIC != A_MAGIC3 &&MAGIC != A_MAGIC4
struct ar_hdr arp;
struct exec exph;
FILE *fi, *fo;
long off, oldoff;
char arcmd[1024] = "ar"; /* resolved to this toolchain's ar below */
#define TABSZ 700

struct tab {
	char cname[8];
	int32_t cloc; /* on-disk byte offset: 4 bytes, so the ranlib
		       * entry is 12 bytes (not 16 on an LP64 host) */
} __attribute__((packed)) tab[TABSZ];

int tnum;
int new;
char tempnm[] = "__.SYMDEF";
char firstname[17];
long offdelta;

static void setup_ar(void);
static int nextel(FILE *af);
static void stash(struct nlist *s);
static int fixsize(void);
static void fixdate(char *s);
static int  run_ar(char *first, char *archive, char *temp);

/* Resolve this toolchain's ar (e.g. .../usr/bin/pdp11-bsd29-ar) relative to
 * the ranlib binary, so `ar rlb' invokes the matching ar -- the same
 * /proc/self/exe scheme cc and ld use. */
static void
setup_ar(void)
{
	static char self[1024];
	int n = readlink("/proc/self/exe", self, sizeof self - 1);
	if (n > 0) {
		char *sl;
		self[n] = '\0';
		for (sl = self + n; sl > self && sl[-1] != '/'; sl--)
			;
		/* "<dir>/<prefix>ranlib" -> "<dir>/<prefix>ar" */
		if (sl - self >= 6 && strcmp(sl, "ranlib") != 0) {
			char *r = sl;
			while ((r = strstr(r, "ranlib")) != 0) {
				char tail[1024];
				strcpy(tail, r + 6);
				strcpy(r, "ar");
				strcat(r, tail);
				break;
			}
			strcpy(arcmd, self);
		}
		else if (sl - self >= 6 && strcmp(sl, "ranlib") == 0) {
			strcpy(sl, "ar");
			strcpy(arcmd, self);
		}
	}
}

/* Run `<ar> r[l]b? [first] archive temp' via fork/exec, NOT system(): the
 * member name `first' comes straight from an on-disk ar_name and must never
 * be interpreted as shell text (a crafted name could inject commands). */
static int
run_ar(char *first, char *archive, char *temp)
{
	char *av[8];
	int n = 0, st;
	pid_t pid;

	av[n++] = arcmd;
	av[n++] = first ? "rlb" : "rl";
	if (first)
		av[n++] = first;
	av[n++] = archive;
	av[n++] = temp;
	av[n] = 0;
	if ((pid = fork()) == 0) {
		execvp(arcmd, av);
		_exit(127);
	}
	if (pid < 0)
		return (-1);
	while (waitpid(pid, &st, 0) < 0)
		;
	return (st);
}

int
main(int argc, char **argv)
{

	setup_ar();
	--argc;
	while (argc--) {
		fi = fopen(*++argv, "r");
		if (fi == NULL) {
			fprintf(stderr, "nm: cannot open %s\n", *argv);
			continue;
		}
		off = sizeof(exph.a_magic);
		if (fread((char *)&exph, 1, sizeof(MAGIC), fi)) {
		} /* get magic no. */
		if ((unsigned short)MAGIC != ARMAG) {
			fprintf(stderr, "not archive: %s\n", *argv);
			continue;
		}
		fseek(fi, 0L, 0);
		new = tnum = 0;
		if (nextel(fi) == 0) {
			fclose(fi);
			continue;
		}
		do {
			long o;
			register int n;
			struct nlist sym;

			if (fread((char *)&exph, 1, sizeof(struct exec), fi)) {
			}
			if (BADMAG)	  /* archive element not in  */
				continue; /* proper format - skip it */
			o = (long)exph.a_text + exph.a_data;
			if ((exph.a_flag & 01) == 0)
				o *= 2;
			fseek(fi, o, 1);
			n = exph.a_syms / sizeof(struct nlist);
			if (n == 0) {
				fprintf(stderr, "nm: %s-- no name list\n", arp.ar_name);
				continue;
			}
			while (--n >= 0) {
				if (fread((char *)&sym, 1, sizeof(sym), fi)) {
				}
				if ((sym.n_type & N_EXT) == 0)
					continue;
				switch (sym.n_type & N_TYPE) {
				case N_UNDF:
					continue;

				default:
					stash(&sym);
					continue;
				}
			}
		}
		while (nextel(fi));
		new = fixsize();
		fclose(fi);
		fo = fopen(tempnm, "w");
		if (fo == NULL) {
			fprintf(stderr, "can't create temporary\n");
			exit(1);
		}
		{
			int i;
			for (i = 0; i < tnum; i++)
				tab[i].cloc = PDPL(tab[i].cloc);
		}
		fwrite((char *)tab, tnum, sizeof(struct tab), fo);
		fclose(fo);
		if (run_ar(new ? firstname : NULL, *argv, tempnm))
			fprintf(stderr, "can't run %s\n", arcmd);
		else
			fixdate(*argv);
		unlink(tempnm);
	}
	exit(0);
}

static int
nextel(FILE *af)
{
	register int r;

	oldoff = off;
	fseek(af, off, 0);
	r = fread((char *)&arp, 1, sizeof(struct ar_hdr), af); /* read archive header */
	if (r == sizeof(struct ar_hdr)) {		       /* PDP-11 middle-endian -> host */
		arp.ar_size = PDPL(arp.ar_size);
		arp.ar_date = PDPL(arp.ar_date);
	}
	if (r <= 0)
		return (0);
	if (arp.ar_size & 1)
		++arp.ar_size;
	off = ftell(af) + arp.ar_size; /* offset to next element */
	return (1);
}

static void
stash(struct nlist *s)
{
	int i;
	if (tnum >= TABSZ) {
		fprintf(stderr, "symbol table overflow\n");
		exit(1);
	}
	for (i = 0; i < 8; i++)
		tab[tnum].cname[i] = s->n_name[i];
	tab[tnum].cloc = oldoff;
	tnum++;
}

static int
fixsize(void)
{
	int i;
	offdelta = tnum * sizeof(struct tab) + sizeof(arp);
	off = sizeof(MAGIC);
	nextel(fi);
	if (strncmp(arp.ar_name, tempnm, 14) == 0) {
		new = 0;
		offdelta -= sizeof(arp) + arp.ar_size;
	}
	else {
		new = 1;
		strncpy(firstname, arp.ar_name, 14);
	}
	for (i = 0; i < tnum; i++)
		tab[i].cloc += offdelta;
	return (new);
}

/* patch time */
static void
fixdate(char *s)
{
	int32_t timex;
	int fd;
	fd = open(s, 1);
	if (fd < 0) {
		fprintf(stderr, "can't reopen %s\n", s);
		return;
	}
	/* The __.SYMDEF member's date must be >= the archive's mtime or ld treats
	 * the table as out of date and falls back to a single-pass scan.  On the
	 * PDP-11 this was time()+5; on the host (where filesystem clock skew is
	 * possible) use a far-future date so the table is always honoured.  Write
	 * exactly 4 bytes -- the on-disk ar_date is 4, not host sizeof(long)=8. */
	timex = PDPL(0x7fffffff); /* PDP-11 middle-endian on-disk ar_date */
	lseek(fd, (long)sizeof(exph.a_magic) + ((char *)&arp.ar_date - (char *)&arp), 0);
	if (write(fd, (char *)&timex, 4)) {
	}
	close(fd);
}
