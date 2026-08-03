/* GENERATED from universes.tsv by mkuniverse.py -- do not edit. */
#ifndef PDP11_UNIVERSE_H
#define PDP11_UNIVERSE_H

/* X(name, id, status, desc) for every universe.  status is one of
 * "full", "sim", "planned" -- see universes.tsv. */
#define PDP11_UNIVERSES(X) \
	X(v1, 1, "sim", "First Edition UNIX (1971-72); apsim personality for 0405 binaries") \
	X(v5, 5, "planned", "Fifth Edition UNIX (1974); sources in ~/unix/v5") \
	X(v6, 6, "planned", "Sixth Edition UNIX (1975); sources in ~/unix/v6") \
	X(v7, 7, "sim", "Seventh Edition UNIX (1979); apsim's syscall layer is the V7 set") \
	X(bsd28, 28, "full", "2.8BSD (1981), Ritchie cc era; FP11 D-format floating point") \
	X(bsd29, 29, "full", "2.9BSD (1983), overlays and split I&D; the default universe") \
	X(bsd210, 210, "planned", "2.10BSD (1987); sources in ~/bsd/2.10, das/apsim know its stat renumbering") \
	X(bsd211, 211, "planned", "2.11BSD pl431 (2000), string-table a.out; porting base for new tools, sources in ~/bsd/2.11") \
	X(sys3, 103, "planned", "UNIX System III (1980), PDP-11 line; sources in ~/unix/sys3") \
	/* end */

#define PDP11_UNIV_V1	1
#define PDP11_UNIV_V5	5
#define PDP11_UNIV_V6	6
#define PDP11_UNIV_V7	7
#define PDP11_UNIV_BSD28	28
#define PDP11_UNIV_BSD29	29
#define PDP11_UNIV_BSD210	210
#define PDP11_UNIV_BSD211	211
#define PDP11_UNIV_SYS3	103

/* The universe assumed when PDP11_UNIVERSE is unset. */
#define PDP11_UNIV_DEFAULT_NAME	"bsd29"

#endif /* PDP11_UNIVERSE_H */
