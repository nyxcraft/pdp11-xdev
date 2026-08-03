typedef	long       	daddr_t;  	/* disk address */
typedef	char *     	caddr_t;  	/* core address */
typedef	unsigned int	ino_t;     	/* i-node number */
typedef	long       	time_t;   	/* a time */
#ifdef	MENLO_KOV
typedef	int		label_t[7];	/* regs 2-7 and __ovno */
#else
typedef	int        	label_t[6]; 	/* program status */
#endif
typedef	int        	dev_t;    	/* device code */
typedef	long       	off_t;    	/* offset in file */
typedef	long		ubadr_t;	/* physical unibus address */
	/* selectors and constructor for device code */
#define	major(x)  	(int)(((unsigned)x>>8))
#define	minor(x)  	(int)(x&0377)
#define	makedev(x,y)	(dev_t)((x)<<8|(y))
