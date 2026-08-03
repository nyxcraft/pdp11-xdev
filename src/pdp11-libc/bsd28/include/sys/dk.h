/*
 * Instrumentation
 */
#ifdef	UCB_DKEXT
#define	DKMAX	5
#define	DK_CONT0	2
#define	DK_CONT1	1
#define	DKPRINT		017
/* #define	CONTROL		014 */
struct	dk
{
	int	dk_0busy;
	int	dk_1busy;
	long	dk_0time[2<<(DK_CONT0*2)];
	long	dk_1time[2<<(DK_CONT1*2)];
	long	sys_time[4];
	long	dk_nnumb[DKMAX+1];
	long	dk_wdsn[DKMAX+1];
};
#endif
