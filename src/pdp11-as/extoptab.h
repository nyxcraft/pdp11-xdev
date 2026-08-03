/* Extended keyword tables, selected by --std= (see as.c).  NONE of
   these are in the default table: several names collide with symbols
   in the era corpora (the 2.9 overlay kernels' `mfpt' ^-cast,
   standalone boot's `spl', MACRO-11's `scanc', px's `ldexp'/`stexp'),
   and a builtin name suppresses same-named `^'-alias symbols at
   write-out (authentic 2.8 as behavior).

   Sources: the assemblers' own keyword tables (V1: unix72 as29.s;
   V7: as19.s; 2.9: as19.s -- identical to V7's; 2.11: as0.s) plus,
   for `extended', the DEC hardware line: DCJ11 User's Guide, KEV11
   FIS, 11/60 handbook (MED/XFC), the Commercial Instruction Set
   spec, and the FP11 opcode map. */

/* The mnemonics 2.11BSD's as REALLY added (its as0.s keyword table,
   diffed against V7/2.9): previous-space and PS moves, the J-11
   group, FP11 STST, and keyword spellings for the no-operand and
   condition-code combinations.  --std=newbsd / --isa=bsd211.
   Two motives, both visible in the tree: (1) the kernel's mch.s
   ^-cast defines (mfpi = 6500^tst, spl = 230, since V6) became
   real keywords -- 2.11's mch_*.s spell them natively; (2) the
   rest (csm/tstset/wrtlck/mfps/mtps, used nowhere in the tree)
   complete the DCJ11: every entry below is a J-11 instruction,
   and the set stops exactly at the J-11's edge -- FIS/CIS/MED/XFC,
   which it lacks, live in extoptab (--isa=extended) instead. */
struct op tab211[] = {
	{"mfpt",	01,	07},
	{"spl",		011,	0230},
	{"csm",		015,	07000},
	{"tstset",	015,	07200},
	{"wrtlck",	015,	07300},
	{"mfpi",	015,	06500},
	{"mtpi",	015,	06600},
	{"mfpd",	015,	0106500},
	{"mtpd",	015,	0106600},
	{"mfps",	015,	0106700},
	{"mtps",	015,	0106400},
	{"stst",	015,	0170300},
	{"nop",		01,	0240},
	{"ccc",		01,	0257},
	{"scc",		01,	0277},
	{0, 0, 0}
};

/* The 1972 assembler's SYSCALL-NAME keywords (as29.s lines 42-75):
   `sys write' assembled by name.  Note `wait' here is the SYSCALL
   (7) -- the 1972 table has no WAIT-instruction keyword at all, so
   under --std=v1/v2 the base entry is shadowed out and this wins. */
struct op v1systab[] = {
	{"exit",1,1},{"fork",1,2},{"read",1,3},{"write",1,4},{"open",1,5},
	{"close",1,6},{"wait",1,7},{"creat",1,010},{"link",1,011},
	{"unlink",1,012},{"exec",1,013},{"chdir",1,014},{"time",1,015},
	{"makdir",1,016},{"chmod",1,017},{"chown",1,020},{"break",1,021},
	{"stat",1,022},{"seek",1,023},{"tell",1,024},{"mount",1,025},
	{"umount",1,026},{"setuid",1,027},{"getuid",1,030},{"stime",1,031},
	{"quit",1,032},{"intr",1,033},{"fstat",1,034},{"cemt",1,035},
	{"mdate",1,036},{"stty",1,037},{"gtty",1,040},{"ilgins",1,041},
	{"nice",1,042},
	{0, 0, 0}
};

/* The V4-through-V6 syscall-name keywords: the V4, V5 and V6
   assemblers carry BYTE-IDENTICAL 186-entry keyword tables (their
   own as19.s each), so one dialect serves all three (--std=v4, v5,
   v6).  The 1972 list survived into it minus quit/intr (replaced by
   signal=60), cemt and ilgins; V7 dropped them all.  `wait' is
   still the SYSCALL (7); none of these tables has a
   WAIT-instruction keyword. */
struct op v6systab[] = {
	{"exit",1,1},{"fork",1,2},{"read",1,3},{"write",1,4},{"open",1,5},
	{"close",1,6},{"wait",1,7},{"creat",1,010},{"link",1,011},
	{"unlink",1,012},{"exec",1,013},{"chdir",1,014},{"time",1,015},
	{"makdir",1,016},{"chmod",1,017},{"chown",1,020},{"break",1,021},
	{"stat",1,022},{"seek",1,023},{"tell",1,024},{"mount",1,025},
	{"umount",1,026},{"setuid",1,027},{"getuid",1,030},{"stime",1,031},
	{"fstat",1,034},{"mdate",1,036},{"stty",1,037},{"gtty",1,040},
	{"nice",1,042},{"signal",1,060},
	{0, 0, 0}
};

