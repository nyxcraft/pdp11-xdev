/*
 * Cross-compile version of <ar.h> for LP64 hosts reading/writing
 * 16-bit PDP-11 2.9BSD archives (the old binary ar format, ARMAG 0177545).
 *
 * On the PDP-11 the header is 26 bytes:
 *   ar_name[14] + ar_date(long,4) + ar_uid(1) + ar_gid(1)
 *   + ar_mode(int,2) + ar_size(long,4).
 * Field widths are pinned to those sizes, and the struct is packed so the
 * host compiler does not insert alignment padding before the 4-byte fields
 * (the PDP-11 only requires 2-byte alignment).
 */
#include <stdint.h>

#define	ARMAG	0177545
struct	ar_hdr {
	char	 ar_name[14];
	int32_t	 ar_date;
	char	 ar_uid;
	char	 ar_gid;
	int16_t	 ar_mode;
	int32_t	 ar_size;
} __attribute__((packed));

/*
 * The PDP-11 stores a 32-bit `long' high-16-bit-word first ("middle-endian"):
 * its two little-endian halves are swapped relative to an LP64/x86 host long.
 * On disk, ar_date and ar_size (and the ranlib __.SYMDEF offsets) are genuine
 * PDP-11 longs, so swap the halves when crossing the disk boundary -- this is
 * what makes our archives byte-identical in layout to authentic 2BSD ar.  The
 * swap is its own inverse, so read and write use the same macro.
 */
#define	PDPL(x)	((int32_t)((((uint32_t)(x) & 0xffffU) << 16) | \
			   (((uint32_t)(x) >> 16) & 0xffffU)))
