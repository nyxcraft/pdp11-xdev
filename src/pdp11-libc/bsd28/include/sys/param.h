#ifndef	UCB_SYSNAME
#include "whoami.h"
#endif

#ifndef	NONSEPARATE	/* Tunable parameters separate I/D System */

/*
 * tunable variables
 */

#ifdef	UCB_BUFOUT
#define	NBUF	30		/* size of buffer cache (must be <=256) */
#define	NABUF	5		/* size of in-address-space buffer pool */
#else
#define	NBUF	20		/* size of buffer cache */
#endif
#define	NINODE	200		/* number of in core inodes */
#define	NFILE	200		/* number of in core file structures */
#define	NMOUNT	8		/* number of mountable file systems */
#define	MAXMEM	(100*32)		/* max core per process - first # is Kw */
#define	MAXUPRC	10		/* max processes per user */
#define MAXSPRC 30		/* for networks, etc; uid < 15 */
#define	SSIZE	20		/* initial stack size (*64 bytes) */
#define	SINCR	20		/* increment of stack (*64 bytes) */
#define	NOFILE	20		/* max open files per process */
#define	CANBSIZ	256		/* max size of typewriter line */
#define	CMAPSIZ	200		/* size of core allocation area */
#define	SMAPSIZ	200		/* size of swap allocation area */
#define	NCALL	20		/* max simultaneous time callouts */
#define	NPROC	150		/* max number of processes */
#define	NTEXT	40		/* max number of pure texts */
# ifdef UCB_CLIST
#define	NCLIST	500		/* max total clist size */
# else
#define NCLIST 100
# endif
#define	HZ	60		/* Ticks/second of the clock */
#define	TIMEZONE (8*60)		/* Minutes westward from Greenwich */
#define	DSTFLAG	1		/* Daylight Saving Time applies in this locality */
#define	MSGBUFS	128		/* Characters saved from error messages */
#define	NCARGS	5120		/* # characters in exec arglist */
#ifdef UCB_METER
#define MAXSLP	20		/* max time a process sleeps */
#endif

#else	/* Tunables for non-separate I/D */


#ifdef	UCB_BUFOUT
#define	NBUF	30		/* size of buffer cache (must be <=256) */
#define	NABUF	3		/* size of in-address-space buffer pool */
#else
#define	NBUF	15		/* size of buffer cache */
#endif
#define	NINODE	40		/* number of in core inodes */
#define	NFILE	40		/* number of in core file structures */
#define	NMOUNT	 6		/* number of mountable file systems */
#define	MAXMEM	(64*32)		/* max core per process - first # is Kw */
#define	MAXUPRC	 7		/* max processes per user */
#define MAXSPRC 20		/* for networks, etc; uid < 15 */
#define	SSIZE	20		/* initial stack size (*64 bytes) */
#define	SINCR	20		/* increment of stack (*64 bytes) */
#define	NOFILE	20		/* max open files per process */
#define	CANBSIZ	256		/* max size of typewriter line */
#define	CMAPSIZ	50		/* size of core allocation area */
#define	SMAPSIZ	50		/* size of swap allocation area */
#define	NCALL	20		/* max simultaneous time callouts */
#define	NPROC	30		/* max number of processes */
#define	NTEXT	15		/* max number of pure texts */
# ifdef UCB_CLIST
#define	NCLIST	500		/* max total clist size */
#else
#define	NCLIST	60		/* max clist for DH sys w in core c-lists */
# endif
#define	HZ	60		/* Ticks/second of the clock */
#define	TIMEZONE (8*60)		/* Minutes westward from Greenwich */
#define	DSTFLAG	1		/* Daylight Saving Time applies in this locality */
#define	MSGBUFS	128		/* Characters saved from error messages */
#define	NCARGS	5120		/* # characters in exec arglist */
#ifdef UCB_METER
#define MAXSLP	20		/* max time a process sleeps */
#endif

#endif		/* of system conditional tunable parameters */


/*
 * priorities
 * probably should not be
 * altered too much
 */

#ifdef	CGL_RTP
#define PRTP	0
#define	PSWP	5
#else
#define	PSWP	0
#endif
#define	PINOD	10
#define	PRIBIO	20
#define	PZERO	25
#define	NZERO	20
#define	PPIPE	26
#define	PWAIT	30
#define	PSLEP	40
#define	PUSER	50

/*
 * signals
 * dont change
 */

#ifndef		NSIG
#include	"signal.h"
#endif


/*
 * fundamental constants of the implementation--
 * cannot be changed easily
 */

#define	NBPW	sizeof(int)	/* number of bytes in an integer */

#ifndef UCB_NKB
#define	BSIZE	512		/* size of secondary block (bytes) */
/* BSLOP can be 0 unless you have a TIU/Spider */
#define	BSLOP	2		/* In case some device needs bigger buffers */
#define	NINDIR	(BSIZE/sizeof(daddr_t))
#define	BMASK	0777		/* BSIZE-1 */
#define	BSHIFT	9		/* LOG2(BSIZE) */
#define	NMASK	0177		/* NINDIR-1 */
#define	NSHIFT	7		/* LOG2(NINDIR) */
#endif