/* --std=extended: the rest of the DEC hardware line -- instructions
   NO assembler standard ever spelled.  FIS (KEV11), 11/60 MED/XFC,
   the complete Commercial Instruction Set (register and inline
   forms), and FP11 DEC-style names as pure aliases of the era
   f-idiom (the hardware has ONE opcode set; precision is the FD
   mode bit set by setf/setd; ldexp/stexp are the DEC names for
   movie/movei). */
struct op extoptab[] = {
	{"fadd",	010,	075000},
	{"fsub",	010,	075010},
	{"fmul",	010,	075020},
	{"fdiv",	010,	075030},
	{"med",		01,	076600},
	{"xfc",		011,	076700},
	/* CIS: descriptors live in r0-r5; register forms take no operand */
	{"l2dr",	010,	076020},
	{"movc",	01,	076030},
	{"movrc",	01,	076031},
	{"movtc",	01,	076032},
	{"locc",	01,	076040},
	{"skpc",	01,	076041},
	{"scanc",	01,	076042},
	{"spanc",	01,	076043},
	{"cmpc",	01,	076044},
	{"matc",	01,	076045},
	{"addn",	01,	076050},
	{"subn",	01,	076051},
	{"cmpn",	01,	076052},
	{"cvtnl",	01,	076053},
	{"cvtpn",	01,	076054},
	{"cvtnp",	01,	076055},
	{"ashn",	01,	076056},
	{"cvtln",	01,	076057},
	{"l3dr",	010,	076060},
	{"addp",	01,	076070},
	{"subp",	01,	076071},
	{"cmpp",	01,	076072},
	{"cvtpl",	01,	076073},
	{"mulp",	01,	076074},
	{"divp",	01,	076075},
	{"ashp",	01,	076076},
	{"cvtlp",	01,	076077},
	/* inline forms: pointer words to the descriptors follow the
	   opcode in the instruction stream */
	{"movci",	01,	076130},
	{"movrci",	01,	076131},
	{"movtci",	01,	076132},
	{"locci",	01,	076140},
	{"skpci",	01,	076141},
	{"scanci",	01,	076142},
	{"spanci",	01,	076143},
	{"cmpci",	01,	076144},
	{"matci",	01,	076145},
	{"addni",	01,	076150},
	{"subni",	01,	076151},
	{"cmpni",	01,	076152},
	{"cvtnli",	01,	076153},
	{"cvtpni",	01,	076154},
	{"cvtnpi",	01,	076155},
	{"ashni",	01,	076156},
	{"cvtlni",	01,	076157},
	{"addpi",	01,	076170},
	{"subpi",	01,	076171},
	{"cmppi",	01,	076172},
	{"cvtpli",	01,	076173},
	{"mulpi",	01,	076174},
	{"divpi",	01,	076175},
	{"ashpi",	01,	076176},
	{"cvtlpi",	01,	076177},
	/* FP11 DEC-style names */
	{"ldf",		014,	0172400},
	{"ldd",		014,	0172400},
	{"stf",		05,	0174000},
	{"std",		05,	0174000},
	{"movd",	012,	0172400},
	{"addd",	014,	0172000},
	{"subd",	014,	0173000},
	{"muld",	014,	0171000},
	{"divd",	014,	0174400},
	{"cmpd",	014,	0173400},
	{"modd",	014,	0171400},
	{"clrd",	015,	0170400},
	{"negd",	015,	0170700},
	{"absd",	015,	0170600},
	{"tstd",	015,	0170500},
	{"ldcif",	014,	0177000},
	{"ldcid",	014,	0177000},
	{"ldclf",	014,	0177000},
	{"ldcld",	014,	0177000},
	{"stcfi",	05,	0175400},
	{"stcfl",	05,	0175400},
	{"stcdi",	05,	0175400},
	{"stcdl",	05,	0175400},
	{"ldcdf",	014,	0177400},
	{"ldcfd",	014,	0177400},
	{"stcfd",	05,	0176000},
	{"stcdf",	05,	0176000},
	{"ldexp",	014,	0176400},
	{"stexp",	05,	0175000},
	{0, 0, 0}
};
