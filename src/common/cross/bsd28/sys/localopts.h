/*
 * local system options - included by whoami.h
 */

/*
 *	#define variables have boolean values of 1 or 0, so that
 * bitwise operators in #if's can be used for boolean conditional
 * compilation.
 */

/*
 *	System Changes Which May Have User Visible Effects
 */

#define UCB_QUOTAS	1	/* Dynamic file system quotas */
#define UCB_GRPMAST	1	/* Group master accounts */
#define UCB_NKB		1	/* "n" KB byte system buffers (not just bool) */

#define UCB_PGRP	1	/* Count process limit by process group */
#define UCB_STICKYDIR	1	/* Can't rm file from sticky dir unless owned */

#define UCB_LSTAT	1	/* Extended stat call (for quotas) */
#define	UCB_DKEXT	1	/* Extended I/O Monitoring */
#define UCB_SMINO	1	/* Small inodes (NADDR == 7) */
/*
#define	UCB_LOGIN	1	/* login sys call is available */
/*
#define	UCB_SUBM	1	/* "submit" processing */
/*
#define	UCB_XACC	1	/* extended process accounting */
#define UCB_LOAD	1	/* load average and uptime */
#define UCB_METER	1	/* vmstat performance metering */
#define MENLO_OVLY	1	/* process text overlays */
#define UCB_NTTY	1	/* new tty driver + net line discipline */
#define UCB_LDISC	1	/* know about line disciplines */

/*	Non User Visible Kernel Changes
 *
 * It should not be necessary to use these in user products 
 *
 */

#define UCB_BUFOUT	1	/* Cache buffers moved out of sys data space */
#define UCB_CLIST	1	/* Clists moved out of regular d-space */
#define UCB_MOUNT	1	/* Multiple superblocks per internal buffer */
/*#define UCB_SCCSID	1	/* Put SCCS ID string in each system module */

#define UCB_BHASH	1	/* hashed buffer accessing */
/*
#define UCB_FRCSWAP	1	/* Force swap on expand/fork */

/*
 * Note:
 *	The UCB_NKB and UCB_SMINO flags
 *	require changes to UNIX boot pgms as well as changes to
 *	dump, restore, icheck, dcheck, ncheck, mkfs.
 */
#define	MENLO_ECC	1	/* Allow ECC to operate on RH devices */
#define	UCB_IHASH	1	/* hashed inode table */
#define	UCB_FSFIX	1	/* Write fs info syncronously to limit 
					crash damage */
#define CGL_RTP		1	/* Allow one real time process */

/*
 * Options determined by machine type:
 *	machine type set in whoami.h
 */
#if ((PDP11 <= 40) || (PDP11 == 60))
#define NONSEPARATE
#define MENLO_KOV
#else
#undef NONSEPARATE
#endif
#if ((PDP11 == 44) || (PDP11 == 70))
#define UNIBUS_MAP
#else
#undef UNIBUS_MAP
#endif

/*
 * Standard Bell V7 features you may or may not want
 *
 */
#define ACCT			/* Process accounting */
#define PARITY			/* 11/70 parity	      */
/* #define CLR_SETUID		/* clear set-user ID, setgid bits on write */
