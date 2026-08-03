/*
 * Mount structure.
 * One allocated on every mount.
 * Used to find the super block.
 */
struct	mount
{
	dev_t	m_dev;		/* device mounted */
	struct buf *m_bufp;	/* pointer to superblock */
	struct inode *m_inodp;	/* pointer to mounted on inode */
#ifdef UCB_MOUNT
	caddr_t	m_caddr;	/* address of superblock data */
#endif
};

#ifdef UCB_MOUNT
#define SBSIZE	(sizeof(struct filsys))	/* superblock size */
#define NSBPFSB	(BSIZE/SBSIZE)	/* number of superblocks per filesystem block */
#endif
# ifdef KERNEL
struct mount mount[NMOUNT];
# endif
