struct map
{
	unsigned short m_size;
	unsigned short m_addr;
};

#ifdef	KERNEL
struct map coremap[CMAPSIZ];	/* space for core allocation */
struct map swapmap[SMAPSIZ];	/* space for swap allocation */
#endif
