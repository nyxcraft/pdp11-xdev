struct lstat
{
	char	ls_flag;
	char	ls_count;	/* reference count */
	dev_t	ls_dev;		/* device where inode resides */
	ino_t	ls_number;	/* i number, 1-to-1 with device address */
	unsigned short	ls_mode;
	short	ls_nlink;	/* directory entries */
	short	ls_uid;		/* owner */
	short	ls_gid;		/* group of owner */
	off_t	ls_size;		/* size of file */
	union {
		struct {
			daddr_t	ls_addr[NADDR];	/* if normal file/directory */
			daddr_t	ls_lastr;	/* last logical block read (for read-ahead) */
		};
		struct {
			daddr_t	ls_rdev;			/* ls_addr[0] */
			struct group	ls_group;	/* multiplexor group file */
		};
#ifdef	UCB_QUOTAS
		struct {
			daddr_t	ls_qused;
			daddr_t	ls_qmax;
		};
#endif
	} ls_un;
	time_t	ls_atime;	/* access time */
	time_t	ls_mtime;	/* mod time */
	time_t	ls_ctime;	/* creation time */
};
