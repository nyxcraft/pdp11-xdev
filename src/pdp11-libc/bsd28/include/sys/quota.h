#ifdef	UCB_QUOTAS

#define	isquot(ip)	((ip->i_mode & IFMT) == IFQUOT)
#ifdef	QUOTADEBUG
#define	qprint(x)	if (qdebug & x) printf
extern int	qdebug;
#else
#define qprint(x)	
#endif

#endif
