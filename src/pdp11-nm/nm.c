/*
 *	print symbol tables for
 *	object or archive files
 *
 *	nm [-goprun] [name ...]
 */

#include <sys/param.h>
#include <ar.h>
#include <a.out.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef MENLO_OVLY
struct nnlist {		/* symbol table entry */
	char n_name[8]; /* symbol name */
	char nn_type;	/* type flag */
	char nn_ovno;
	unsigned n_value; /* value */
};
#endif /* MENLO_OVLY */
#define SELECT nmname(*argv)
int numsort_flg;
int undef_flg;
int revsort_flg = 1;
int globl_flg;
int nosort_flg;
int arch_flg;
int prep_flg;
struct ar_hdr arp;
struct exec exph;

/* Return a NUL-terminated name: an archive member's ar_name[14] is not
 * terminated when a name fills all 14 bytes, so `%s' would over-read into
 * ar_date; bound it.  Plain file names (fn == *argv) are C strings already. */
static char *
nmname(char *fn)
{
	static char nb[15];

	if (arch_flg) {
		memcpy(nb, arp.ar_name, 14);
		nb[14] = 0;
		return nb;
	}
	return fn;
}
FILE *fi;
long off;
static int compare(const void *a, const void *b);
static int nextel(FILE *af);

int
main(int argc, char **argv)
{
	int narg;

	if (--argc > 0 && argv[1][0] == '-' && argv[1][1] != 0) {
		argv++;
		while (*++*argv)
			switch (**argv) {
			case 'n': /* sort numerically */
				numsort_flg++;
				continue;

			case 'g': /* globl symbols only */
				globl_flg++;
				continue;

			case 'u': /* undefined symbols only */
				undef_flg++;
				continue;

			case 'r': /* sort in reverse order */
				revsort_flg = -1;
				continue;

			case 'p': /* don't sort -- symbol table order */
				nosort_flg++;
				continue;

			case 'o': /* prepend a name to each line */
				prep_flg++;
				continue;

			default: /* oops */
				fprintf(stderr, "nm: invalid argument -%c\n", *argv[0]);
				exit(1);
			}
		argc--;
	}
	if (argc == 0) {
		argc = 1;
		argv[1] = "a.out";
	}
	narg = argc;
	while (argc--) {
		fi = fopen(*++argv, "r");
		if (fi == NULL) {
			fprintf(stderr, "nm: cannot open %s\n", *argv);
			continue;
		}
		off = sizeof(exph.a_magic);
		if (fread((char *)&exph, 1, sizeof(exph.a_magic), fi)) {
		} /* get magic no. */
		/* compare as 16-bit: on the host a.a_magic sign-extends (ARMAG
		 * 0177545 has bit 15 set) and would never equal the int ARMAG. */
		if ((unsigned short)exph.a_magic == (unsigned short)ARMAG)
			arch_flg++;
		else if (N_BADMAG(exph)) {
			fprintf(stderr, "nm: %s-- bad format\n", *argv);
			continue;
		}
		fseek(fi, 0L, 0);
		if (arch_flg) {
			(void)nextel(fi);
			if (narg > 1)
				printf("\n%s:\n", *argv);
		}
		do {
			long o;
			register int i, n, c;
#ifdef MENLO_OVLY
			struct nnlist sym;
			struct nnlist *symp = NULL;
			unsigned ovsizes[1 + NOVL];
#else
			struct nlist sym;
			struct nlist *symp = NULL;
#endif /* MENLO_OVLY */
			if (fread((char *)&exph, 1, sizeof(struct exec), fi)) {
			}
			if (N_BADMAG(exph)) /* archive element not in  */
				continue;   /* proper format - skip it */
			if (exph.a_magic == 0405) {
				/* First Edition a.out(V): a 6-word header that
				 * a_text INCLUDES, so the symbol table (12-byte
				 * entries) begins at file offset a_text and its
				 * SIZE is in a_data; the a_syms slot holds the bss.
				 * Seek relative to here (after the 16-byte exec
				 * read) so this works for archive members too. */
				fseek(fi, (long)(unsigned short)exph.a_text - (long)sizeof(struct exec), 1);
				n = (unsigned short)exph.a_data / sizeof(struct nlist);
			}
			else {
#ifdef MENLO_OVLY
				if (exph.a_magic == A_MAGIC5 || exph.a_magic == A_MAGIC6) {
					if (fread((char *)ovsizes, 1, sizeof ovsizes, fi)) {
					}
					o = 0L;
					for (i = 1; i <= NOVL; i++)
						o += (long)ovsizes[i];
					fseek(fi, o, 1);
				}
#endif /* MENLO_OVLY */
				o = (long)exph.a_text + exph.a_data;
				if ((exph.a_flag & 01) == 0)
					o *= 2;
				fseek(fi, o, 1);
				n = exph.a_syms / sizeof(struct nlist);
			}
			if (n == 0) {
				fprintf(stderr, "nm: %s-- no name list\n", SELECT);
				continue;
			}
			i = 0;
			while (--n >= 0) {
				if (fread((char *)&sym, 1, sizeof(sym), fi)) {
				}
				if (exph.a_magic == 0405) {
					/* V1 symbol flag: 00 undef, 01 abs, 02 register,
					 * 03 relocatable, |40 global -- map to the later
					 * type/N_EXT encoding the switch below reads. */
#ifdef MENLO_OVLY
					unsigned vf = (unsigned char)sym.nn_type;
#else
					unsigned vf = (unsigned char)sym.n_type;
#endif
					int bt = vf & 037, nt;
					nt = bt == 0 ? N_UNDF : bt == 1 ? N_ABS
							: bt == 2	? N_REG
									: N_TEXT;
					if (vf & 040)
						nt |= N_EXT;
#ifdef MENLO_OVLY
					sym.nn_type = nt;
#else
					sym.n_type = nt;
#endif
				}
#ifndef MENLO_OVLY
				if (globl_flg && (sym.n_type & N_EXT) == 0)
#else  /* MENLO_OVLY */
				if (globl_flg && (sym.nn_type & N_EXT) == 0)
#endif /* MENLO_OVLY */
					continue;
#ifndef MENLO_OVLY
				switch (sym.n_type & N_TYPE)
#else  /* MENLO_OVLY */
				switch (sym.nn_type & N_TYPE)
#endif /* MENLO_OVLY */
				{

				case N_UNDF:
					c = 'u';
					if (sym.n_value)
						c = 'c';
					break;

				default:
				case N_ABS:
					c = 'a';
					break;

				case N_TEXT:
					c = 't';
					break;

				case N_DATA:
					c = 'd';
					break;

				case N_BSS:
					c = 'b';
					break;

				case N_FN:
					c = 'f';
					break;

				case N_REG:
					c = 'r';
					break;
				}
				if (undef_flg && c != 'u')
					continue;
#ifndef MENLO_OVLY
				if (sym.n_type & N_EXT)
#else  /* MENLO_OVLY */
				if (sym.nn_type & N_EXT)
#endif /* MENLO_OVLY */
					c = toupper(c);
#ifndef MENLO_OVLY
				sym.n_type = c;
#else  /* MENLO_OVLY */
				sym.nn_type = c;
#endif /* MENLO_OVLY */
				if (symp == NULL)
#ifdef MENLO_OVLY
					symp = (struct nnlist *)malloc(sizeof(struct nlist));
#else
					symp = (struct nlist *)malloc(sizeof(struct nlist));
#endif /* MENLO_OVLY */
				else {
#ifdef MENLO_OVLY
					symp = (struct nnlist *)realloc(symp, (i + 1) * sizeof(struct nlist));
#else
					symp = (struct nlist *)realloc(symp, (i + 1) * sizeof(struct nlist));
#endif /* MENLO_OVLY */
				}
				if (symp == NULL) {
					fprintf(stderr, "nm: out of memory on %s\n", *argv);
					exit(2);
				}
				symp[i++] = sym;
			}
			if (nosort_flg == 0)
				qsort(symp, i, sizeof(struct nlist), compare);
			if ((arch_flg || narg > 1) && prep_flg == 0)
				printf("\n%s:\n", SELECT);
			for (n = 0; n < i; n++) {
				if (prep_flg) {
					if (arch_flg)
						printf("%s:", *argv);
					printf("%s:", SELECT);
				}
#ifdef MENLO_OVLY
				c = symp[n].nn_type;
#else
				c = symp[n].n_type;
#endif /* MENLO_OVLY */
				if (!undef_flg) {
					if (c == 'u' || c == 'U')
						printf("      ");
					else
						printf(FORMAT, symp[n].n_value);
					printf(" %c ", c);
				}
#ifndef MENLO_OVLY
				printf("%.8s\n", symp[n].n_name);
#else  /* MENLO_OVLY */
				if (symp[n].nn_ovno)
					printf("%-8.8s %d", symp[n].n_name,
					       symp[n].nn_ovno);
				else
					printf("%.8s", symp[n].n_name);
				printf("\n");
#endif /* MENLO_OVLY */
			}
			if (symp)
				free((char *)symp);
		}
		while (arch_flg && nextel(fi));
		fclose(fi);
	}
	exit(0);
}

static int
compare(const void *a, const void *b)
{
	register const struct nlist *p1 = a, *p2 = b;
	register int i;

	if (numsort_flg) {
		if (p1->n_value > p2->n_value)
			return (revsort_flg);
		if (p1->n_value < p2->n_value)
			return (-revsort_flg);
	}
	for (i = 0; i < sizeof(p1->n_name); i++)
		if (p1->n_name[i] != p2->n_name[i]) {
			if (p1->n_name[i] > p2->n_name[i])
				return (revsort_flg);
			else
				return (-revsort_flg);
		}
	return (0);
}

static int
nextel(FILE *af)
{
	register int r;

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
