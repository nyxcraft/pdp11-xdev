/*
 * Cross-compile version of <ar.h> for LP64 hosts reading/writing
 * 16-bit PDP-11 2.8BSD archives (the old binary ar format, ARMAG 0177545).
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
