/*	@(#)concat.c	2.2	SCCS id keyword	*/
#include "ucbpath.h"

void
_concat(register char *out, register char **list)
{
	register	char	*cp;

	while (cp = *list++)
		while (*cp)
			*out++ = *cp++;
	*out = '\0';
}
