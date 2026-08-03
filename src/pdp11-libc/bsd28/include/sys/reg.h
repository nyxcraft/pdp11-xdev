/*
 * Location of the users' stored
 * registers relative to R0.
 * Usage is u.u_ar0[XX].
 */
#define	R0	(0)
#define	R1	(-2)
#ifdef	MENLO_KOV
#define	R2	(-10)
#define	R3	(-9)
#define	R4	(-8)
#else
#define	R2	(-9)
#define	R3	(-8)
#define	R4	(-7)
#endif
#define	R5	(-6)
#define	R6	(-3)
#define	R7	(1)
#define	PC	(1)
#define	RPS	(2)
#ifdef	MENLO_KOV
#define ROV	(-7)
#endif

#define	TBIT	020		/* PS trace bit */
