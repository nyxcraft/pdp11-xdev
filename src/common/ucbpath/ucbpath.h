/* Prototypes for the UCB $PATH/$UCBPATH search helpers (concat.c, openl.c,
 * openlp.c, ucbpath.c), used by cpp (include search) and ld (-l libraries). */
#ifndef PDP11_UCBPATH_H
#define PDP11_UCBPATH_H

void	_concat(char *out, char **list);
int	openl(char *buffer, int mode, ...);
int	openlp(char *buffer, int mode, ...);
void	resetfp(void);
char	*makefp(char *bestsofar, char *buffer, char **list);

#endif /* PDP11_UCBPATH_H */
