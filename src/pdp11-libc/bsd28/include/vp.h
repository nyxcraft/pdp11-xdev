/* see vp(4) */
#define GETSTATE (('v'<<8)+0)
#define	SETSTATE (('v'<<8)+1)
#define BUFWRITE (('v'<<8)+2)	/* async write */

int	sppmode[]	= {0400, 0, 0};	/* enter spp */
int	pltmode[]	= {0200, 0, 0};	/* enter plot */
int	prtmode[]	= {0100, 0, 0};	/* enter print */
int	clrcom[]	= {0404, 0, 0};	/* remote clear, enter spp */
int	termcom[]	= {0240, 0, 0};	/* remote terminate, enter plot */
int	ffcom[]		= {0220, 0, 0};	/* plot mode form feed */
