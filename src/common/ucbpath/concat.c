/*	@(#)concat.c	2.2	SCCS id keyword	*/
_concat(out, list)
register	char	*out;
register	char	**list;
{
	register	char	*cp;

	while (cp = *list++)
		while (*cp)
			*out++ = *cp++;
	*out = '\0';
}
