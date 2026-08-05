/* GENERATED from universes.tsv by mkuniverse.py -- do not edit. */
#ifndef PDP11_UNIVERSE_H
#define PDP11_UNIVERSE_H

/* X(name, id, status, desc) for every universe.  status is one of
 * "full", "sim", "planned" -- see universes.tsv. */
#define PDP11_UNIVERSES(X) \
	X(v1, 1, "full", "First Edition UNIX (1971-72); 0405 format at 040014, inline-arg traps; ld+libc target it, apsim runs it") \
	X(v2, 2, "full", "Second Edition UNIX (1972); 0405/0407 mix, both load at 040000 with V1 inline-arg traps; ld+libc target it, apsim runs native 0405 and 0407 (root in ~/unix/v2)") \
	X(v3, 3, "full", "Third Edition UNIX (Feb 1973); no native binaries, but its manual confirms the 8-word 0407 (load-at-0, magic 407, entry 0) + FP11 -- matches our ld output; core syscalls match, mid-range names are a First-Edition/modern hybrid (makdir=14, +dup/pipe/csw/fpe)") \
	X(v4, 4, "full", "Fourth Edition UNIX (1973), first C kernel; manual + native binaries both confirm: 8-word 0407/0410 load-at-0 and the modern syscall table (mknod=14, setgid/getgid/signal/times), the V5/V6 personality (root in ~/unix/v4)") \
	X(v5, 5, "full", "Fifth Edition UNIX (1974); manual + native binaries confirm 8-word 0407/0410 load-at-0 + the modern syscall table (mknod/dup/pipe/gid) -- the V5/V6 personality; universal libc compiles+runs here (root in ~/unix/v5)") \
	X(v6, 6, "full", "Sixth Edition UNIX (1975); manual + native binaries confirm 8-word 0407/0410/0411 (first to add split I&D) load-at-0 + the modern syscall table (getpid=20, ptrace=26, signal=48); universal libc compiles+runs here (root in ~/unix/v6)") \
	X(v7, 7, "full", "Seventh Edition UNIX (1979); the canonical syscall numbering the V7-family libc uses") \
	X(bsd1, 11, "planned", "1BSD (1978), V6-kernel userland; sources in ~/bsd/1bsd") \
	X(bsd2, 20, "planned", "2BSD (1979), V7-kernel userland; sources in ~/bsd/2bsd") \
	X(bsd279, 27, "planned", "2.79BSD (1980), V7-kernel userland; sources in ~/bsd/2.79") \
	X(bsd28, 28, "full", "2.8BSD (1981), Ritchie cc era; FP11 D-format floating point") \
	X(bsd29, 29, "full", "2.9BSD (1983), overlays and split I&D; the default universe") \
	X(bsd210, 210, "full", "2.10BSD (1987), 4.3-style numbering; the universal libc's 4BSD-convention personality serves it (root in ~/bsd/2.10)") \
	X(bsd211, 211, "full", "2.11BSD pl431 (2000), 4.4-style numbering + stack args; the universal libc's 4BSD personality; porting base for new tools") \
	X(sys3, 103, "planned", "UNIX System III (1980), PDP-11 line; sources in ~/unix/sys3") \
	X(sys5v2, 105, "planned", "System V Release 2 (1984), the last SysV with PDP-11 support; table groundable from ~/unix/svr2 (VAX kit)") \
	X(ultrix11v2, 21, "planned", "DEC Ultrix-11 2.0 (1984); binary tape staged in ~/unix/ultrix11/2.0; 1.0 survives as docs only (lineage root = V7M)") \
	X(ultrix11, 31, "planned", "DEC Ultrix-11 3.1 (1986), V7 line + fcntl/ulimit/utssys; staged in ~/unix/ultrix11 (from TUHS)") \
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
#define PDP11_UNIV_SYS5V2	105
#define PDP11_UNIV_ULTRIX11V2	21
#define PDP11_UNIV_ULTRIX11	31

/* apsim kernel personalities, in ERA ORDER (comparisons like
 * `Kern >= PDP11_K_BSD210' select everything from that era on). */
enum pdp11_kern {
	PDP11_K_V1,
	PDP11_K_V56,
	PDP11_K_V7,
	PDP11_K_SYS3,
	PDP11_K_ULTRIX,
	PDP11_K_BSD2X,
	PDP11_K_BSD210,
	PDP11_K_BSD211,
};

