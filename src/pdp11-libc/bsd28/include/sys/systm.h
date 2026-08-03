/*
 * Random set of variables
 * used by more than one
 * routine.
 */
char	canonb[CANBSIZ];	/* buffer for erase and kill (#@) */
struct	inode	*rootdir;	/* pointer to inode of root directory */
struct	proc	*runq;		/* head of linked list of running processes */
int	cputype;		/* type of cpu =40, 45, or 70 */
int	lbolt;			/* time of day in 60th not in time */
time_t	time;			/* time in sec from 1970 */

/*
 * Nblkdev is the number of entries
 * (rows) in the block switch. It is
 * set in binit/bio.c by making
 * a pass over the switch.
 * Used in bounds checking on major
 * device numbers.
 */
int	nblkdev;

/*
 * Number of character switch entries.
 * Set by cinit/tty.c
 */
int	nchrdev;

int	mpid;			/* generic for unique process id's */
char	runin;			/* scheduling flag */
char	runout;			/* scheduling flag */
char	runrun;			/* scheduling flag */
char	curpri;			/* more scheduling */
unsigned	maxmem;		/* actual max memory per process */
physadr	lks;			/* pointer to clock device */
daddr_t	swplo;			/* block number of swap space */
int	nswap;			/* size of swap space */
int	updlock;		/* lock for sync */
daddr_t	rablock;		/* block to be read ahead */
extern	char	regloc[];	/* locs. of saved user registers (trap.c) */
extern	char	msgbuf[];
/*
char	msgbuf[MSGBUFS]={0};	/* saved "printf" characters */
dev_t	rootdev;		/* device of the root */
dev_t	swapdev;		/* swapping device */
dev_t	pipedev;		/* pipe device */
extern	int	icode[];	/* user init code */
extern	int	szicode;	/* its size */
#ifdef	CGL_RTP
struct proc *rtpp;		/* pointer to real time process entry */
#endif
#ifdef UCB_LOAD
time_t	bootime;
#endif

dev_t getmdev();
daddr_t	bmap();
struct inode *ialloc();
struct inode *iget();
struct inode *owner();
struct inode *maknode();
struct inode *namei();
struct buf *alloc();
struct buf *getblk();
struct buf *geteblk();
struct buf *bread();
struct buf *breada();
struct filsys *getfs();
struct file *getf();
struct file *falloc();
int	uchar();
#ifdef	UCB_BUFOUT
caddr_t	mapin();
int	mapout();
#endif

/*
 * Instrumentation
 */
#ifdef	UCB_DKEXT
#define DK_MAX	5	/* DK_Ns 3 and 4 are reserved for system (clock.c) */

int	dk_busy;
long	dk_time[2 << DK_MAX];
long	dk_numb[DK_MAX+1];
long	dk_wds[DK_MAX+1];

#else

int	dk_busy;
long	dk_time[32];
long	dk_numb[3];
long	dk_wds[3];

#endif

long	tk_nin;
long	tk_nout;

/*
 * Structure of the system-entry table
 */
extern struct sysent {			/* system call entry table */
	char	sy_narg;		/* total number of arguments */
	char	sy_nrarg;		/* number of args in registers */
	int	(*sy_call)();		/* handler */
} sysent[];

extern struct sysent syslocal[];	/* local system call entry table */

#define	SYSINDIR	0		/* ordinal of indirect sys call */
#define	SYSLOCAL	58		/* ordinal of local indirect call */
