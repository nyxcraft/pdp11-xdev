char *sccsid = "@(#)size.c	2.5";
#include <stdio.h>
#include <stdlib.h>
#include <a.out.h>
#include <whoami.h>

/*
 *	size -- determine object size
 */

int
main(int argc, char **argv)
{
	struct exec buf;
	long sum;
#ifdef MENLO_OVLY
	long coresize;
	struct ovlhdr ovlbuf;
	int i;
#endif
	FILE *f;

	if (argc == 1) {
		*argv = "a.out";
		argc++;
		--argv;
	}
	printf("text\tdata\tbss\tdec\toct\n");
	while (--argc) {
		++argv;
		if ((f = fopen(*argv, "r")) == NULL) {
			perror(*argv);
			continue;
		}
		if (fread((char *)&buf, sizeof(buf), 1, f) != 1) {
			printf("%s:  not in object file format\n", *argv);
			fclose(f);
			continue;
		}
		if (N_BADMAG(buf)) {
			printf("%s:  not in object file format\n", *argv);
			fclose(f);
			continue;
		}
		if (buf.a_magic == 0405) {
			/* First Edition a.out(V): a 6-word header that a_text
			 * INCLUDES (real text = a_text-12); word 5 (the a_syms
			 * slot) is the data area, i.e. the era's bss; no data
			 * segment.  Words 3,4 are the symtab/reloc sizes. */
			unsigned t = buf.a_text - 12, b = buf.a_syms;
			printf("%u +\t%u +\t%u =\t", t, 0, b);
			sum = (long)t + (long)b;
		}
		else {
			printf("%u +\t%u +\t%u =\t", buf.a_text, buf.a_data, buf.a_bss);
			sum = (long)buf.a_text + (long)buf.a_data + (long)buf.a_bss;
		}
		printf("%ld =\t%lo", sum, sum);
		printf("\t%s\n", *argv);
#ifdef MENLO_OVLY
		if (buf.a_magic == A_MAGIC5 || buf.a_magic == A_MAGIC6) {
			if (fread(&ovlbuf, sizeof ovlbuf, 1, f)) {
			}
			coresize = buf.a_text;
			for (i = 0; i < NOVL; i++)
				if (ovlbuf.ov_siz[i])
					coresize += ovlbuf.ov_siz[i];
			printf("%ld total text, overlays: (", coresize);
			for (i = 0; i < NOVL; i++)
				if (ovlbuf.ov_siz[i]) {
					if (i > 0)
						printf(",");
					printf("%u", ovlbuf.ov_siz[i]);
				}
			printf(")\n");
		}
#endif
		fclose(f);
	}
	exit(0);
}