/* X(name, id, status, kern, desc) -- the full table for apsim. */
#define PDP11_UNIVERSE_TABLE(X) \
	X("v1", 1, "full", PDP11_K_V1, "First Edition UNIX (1971-72); 0405 format at 040014, inline-arg traps; ld+libc target it, apsim runs it") \
	X("v2", 2, "full", PDP11_K_V1, "Second Edition UNIX (1972); 0405/0407 mix, both load at 040000 with V1 inline-arg traps; ld+libc target it, apsim runs native 0405 and 0407 (root in ~/unix/v2)") \
	X("v3", 3, "full", PDP11_K_V56, "Third Edition UNIX (Feb 1973); no native binaries, but its manual confirms the 8-word 0407 (load-at-0, magic 407, entry 0) + FP11 -- matches our ld output; core syscalls match, mid-range names are a First-Edition/modern hybrid (makdir=14, +dup/pipe/csw/fpe)") \
	X("v4", 4, "full", PDP11_K_V56, "Fourth Edition UNIX (1973), first C kernel; manual + native binaries both confirm: 8-word 0407/0410 load-at-0 and the modern syscall table (mknod=14, setgid/getgid/signal/times), the V5/V6 personality (root in ~/unix/v4)") \
	X("v5", 5, "full", PDP11_K_V56, "Fifth Edition UNIX (1974); manual + native binaries confirm 8-word 0407/0410 load-at-0 + the modern syscall table (mknod/dup/pipe/gid) -- the V5/V6 personality; universal libc compiles+runs here (root in ~/unix/v5)") \
	X("v6", 6, "full", PDP11_K_V56, "Sixth Edition UNIX (1975); manual + native binaries confirm 8-word 0407/0410/0411 (first to add split I&D) load-at-0 + the modern syscall table (getpid=20, ptrace=26, signal=48); universal libc compiles+runs here (root in ~/unix/v6)") \
	X("v7", 7, "full", PDP11_K_V7, "Seventh Edition UNIX (1979); the canonical syscall numbering the V7-family libc uses") \
	X("bsd1", 11, "planned", PDP11_K_V56, "1BSD (1978), V6-kernel userland; sources in ~/bsd/1bsd") \
	X("bsd2", 20, "planned", PDP11_K_V7, "2BSD (1979), V7-kernel userland; sources in ~/bsd/2bsd") \
	X("bsd279", 27, "planned", PDP11_K_V7, "2.79BSD (1980), V7-kernel userland; sources in ~/bsd/2.79") \
	X("bsd28", 28, "full", PDP11_K_BSD2X, "2.8BSD (1981), Ritchie cc era; FP11 D-format floating point") \
	X("bsd29", 29, "full", PDP11_K_BSD2X, "2.9BSD (1983), overlays and split I&D; the default universe") \
	X("bsd210", 210, "full", PDP11_K_BSD210, "2.10BSD (1987), 4.3-style numbering; the universal libc's 4BSD-convention personality serves it (root in ~/bsd/2.10)") \
	X("bsd211", 211, "full", PDP11_K_BSD211, "2.11BSD pl431 (2000), 4.4-style numbering + stack args; the universal libc's 4BSD personality; porting base for new tools") \
	X("sys3", 103, "planned", PDP11_K_SYS3, "UNIX System III (1980), PDP-11 line; sources in ~/unix/sys3") \
	X("sys5v2", 105, "planned", PDP11_K_SYS3, "System V Release 2 (1984), the last SysV with PDP-11 support; table groundable from ~/unix/svr2 (VAX kit)") \
	X("ultrix11v2", 21, "planned", PDP11_K_ULTRIX, "DEC Ultrix-11 2.0 (1984); binary tape staged in ~/unix/ultrix11/2.0; 1.0 survives as docs only (lineage root = V7M)") \
	X("ultrix11", 31, "planned", PDP11_K_ULTRIX, "DEC Ultrix-11 3.1 (1986), V7 line + fcntl/ulimit/utssys; staged in ~/unix/ultrix11 (from TUHS)") \
	/* end */

/* X(alias, canonical) -- accepted alternate spellings. */
#define PDP11_UNIVERSE_ALIASES(X) \
	X("unix72", "v1") \
	X("1bsd", "bsd1") \
	X("2bsd", "bsd2") \
	X("2.79", "bsd279") \
	X("2.8", "bsd28") \
	X("2.8bsd", "bsd28") \
	X("2.9", "bsd29") \
	X("2.9bsd", "bsd29") \
	X("2.10", "bsd210") \
	X("2.10bsd", "bsd210") \
	X("2.11", "bsd211") \
	X("2.11bsd", "bsd211") \
	X("svr2", "sys5v2") \
	X("ultrix-2.0", "ultrix11v2") \
	X("ultrix", "ultrix11") \
	X("ultrix-3.1", "ultrix11") \
	/* end */

/* The universe assumed when PDP11_UNIVERSE is unset. */
#define PDP11_UNIV_DEFAULT_NAME	"bsd29"

#endif /* PDP11_UNIVERSE_H */
