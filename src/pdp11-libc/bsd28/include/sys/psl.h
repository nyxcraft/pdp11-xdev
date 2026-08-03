/*
 * PDP 11 program status word definitions
 */
#define PS_C 01		/* carry bit */
#define PS_V 02		/* overflow bit */
#define PS_Z 04		/* zero bit */
#define PS_N 010		/* negative bit */
#define PS_ALLCC 017		/* all condition code bits on (unlikely) */
#define PS_T 020		/* trace enable bit */
#define PS_CURMOD	0140000	/* current mode  ( all on is user ) */
#define	PS_PRVMOD	030000	/* previous mode ( all on is user ) */	
#define PS_IPL		0340	/* interrupt priority */
#define PS_USERCLR	0340	/* bits that must be clear in user mode */
#endif
