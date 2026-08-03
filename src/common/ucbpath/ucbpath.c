/*	@(#)ucbpath.c	2.2	SCCS id keyword	*/
#define NULL 0
char	*getenv();
static	char	*searchpath;

resetfp()
{
	static	char	*path = NULL;
	if (path == NULL) {
		path = getenv("UCBPATH");
		if (path == NULL) {
			path = getenv("PATH");
			if (path == NULL)
				path = "/bin:/usr/bin";
		}
	}
	searchpath = path;
}

char *makefp(bestsofar, buffer, list)
char	*bestsofar;
char	*buffer;
char	**list;
{
	char	*retval;
	register	char	*cp1, *cp2;

	cp2 = searchpath;

	do {
		retval = cp2;
		if (cp2 == NULL) {
			if (bestsofar)
				cp2 = bestsofar;
			else 
				cp2 = "...bin";
		}

		cp1 = buffer;
		while (*cp2 && *cp2 != ':')
			*cp1++ = *cp2++;
		if (*cp2)
			cp2++;
		else
			cp2 = NULL;

	} while (
		(cp1 - buffer < 3  || cp1[-3] != 'b' ||
			cp1[-2] != 'i' || cp1[-1] != 'n')
	);
	cp1 -= 3;

	searchpath = cp2;
	
	_concat(cp1, list);
	return(retval);
}
