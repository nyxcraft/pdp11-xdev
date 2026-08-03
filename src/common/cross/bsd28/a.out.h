/*
 * Cross-compile version of <a.out.h> for LP64 hosts reading/writing
 * 16-bit PDP-11 2.8BSD a.out binaries.
 *
 * On the PDP-11:  int = 2, unsigned = 2, long = 4 bytes.
 * On an LP64 host: int = 4, unsigned = 4, long = 8 bytes.
 *
 * The field widths below are pinned to the on-disk PDP-11 sizes with
 * the fixed-width types from <stdint.h>.  PDP-11 and x86-64 are both
 * little-endian, so no byte swapping is needed -- only the widths (and,
 * for <ar.h>, struct packing) have to be corrected.
 *
 * Struct/field/macro names match the original include/a.out.h so the
 * tools compile unchanged; they pick up this version via -Icross.
 */
#include <stdint.h>

struct	exec {	/* a.out header -- 8 words, 16 bytes */
	int16_t  	a_magic;	/* magic number */
	uint16_t	a_text; 	/* size of text segment */
	uint16_t	a_data; 	/* size of initialized data */
	uint16_t	a_bss;  	/* size of unitialized data */
	uint16_t	a_syms; 	/* size of symbol table */
	uint16_t	a_entry; 	/* entry point */
	uint16_t	a_unused;	/* not used */
	uint16_t	a_flag; 	/* relocation info stripped */
};
#define NOVL	7
struct	ovlhdr { /* overlay size information (auto-overlays only) */
	int16_t  	max_ovl;	/* maximum ovl size */
	uint16_t	ov_siz[NOVL];	/* size of i'th overlay */
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

struct	nlist {	/* symbol table entry -- 12 bytes */
	char    	n_name[8];	/* symbol name */
	char		n_type;		/* type flag */
	char		n_ovly;		/* overlay # */
	uint16_t	n_value;	/* value */
};

/*
 * The 2.8BSD binutils (nm/size/strip), built with MENLO_OVLY, spell the type
 * and overlay fields nn_type/nn_ovno.  The layout is identical to n_type/
 * n_ovly, so alias them unconditionally -- nm.c includes a.out.h BEFORE
 * whoami.h, so a MENLO_OVLY-gated alias would be evaluated too early.
 */
#define	nn_type	n_type
#define	nn_ovno	n_ovly

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
