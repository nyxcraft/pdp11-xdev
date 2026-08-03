/*
 * mktab -- convert the cvopt-generated PDP-11 assembler code table
 * (table.i) into C (table.c) so the c1 code generator's tables can be
 * compiled and linked into the host build instead of being assembled.
 *
 *   table.s --cvopt--> table.i --mktab--> table.c --cc--> table.o
 *
 * table.i has these kinds of content:
 *   template      Ln:<frag>;.byte N;<frag>... <\0>        (may span lines)
 *   named/aliased add1:;L44:<...>     and     L40=fas1
 *   index         _NAME=.   then   VAL.; label   ... 0
 *   optab         LABEL=.+2; 0  then  .byte d,t;.byte d,t \n label  ... 0
 *
 * Emitted C:
 *   static char Lnn[] = "....";          (one per template, \NNN for bytes)
 *   #define ALIAS  Lnn                    (extra labels / Ln=name aliases)
 *   static struct optab LABEL[] = { {d,t,d2,t2, str}, ..., {0,0,0,0,0} };
 *   struct table NAME[] = { {VAL, label}, ..., {0,0} };
 *
 * Index tables forward-reference optab arrays, which reference template
 * strings; templates are emitted first, then the #define aliases, then the
 * optab arrays, then the index tables.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

FILE *fdef, *fopt, *fidx;
int intempl, inidx, inopt;

/* append one output byte of a template string, C-escaped, to stdout */
void tbyte(int c)
{
	c &= 0377;
	if (c == '"' || c == '\\')	printf("\\%c", c);
	else if (c == '\n')		printf("\\n");
	else if (c >= 040 && c < 0177)	putchar(c);
	else				printf("\\%o", c);
}

/* emit the <literal> / .byte fragments on one line; return 1 at \0 (end) */
int fragline(char *s)
{
	while (*s) {
		if (*s == '<') {
			s++;
			while (*s && *s != '>') {
				if (*s == '\\') {
					s++;
					if (*s == 'n') { tbyte('\n'); s++; }
					else if (*s == '0') return 1;
					else tbyte('\\');
				} else
					tbyte(*s++);
			}
			if (*s == '>') s++;
		} else if (strncmp(s, ".byte", 5) == 0) {
			s += 5;
			while (*s == ' ' || *s == '\t') s++;
			tbyte((int)strtol(s, &s, 8));
		} else
			s++;
	}
	return 0;
}

/* strip a trailing assembler comment ( / ... ) and any newline/trailing ws */
void clean(char *s)
{
	char *p;
	if ((p = strchr(s, '\n'))) *p = 0;
	for (p = s; *p; p++)
		if (*p == '/') { *p = 0; break; }
	for (p = s + strlen(s); p > s && (p[-1] == ' ' || p[-1] == '\t'); )
		*--p = 0;
}

