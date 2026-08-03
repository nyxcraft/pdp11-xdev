/* GENERATED from universes.tsv by mkuniverse.py -- do not edit. */
#ifndef PDP11_UNIVERSE_H
#define PDP11_UNIVERSE_H

/* X(name, id, status, desc) for every universe.  status is one of
 * "full", "sim", "planned" -- see universes.tsv. */
#define PDP11_UNIVERSES(X) \
	X(v1, 1, "sim", "First Edition UNIX (1971-72); apsim personality for 0405 binaries") \
	X(v2, 2, "sim", "Second Edition UNIX (1972); 0405 format, runs on the V1 personality; sources in ~/unix/v2") \
	X(v3, 3, "planned", "Third Edition UNIX (1973); no complete source tree staged") \
	X(v4, 4, "planned", "Fourth Edition UNIX (1973), first C kernel; sources in ~/unix/v4") \
	X(v5, 5, "planned", "Fifth Edition UNIX (1974); full root with binaries in ~/unix/v5") \
	X(v6, 6, "planned", "Sixth Edition UNIX (1975); full root with binaries in ~/unix/v6") \
	X(v7, 7, "sim", "Seventh Edition UNIX (1979); apsim's syscall layer is the V7 set") \
	X(bsd1, 11, "planned", "1BSD (1978), V6-kernel userland; sources in ~/bsd/1bsd") \
	X(bsd2, 20, "planned", "2BSD (1979), V7-kernel userland; sources in ~/bsd/2bsd") \
	X(bsd279, 27, "planned", "2.79BSD (1980), V7-kernel userland; sources in ~/bsd/2.79") \
	X(bsd28, 28, "full", "2.8BSD (1981), Ritchie cc era; FP11 D-format floating point") \
	X(bsd29, 29, "full", "2.9BSD (1983), overlays and split I&D; the default universe") \
	X(bsd210, 210, "planned", "2.10BSD (1987); sources in ~/bsd/2.10, das/apsim know its stat renumbering") \
	X(bsd211, 211, "planned", "2.11BSD pl431 (2000), string-table a.out; porting base for new tools, sources in ~/bsd/2.11") \
	X(sys3, 103, "planned", "UNIX System III (1980), PDP-11 line; sources in ~/unix/sys3") \
	/* end */

#define PDP11_UNIV_V1	1
#define PDP11_UNIV_V2	2
#define PDP11_UNIV_V3	3
#define PDP11_UNIV_V4	4
#define PDP11_UNIV_V5	5
#define PDP11_UNIV_V6	6
#define PDP11_UNIV_V7	7
#define PDP11_UNIV_BSD1	11
#define PDP11_UNIV_BSD2	20
#define PDP11_UNIV_BSD279	27
#define PDP11_UNIV_BSD28	28
#define PDP11_UNIV_BSD29	29
#define PDP11_UNIV_BSD210	210
#define PDP11_UNIV_BSD211	211
#define PDP11_UNIV_SYS3	103

/* The universe assumed when PDP11_UNIVERSE is unset. */
#define PDP11_UNIV_DEFAULT_NAME	"bsd29"

#endif /* PDP11_UNIVERSE_H */
