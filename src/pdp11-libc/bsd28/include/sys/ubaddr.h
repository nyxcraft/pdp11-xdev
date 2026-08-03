/*
 * union for describing the unibus address
 * of a clist
 */

union	ubaddr {
	ubadr_t	uba_uba;
	struct	{
		int	uba_hi;
		char	*uba_lo;
	} uba_un;
};

ubadr_t	cpaddr();
