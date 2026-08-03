/*	@(#)openlp.c	2.2	SCCS id keyword	*/

#include <errno.h>
#include <stdarg.h>
extern	errno;
char *makefp();

/*
 *	openlp(buffer, mode, list1,....0)
 *
 * Like openl(), but searches each "...bin" element of $PATH/$UCBPATH for
 * the constructed name (see makefp/ucbpath.c).  The variadic suffix list
 * is collected into an array via stdarg rather than read off the stack,
 * so it works on register-argument ABIs (x86-64); makefp() then consumes
 * it as a plain char** as before.
 */
openlp(char *buffer, int mode, ...)
{
	char *argv[64];
	int n = 0;
	char *a;
	va_list ap;
	char *bestpath = 0;
	int besterr = ENOENT;
	int fd;
	char *p;

	va_start(ap, mode);
	while ((a = va_arg(ap, char *)) != 0 && n < 63)
		argv[n++] = a;
	va_end(ap);
	argv[n] = 0;

	resetfp();
	while (p = makefp(bestpath, buffer, argv)) {
		fd = open(buffer, mode);
		if (fd >= 0)
			return(fd);
		if (errno != ENOENT && besterr == ENOENT) {
			besterr = errno;
			bestpath = p;
		}
	}
	errno = besterr;
	return(-1);
}