int main(int argc, char **argv)
{
	char line[4096];

	if (argc > 1 && !freopen(argv[1], "r", stdin)) { perror(argv[1]); return 1; }
	if (argc > 2 && !freopen(argv[2], "w", stdout)) { perror(argv[2]); return 1; }
	fdef = tmpfile(); fopt = tmpfile(); fidx = tmpfile();
	if (!fdef || !fopt || !fidx) { perror("tmpfile"); return 1; }

	printf("/* generated from table.s by cvopt + mktab -- do not edit */\n");
	printf("#include \"c1.h\"\n\n");

	while (fgets(line, sizeof line, stdin)) {
		char *s = line, *p;
		while (*s == ' ' || *s == '\t') s++;

		if (intempl) {				/* template continuation */
			if (fragline(s)) { printf("\";\n"); intempl = 0; }
			continue;
		}
		if (*s == '/' || *s == '\n' || *s == 0) continue;
		if (!strncmp(s, ".globl", 6) || !strncmp(s, ".data", 5) ||
		    !strncmp(s, ".text", 5) || !strncmp(s, ".even", 5)) continue;

		/* a template/label definition line begins with  ident: */
		if (isalpha((unsigned char)*s) || *s == '_') {
			for (p = s; isalnum((unsigned char)*p) || *p == '_'; ) p++;
			if (*p == ':') {
				char *first = 0, *id;
				for (;;) {
					id = p = s;
					while (isalnum((unsigned char)*p) || *p == '_') p++;
					if (*p != ':' || p == id) break;
					*p++ = 0;
					if (!first) first = id;
					else fprintf(fdef, "#define %s\t%s\n", id, first);
					while (*p == ';' || *p == ' ' || *p == '\t') p++;
					s = p;
				}
				/* s/p now point at the template body (<...> or .byte) */
				printf("static char %s[] = \"", first);
				if (fragline(s)) printf("\";\n");
				else intempl = 1;
				continue;
			}
		}

		/* structural lines (no leading label): strip comments first */
		clean(s);
		if (*s == 0) continue;

		if ((p = strchr(s, '='))) {
			if (p[1] == '.' && s[0] == '_' && p[2] != '+') {	/* index */
				*p = 0;
				if (inidx) fprintf(fidx, "\t{0,0}\n};\n");
				if (inopt) { fprintf(fopt, "\t{0,0,0,0,0}\n};\n"); inopt = 0; }
				fprintf(fidx, "\nstruct table %s[] = {\n", s + 1);
				inidx = 1;
				continue;
			}
			if (p[1] == '.' && p[2] == '+') {		/* optab label */
				*p = 0;
				if (inopt) fprintf(fopt, "\t{0,0,0,0,0}\n};\n");
				if (inidx) { fprintf(fidx, "\t{0,0}\n};\n"); inidx = 0; }
				fprintf(fopt, "\nstatic struct optab %s[] = {\n", s);
				inopt = 1;
				continue;
			}
			if (p[1] != '.') {				/* alias Ln=name */
				char *lhs = s, *rhs = p + 1;
				*p = 0;
				for (p = lhs + strlen(lhs); p > lhs && (p[-1]==' '||p[-1]=='\t'); ) *--p = 0;
				while (*rhs == ' ' || *rhs == '\t') rhs++;
				fprintf(fdef, "#define %s\t%s\n", lhs, rhs);
				continue;
			}
		}

		if (!strcmp(s, "0")) {				/* table terminator */
			if (inopt) { fprintf(fopt, "\t{0,0,0,0,0}\n};\n"); inopt = 0; }
			else if (inidx) { fprintf(fidx, "\t{0,0}\n};\n"); inidx = 0; }
			continue;
		}

		if (inidx) {					/* VAL.; label */
			int v; char lbl[64];
			if (sscanf(s, "%d.;%63s", &v, lbl) == 2 ||
			    sscanf(s, "%d.; %63s", &v, lbl) == 2)
				fprintf(fidx, "\t{%d, %s},\n", v, lbl);
			continue;
		}
		if (inopt) {					/* .byte d,t;.byte d2,t2 */
			int d1, t1, d2, t2;
			/* cvopt emits these match bytes with %o, so they are OCTAL */
			if (sscanf(s, ".byte %o,%o;.byte %o,%o", &d1,&t1,&d2,&t2) == 4) {
				char tl[4096], *ts;
				if (fgets(tl, sizeof tl, stdin)) {
					ts = tl;
					while (*ts==' '||*ts=='\t') ts++;
					clean(ts);
					fprintf(fopt, "\t{0%o,0%o,0%o,0%o, %s},\n", d1,t1,d2,t2, ts);
				}
			}
			continue;
		}
	}
	if (inopt) fprintf(fopt, "\t{0,0,0,0,0}\n};\n");
	if (inidx) fprintf(fidx, "\t{0,0}\n};\n");

	{	/* emit: aliases, then optab arrays, then index tables */
		char buf[8192]; size_t n; FILE *f; int i;
		FILE *order[3];
		order[0] = fdef; order[1] = fopt; order[2] = fidx;
		printf("\n");
		for (i = 0; i < 3; i++) {
			f = order[i];
			rewind(f);
			while ((n = fread(buf, 1, sizeof buf, f)) > 0)
				fwrite(buf, 1, n, stdout);
		}
	}
	return 0;
}
