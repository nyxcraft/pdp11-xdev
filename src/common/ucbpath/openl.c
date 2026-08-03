/*	@(#)openl.c	2.2	SCCS id keyword	*/
#include <errno.h>
#include <stdarg.h>
extern	errno;

/*
 *	openl(buffer, mode, list1,....0)
 *
 * The original gathered the variadic string arguments by taking the
 * address of the last named parameter (&list) and reading the words that
 * followed it in memory.  That assumes all arguments are passed
 * contiguously on the stack -- true on the PDP-11, false on register
 * ABIs like x86-64.  Use stdarg to collect the char* arguments into an
 * array, which _concat() (unchanged, char**-based) consumes.
 */
openl(char *buffer, int mode, ...)
{
	char *argv[64];
	int i = 0;
	char *a;
	va_list ap;

	va_start(ap, mode);
	while ((a = va_arg(ap, char *)) != 0 && i < 63)
		argv[i++] = a;
	va_end(ap);
	argv[i] = 0;

	_concat(buffer, argv);
	return(open(buffer, mode));
}
