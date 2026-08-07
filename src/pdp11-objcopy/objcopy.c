/*
 * pdp11-objcopy -- copy or extract a PDP-11 a.out.
 *
 * usage: pdp11-objcopy [-O binary] [-j text|data] infile outfile
 *
 *   (default)      copy infile -> outfile unchanged (identity a.out copy)
 *   -O binary      write the loadable image: text + initialized data, no
 *                  header and no symbol table -- the bytes the loader maps
 *   -j text        write only the text segment, raw
 *   -j data        write only the initialized-data segment, raw
 *
 * PDP-11 is an a.out-only world (unlike the VAX tree, there is no ELF target
 * the modern binutils understand), so the useful objcopy operations here are
 * loadable-image and segment extraction; removing the symbol table/relocation
 * is pdp11-strip's job.  Text and data are contiguous on disk after the
 * 16-byte header for the ordinary magics 0407/0410/0411; the First Edition
 * (0405) and auto-overlay (0430/0431) layouts differ (a header-inclusive text
 * size, or several text segments), so extraction from them is refused rather
 * than silently producing the wrong bytes.
 */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <a.out.h>

static const char *Prog = "pdp11-objcopy";

static void
die(const char *msg, const char *arg)
{
	fprintf(stderr, "%s: %s%s%s\n", Prog, msg, arg ? " " : "", arg ? arg : "");
	exit(1);
}

/* Slurp a whole file into a malloc'd buffer; *len gets its size. */
static unsigned char *
slurp(const char *path, long *len)
{
	struct stat st;
	unsigned char *buf;
	int fd;
	long n, off;

	if ((fd = open(path, O_RDONLY)) < 0)
		die("cannot open", path);
	if (fstat(fd, &st) < 0 || st.st_size < 0)
		die("cannot stat", path);
	*len = (long)st.st_size;
	buf = malloc(*len ? (size_t)*len : 1);
	if (buf == NULL)
		die("out of memory reading", path);
	for (off = 0; off < *len; off += n) {
		n = (long)read(fd, buf + off, (size_t)(*len - off));
		if (n <= 0)
			die("read error on", path);
	}
	close(fd);
	return buf;
}

static void
emit(const char *path, const unsigned char *p, long n)
{
	FILE *fo = fopen(path, "wb");

	if (fo == NULL)
		die("cannot create", path);
	if (n > 0 && fwrite(p, 1, (size_t)n, fo) != (size_t)n)
		die("write error on", path);
	if (fclose(fo) != 0)
		die("write error on", path);
}

int
main(int argc, char **argv)
{
	const char *ofmt = NULL, *seg = NULL, *in, *out;
	unsigned char *buf;
	struct exec hdr;
	long len, txtoff, tsize, dsize;
	int i;

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (strcmp(argv[i], "-O") == 0 && i + 1 < argc)
			ofmt = argv[++i];
		else if (strcmp(argv[i], "-j") == 0 && i + 1 < argc)
			seg = argv[++i];
		else
			die("usage: [-O binary] [-j text|data] infile outfile; bad option", argv[i]);
	}
	if (argc - i != 2)
		die("usage: pdp11-objcopy [-O binary] [-j text|data] infile outfile", NULL);
	in = argv[i];
	out = argv[i + 1];
	if (ofmt && strcmp(ofmt, "binary") != 0)
		die("only -O binary is supported, not", ofmt);
	if (seg && strcmp(seg, "text") != 0 && strcmp(seg, "data") != 0)
		die("-j takes text or data, not", seg);

	buf = slurp(in, &len);
	if (len < (long)sizeof hdr)
		die("not a PDP-11 a.out (too short):", in);
	memcpy(&hdr, buf, sizeof hdr);
	if (N_BADMAG(hdr))
		die("not a PDP-11 a.out (bad magic):", in);

	/* A plain copy with no extraction requested is format-agnostic. */
	if (ofmt == NULL && seg == NULL) {
		emit(out, buf, len);
		return 0;
	}

	/* Extraction needs the contiguous text/data layout of 0407/0410/0411. */
	if (hdr.a_magic == A_MAGIC4 || hdr.a_magic == A_MAGIC5 || hdr.a_magic == A_MAGIC6)
		die("extraction unsupported for First Edition / overlay a.out:", in);
	txtoff = (long)sizeof hdr;
	tsize = hdr.a_text;
	dsize = hdr.a_data;
	/* the header's sizes are attacker-controlled; keep every slice inside the
	 * file we actually read */
	if (txtoff + tsize + dsize > len || tsize < 0 || dsize < 0)
		die("header text/data sizes exceed the file:", in);

	if (seg && strcmp(seg, "text") == 0)
		emit(out, buf + txtoff, tsize);
	else if (seg && strcmp(seg, "data") == 0)
		emit(out, buf + txtoff + tsize, dsize);
	else /* -O binary: the loadable text+data image */
		emit(out, buf + txtoff, tsize + dsize);
	return 0;
}
