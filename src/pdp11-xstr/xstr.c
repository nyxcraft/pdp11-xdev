/* Copyright (c) 1979 Regents of the University of California */
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * xstr - extract and hash strings in a C program
 *
 * Bill Joy UCB
 * November, 1978
 */

#define ignore(a) Ignore((char *)(long)a)

/* Forward declarations for file-local functions. */
static void process(char *name);
static off_t yankstr(char **cpp);
static int octdigit(char c);
static void inithash(void);
static int fgetNUL(char *obuf, int rmdr, FILE *file);
static int xgetc(FILE *file);
static off_t hashit(char *str, int new);
static void flushsh(void);
static void found(int new, off_t off, char *str);
static void prstr(char *cp);
static void xsdotc(void);
static char *savestr(char *cp);
static void Ignore(char *a);
static void ignorf(void (*a)(int));
static int lastchr(char *cp);
static int istail(char *str, char *of);
static void onintr(int sig);

off_t tellpt;
off_t mesgpt;
char *strings = "strings";

int cflg;
int vflg;
int readstd;

int
main(int argc, char **argv)
{
	argc--, argv++;
	while (argc > 0 && argv[0][0] == '-') {
		register char *cp = &(*argv++)[1];

		argc--;
		if (*cp == 0) {
			readstd++;
			continue;
		}
		do
			switch (*cp++) {
			case 'c':
				cflg++;
				continue;

			case 'v':
				vflg++;
				continue;

			default:
				fprintf(stderr, "usage: xstr [ -v ] [ -c ] [ - ] [ name ... ]\n");
			}
		while (*cp);
	}
	if (signal(SIGINT, SIG_IGN) == SIG_DFL)
		signal(SIGINT, onintr);
	if (cflg || argc == 0 && !readstd)
		inithash();
	else
		strings = savestr("/tmp/xstrXXXXXX");
	{
		int fd = mkstemp(strings);
		if (fd >= 0)
			close(fd);
	}
	while (readstd || argc > 0) {
		if (freopen("x.c", "w", stdout) == NULL)
			perror("x.c"), exit(1);
		if (!readstd && freopen(argv[0], "r", stdin) == NULL)
			perror(argv[0]), exit(2);
		process("x.c");
		if (readstd == 0)
			argc--, argv++;
		else
			readstd = 0;
	};
	flushsh();
	if (cflg == 0)
		xsdotc();
	if (strings[0] == '/')
		ignore(unlink(strings));
	exit(0);
}

static void
process(char *name)
{
	char *cp;
	char linebuf[BUFSIZ];
	register int c;
	register int incomm = 0;

	printf("char\txstr[];\n");
	for (;;) {
		if (fgets(linebuf, sizeof linebuf, stdin) == NULL) {
			if (ferror(stdin)) {
				perror(name);
				exit(3);
			}
			break;
		}
		if (linebuf[0] == '#') {
			if (linebuf[1] == ' ' && isdigit(linebuf[2]))
				printf("#line%s", &linebuf[1]);
			else
				printf("%s", linebuf);
			continue;
		}
		for (cp = linebuf; c = *cp++;)
			switch (c) {
			case '"':
				if (incomm)
					goto def;
				printf("(&xstr[%d])", (int)yankstr(&cp));
				break;

			case '\'':
				if (incomm)
					goto def;
				putchar(c);
				if (*cp)
					putchar(*cp++);
				break;

			case '/':
				if (incomm || *cp != '*')
					goto def;
				incomm = 1;
				cp++;
				printf("/*");
				continue;

			case '*':
				if (incomm && *cp == '/') {
					incomm = 0;
					cp++;
					printf("*/");
					continue;
				}
				goto def;

			def:
			default:
				putchar(c);
				break;
			}
	}
	if (ferror(stdout))
		perror("x.c"), onintr(0);
}

static off_t
yankstr(register char **cpp)
{
	register char *cp = *cpp;
	register int c, ch;
	char dbuf[BUFSIZ];
	register char *dp = dbuf;
	register char *tp;

	while (c = *cp++) {
		switch (c) {
		case '"':
			cp++;
			goto out;

		case '\\':
			c = *cp++;
			if (c == 0)
				break;
			if (c == '\n')
				continue;
			for (tp = "b\bt\tr\rn\nf\f\\\\\"\""; ch = *tp++; tp++)
				if (c == ch) {
					c = *tp;
					goto gotc;
				}
			if (!octdigit(c)) {
				*dp++ = '\\';
				break;
			}
			c -= '0';
			if (!octdigit(*cp))
				break;
			c <<= 3, c += *cp++ - '0';
			if (!octdigit(*cp))
				break;
			c <<= 3, c += *cp++ - '0';
			break;
		}
	gotc:
		*dp++ = c;
	}
out:
	*cpp = --cp;
	*dp = 0;
	return (hashit(dbuf, 1));
}

static int
octdigit(char c)
{
	return (isdigit(c) && c != '8' && c != '9');
}

static void
inithash(void)
{
	char buf[BUFSIZ];
	register FILE *mesgread = fopen(strings, "r");

	if (mesgread == NULL)
		return;
	for (;;) {
		mesgpt = tellpt;
		if (fgetNUL(buf, sizeof buf, mesgread) == 0)
			break;
		ignore(hashit(buf, 0));
	}
	ignore(fclose(mesgread));
}

