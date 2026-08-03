/*
 * local definitions for bussiplexer
 */

#define	BXADDR	((struct bxregs *) 0160200) /* unibus address */
#define	NBX	128		/* number of bx lines */
#define	NCPU	10		/* number of lines that are cpu's */
#define MASTER	004		/* default master */
#define	ADDR	004		/* address of this device */
#define	SETHOST	"H 004\r"	/* message to set host to this device */
#define	LIMIT	32		/* max number of simultaneously open lines */
#define	MDB			/* MDB interface board */

#define	LOCALSTOP		/* start/stop processing done in driver */
