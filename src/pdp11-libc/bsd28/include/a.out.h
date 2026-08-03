struct	exec {	/* a.out header */
	int     	a_magic;	/* magic number */
	unsigned	a_text; 	/* size of text segment */
	unsigned	a_data; 	/* size of initialized data */
	unsigned	a_bss;  	/* size of unitialized data */
	unsigned	a_syms; 	/* size of symbol table */
	unsigned	a_entry; 	/* entry point */
	unsigned	a_unused;	/* not used */
	unsigned	a_flag; 	/* relocation info stripped */
};
#define NOVL	7
struct	ovlhdr { /* overlay size information (auto-overlays only) */
	int		max_ovl;	/* maximum ovl size */
	unsigned	ov_siz[NOVL];	/* size of i'th overlay */
};

#define	A_MAGIC1	0407       	/* normal */
#define	A_MAGIC2	0410       	/* read-only text */
#define	A_MAGIC3	0411       	/* separated I&D */
#define	A_MAGIC4	0405       	/* overlay */
#define	A_MAGIC5	0430       	/* auto-overlay (nonseparate) */
#define	A_MAGIC6	0431       	/* auto-overlay (separate)  */

/*
 * Macros which take exec structures as arguments and tell whether
 * the file has a reasonable magic number or offsets to text|symbols.
 */
#define	N_BADMAG(x) \
    (((x).a_magic)!=A_MAGIC1 && ((x).a_magic)!=A_MAGIC2 && \
     ((x).a_magic)!=A_MAGIC3 && ((x).a_magic)!=A_MAGIC4 && \
     ((x).a_magic)!=A_MAGIC5 && ((x).a_magic)!=A_MAGIC6)

#define	N_TXTOFF(x) \
	((x).a_magic==A_MAGIC5 || (x).a_magic==A_MAGIC6 ? \
	 sizeof (struct ovlhdr) + sizeof (struct exec) : sizeof (struct exec))

struct	nlist {	/* symbol table entry */
	char    	n_name[8];	/* symbol name */
	char		n_type;		/* type flag */
	char		n_ovly;		/* overlay # */
	unsigned	n_value;	/* value */
};

		/* values for type flag */
#define	N_UNDF	0	/* undefined */
#define	N_ABS	01	/* absolute */
#define	N_TEXT	02	/* text symbol */
#define	N_DATA	03	/* data symbol */
#define	N_BSS	04	/* bss symbol */
#define	N_TYPE	037
#define	N_REG	024	/* register name */
#define	N_FN	037	/* file name symbol */
#define	N_EXT	040	/* external bit, or'ed in */
#define	FORMAT	"%06o"	/* to print a value */