static int
fgetNUL(char *obuf, register int rmdr, FILE *file)
{
	register int c;
	register char *buf = obuf;

	while (--rmdr > 0 && (c = xgetc(file)) != 0 && c != EOF)
		*buf++ = c;
	*buf++ = 0;
	return ((feof(file) || ferror(file)) ? 0 : 1);
}

static int
xgetc(FILE *file)
{
	tellpt++;
	return (getc(file));
}

#define BUCKETS 128

struct hash {
	off_t hpt;
	char *hstr;
	struct hash *hnext;
	short hnew;
} bucket[BUCKETS];

static off_t
hashit(char *str, int new)
{
	int i;
	register struct hash *hp, *hp0;

	hp = hp0 = &bucket[lastchr(str) & 0177];
	while (hp->hnext) {
		hp = hp->hnext;
		i = istail(str, hp->hstr);
		if (i >= 0)
			return (hp->hpt + i);
	}
	hp = (struct hash *)calloc(1, sizeof(*hp));
	hp->hpt = mesgpt;
	hp->hstr = savestr(str);
	mesgpt += strlen(hp->hstr) + 1;
	hp->hnext = hp0->hnext;
	hp->hnew = new;
	hp0->hnext = hp;
	return (hp->hpt);
}

static void
flushsh(void)
{
	register int i;
	register struct hash *hp;
	register FILE *mesgwrit;
	register int old = 0, new = 0;

	for (i = 0; i < BUCKETS; i++)
		for (hp = bucket[i].hnext; hp != NULL; hp = hp->hnext)
			if (hp->hnew)
				new ++;
			else
				old++;
	if (new == 0 && old != 0)
		return;
	/* 2.9 stdio "a" merely seeked to EOF at open; fseek+fwrite then
	 * landed at the seeked offset.  Modern "a" sets O_APPEND (every
	 * write forced to EOF), which scrambles the offset-addressed
	 * layout and divorces the file from the offsets handed out to
	 * x.c.  "r+" restores the era semantics for accumulation runs.
	 * (The fully-seeded rogue workflow never reaches this write --
	 * new==0 returns above -- so this affects natural accumulation
	 * only.) */
	mesgwrit = fopen(strings, old ? "r+" : "w");
	if (mesgwrit == (FILE *)NULL) {
		perror(strings);
		exit(8);
	}
	for (i = 0; i < BUCKETS; i++)
		for (hp = bucket[i].hnext; hp != NULL; hp = hp->hnext) {
			found(hp->hnew, hp->hpt, hp->hstr);
			if (hp->hnew) {
				fseek(mesgwrit, hp->hpt, 0);
				ignore(fwrite(hp->hstr, strlen(hp->hstr) + 1, 1, mesgwrit));
				if (ferror(mesgwrit))
					perror(strings), exit(4);
			}
		}
	ignore(fclose(mesgwrit));
}

static void
found(int new, off_t off, char *str)
{
	register char *cp;

	if (vflg == 0)
		return;
	if (!new)
		fprintf(stderr, "found at %d:", (int)off);
	else
		fprintf(stderr, "new at %d:", (int)off);
	prstr(str);
	fprintf(stderr, "\n");
}

static void
prstr(register char *cp)
{
	register int c;

	while (c = (*cp++ & 0377))
		if (c < ' ')
			fprintf(stderr, "^%c", c + '`');
		else if (c == 0177)
			fprintf(stderr, "^?");
		else if (c > 0200)
			fprintf(stderr, "\\%03o", c);
		else
			fprintf(stderr, "%c", c);
}

static void
xsdotc(void)
{
	register FILE *strf = fopen(strings, "r");
	register FILE *xdotcf;

	if (strf == NULL)
		perror(strings), exit(5);
	xdotcf = fopen("xs.c", "w");
	if (xdotcf == NULL)
		perror("xs.c"), exit(6);
	fprintf(xdotcf, "char\txstr[] = {\n");
	for (;;) {
		register int i, c;

		for (i = 0; i < 8; i++) {
			c = getc(strf);
			if (ferror(strf)) {
				perror(strings);
				onintr(0);
			}
			if (feof(strf)) {
				fprintf(xdotcf, "\n");
				goto out;
			}
			fprintf(xdotcf, "0x%02x,", c);
		}
		fprintf(xdotcf, "\n");
	}
out:
	fprintf(xdotcf, "};\n");
	ignore(fclose(xdotcf));
	ignore(fclose(strf));
}

static char *
savestr(register char *cp)
{
	register char *dp = (char *)calloc(1, strlen(cp) + 1);

	return (strcpy(dp, cp));
}

static void
Ignore(char *a)
{
	a = a;
}

static void
ignorf(void (*a)(int))
{
	a = a;
}

static int
lastchr(register char *cp)
{
	while (cp[0] && cp[1])
		cp++;
	return (*cp);
}

static int
istail(register char *str, register char *of)
{
	register int d = strlen(of) - strlen(str);

	if (d < 0 || strcmp(&of[d], str) != 0)
		return (-1);
	return (d);
}

static void
onintr(int sig)
{
	ignorf(signal(SIGINT, SIG_IGN));
	if (strings[0] == '/')
		ignore(unlink(strings));
	ignore(unlink("x.c"));
	ignore(unlink("xs.c"));
	exit(7);
}
