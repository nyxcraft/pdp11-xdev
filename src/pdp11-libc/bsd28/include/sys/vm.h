#ifdef UCB_METER

/* system totals computed every five seconds */
struct vmmeter
{
#define v_first	v_swtch
	long v_swtch;	/* context switches */
	long v_trap;	/* calls to trap */
	long v_syscall;	/* calls to syscall() */
	long v_intr;	/* device interrupts */
	long v_ovly;	/* overlay emts */
	long v_pswpin;	/* pages swapped in */
	long v_pswpout;	/* pages swapped out */
	long v_swpin;	/* swapins */
	long v_swpout;	/* swapouts */
#define v_last v_swpout
};

struct vmmeter cnt, rate, sum;

struct vmtotal
{
	unsigned	t_rq;		/* length of run queue */
	unsigned	t_dw;		/* jobs in ``disk wait'' (neg priority) */
	unsigned	t_pw;		/* jobs in page wait (not used) */
	unsigned	t_sl;		/* joobs sleeping in core */
	unsigned	t_sw;		/* swapped out runnable/short block jobs */
	long		t_vm;		/* total virtual memory */
	long		t_avm;		/* active virtual memory */
	unsigned	t_rm;		/* total real memory */
	unsigned	t_arm;		/* active real memory */
	long		t_vmtxt;	/* virtual memory used by text */
	long		t_avmtxt;	/* active virtual memory used by text */
	unsigned	t_rmtxt;	/* real memory used by text */
	unsigned	t_armtxt;	/* active real memory used by text */
	unsigned	t_free;		/* free memory pages */
};
struct vmtotal	total;

int	avefree, freemem;

#endif