#if UCB_NKB==1
/* remember to change INOPB in ../h/ino.h */
#define CLSIZE	2		/* number of blocks / cluster */
#define	BSIZE	1024		/* size of secondary block (bytes) */
/* BSLOP can be 0 unless you have a TIU/Spider */
#define	BSLOP	2		/* In case some device needs bigger buffers */
#define	NINDIR	(BSIZE/sizeof(daddr_t))
#define	BMASK	01777		/* BSIZE-1 */
#define	BSHIFT	10		/* LOG2(BSIZE) */
#define	NMASK	0377		/* NINDIR-1 */
#define	NSHIFT	8		/* LOG2(NINDIR) */
#endif

#define	UBSIZE	512		/* block size visible to users */
#ifdef	UCB_QUOTAS
#define	QCOUNT	(BSIZE/UBSIZE)	/* BSIZE must always be a multiple of UBSIZE */
#endif

#define	USIZE	16		/* size of user block (*64) */
#define	UBASE	0140000		/* abs. addr of user block */
#define	NULL	0
#define	CMASK	0		/* default mask for file creation */
#define	NODEV	(dev_t)(-1)
#define	ROOTINO	((ino_t)2)	/* i number of all roots */
#define	SUPERB	((daddr_t)1)	/* block number of the super block */
#define	DIRSIZ	14		/* max characters per directory */

#ifndef UCB_NKB
#define	NICINOD	100		/* number of superblock inodes */
#define	NICFREE	50		/* number of superblock free blocks */
#endif

#if UCB_NKB==1
#define	NICINOD	100		/* number of superblock inodes */
#define	NICFREE	50		/* number of superblock free blocks */
#endif

#define	INFSIZE	138		/* size of per-proc info for users */
#define	CBSIZE	14		/* number of chars in a clist block */
#define	CROUND	017		/* clist rounding: sizeof(int *) + CBSIZE - 1*/

#ifdef UCB_NKB
#define PGSIZE	512		/* bytes per addressable disk sector */
#define PGSHIFT	9		/* LOG2(PGSIZE) */
#endif

/*
 * Some macros for units conversion
 */
/* Core clicks (64 bytes) to segments and vice versa */
#define	ctos(x)	((x+127)/128)
#define stoc(x) ((x)*128)

/* Core clicks (64 bytes) to disk blocks */
#define	ctod(x)	((x+7)>>3)

/* inumber to disk address */
#ifndef UCB_NKB
#define	itod(x)	(daddr_t)((((unsigned)x+15)>>3))
#else
#define itod(x) ((daddr_t)((((unsigned)(x)+2*INOPB-1)/INOPB)))
#endif

/* inumber to disk offset */
#ifndef UCB_NKB
#define	itoo(x)	(int)((x+15)&07)
#else
#define itoo(x) ((int)(((x)+2*INOPB-1)%INOPB))
#endif

#if UCB_NKB==1
/* file system blocks to disk blocks and back */
#define fsbtodb(b)	((daddr_t)((daddr_t)(b)<<1))
#define dbtofsb(b)	((daddr_t)((daddr_t)(b)>>1))
#endif

#ifdef UCB_NKB
/* round a number of clicks up to a whole cluster */
#define clrnd(i)	(((i) + (CLSIZE-1)) & ~(CLSIZE-1))
#endif

/* clicks to bytes */
#define	ctob(x)	(x<<6)

/* bytes to clicks */
#define	btoc(x)	((((unsigned)x+63)>>6))

/* major part of a device */
#define	major(x)	(int)(((unsigned)x>>8))

/* minor part of a device */
#define	minor(x)	(int)(x&0377)

/* make a device number */
#define	makedev(x,y)	(dev_t)((x)<<8 | (y))

/* low int of a long */
#define	loint(l)	((int) l & 0177777)

/* high int of a long */
#define	hiint(l)	((int) (l >> 16))

typedef	struct { int r[1]; } *	physadr;
typedef	long		daddr_t;
typedef char *		caddr_t;
typedef	unsigned int	ino_t;
typedef	long		time_t;
#ifdef	MENLO_KOV
typedef	int		label_t[7];	/* regs 2-7 and __ovno */
#else
typedef	int		label_t[6];	/* regs 2-7 */
#endif
typedef	int		dev_t;
typedef	long		off_t;
typedef	long		ubadr_t;	/* physical unibus address */
	/* embarassing crock for Ritchie C compiler */
typedef	int		void;

/*
 * Machine-dependent bits and macros
 */


#define	UMODE	0170000		/* usermode bits */
#define	USERMODE(ps)	((ps & UMODE)==UMODE)

#define	INTPRI	0340		/* Priority bits */
#define	BASEPRI(ps)	((ps & INTPRI) != 0)

#define PSW	((physadr) 0177776)
#define splx(ops)	(PSW->r[0] = ops)

#ifdef UCB_NTTY
# ifndef MIN
#  define	MIN(a,b)	(((a)<(b))? (a):(b))
# endif
#endif
