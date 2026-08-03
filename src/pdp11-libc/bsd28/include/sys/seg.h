/*
 * KT-11 addresses and bits.
 */

#define	UISD	((physadr)0177600)	/* first user I-space descriptor register */
#define	UISA	((physadr)0177640)	/* first user I-space address register */
#define	UDSA	((physadr)0177660)	/* first user D-space address register */
#define	RO	02		/* access abilities */
#define	RW	06
#define	ED	010		/* extend direction */
#define	TX	020		/* Software: text segment */
#define	ABS	040		/* Software: absolute address */

/*
 * structure used to address
 * a sequence of integers.
 */
physadr	ka6;		/* 11/40 KISA6; 11/45 KDSA6 */

/*
 * address to access 11/70 UNIBUS map
 */


#ifdef UNIBUS_MAP
struct	ubmap	{		/* structure to access Unibus Map registers */
	int	ub_lo;
	int	ub_hi;
};

#define	UBMAP	((struct ubmap *) 0170200)
#define	UBPAGE	020000L		/* size of unibus mapping segment */
#define	SSR3	((physadr) 0172516)
#define	UBMAPON	040

#endif

#ifdef	UCB_BUFOUT
/*
 * definitions for remapping KD5 and KD6 for buffers and clists
 */

#define	KISA0	((physadr) 0172340)
#ifdef	NONSEPARATE
#define	KISA5	((physadr) 0172352)
#define	KDSA5	KISA5
#define	KISD5	((physadr) 0172312)
#define	KDSD5	KISD5
#else
#define	KDSA5	((physadr) 0172372)
#define	KDSD5	((physadr) 0172332)
#endif
#define	KDSA6	((physadr) 0172374)
#define	KDSD6	((physadr) 0172334)
#define	SEG5	((caddr_t) 0120000)

#else

#define	UBMAP	((physadr)0170200)

#endif

#ifdef	UCB_CLIST

/*
 *  These segments are remapped for accessing clists when they
 *  are moved out of normal kernel data space.  The inode tables
 *  live at the normal addresses so there are no concurrent access
 *  conflicts.  Only KDSA1 is used unless NCLIST * (CBSIZE + 2) > 8K.
 */

#ifdef	NONSEPARATE
#define KISA1	((physadr) 0172342)
#define KISD1	((physadr) 0172302)
#else
#define KDSA1	((physadr) 0172362)
#endif

#define	SEG1	((caddr_t) 0020000)

#endif
