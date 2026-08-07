# pdp11-libc -- ONE universal C library for the PDP-11 cross toolchain.
#
# A single libc.a + one crt0 serve every universe.  The era is chosen when a
# program is LINKED, not when the library is built: pdp11-ld (reading
# --universe / $PDP11_UNIVERSE) stamps the absolute symbol __univ = the era id,
# crt0 copies it into a data cell the library can read, and any routine whose
# ABI moved between releases branches on it -- a run-time `if' on a link-time
# constant.  This is the vax11-libc mechanism (see docs/porting.md); on the
# PDP-11 the served pair (2.8BSD, 2.9BSD) share every syscall number, so no
# routine has to dispatch yet -- the machinery is in place for 2.10/2.11.
#
# Built WITH the cross tools in ../../bin (these are PDP-11 objects, not host
# objects).  Installs libc.a + the crt0 variants into ../../lib and the matched
# headers into ../../include -- FLAT, not per-universe, so cc/cpp/ld find them
# for any era (cpp's getincdir and ld both fall back to the flat dir).
#
# Provenance: the sources are 2.9BSD's libc (a functional superset of 2.8BSD --
# identical syscall numbers, superset errno/signal, one struct stat layout), so
# the one library serves both.  Byte-for-byte reproduction of the native
# per-era libc.a is deliberately NOT a goal here (that was the old per-universe
# build); the archive member order is therefore free.

BIN	= ../../bin
LIB	= ../../lib
INC	= ../../include
PREFIX	= pdp11
ASM	= ${BIN}/${PREFIX}-as
CC	= ${BIN}/${PREFIX}-cc
AR	= ${BIN}/${PREFIX}-ar
RANLIB	= ${BIN}/${PREFIX}-ranlib
LSYS	= common/include/sys.s

# crt0 flavours: plain, profiled (-p), FP-trap (-f), FP+profiled.
CRT0S	= crt0 mcrt0 fcrt0 fmcrt0

# ---- the library's intended source set (ported from the 2.9 build) ---------
# gen/: ABI-independent C (string, ctype, numeric, small utilities).  errlst is
# compiled separately (needs -DUCB_NET); the hand-written asm is GENS.
GENC	= strcmp strlen strcpy strcat strncmp strncpy strncat index rindex atoi \
	  atol abs ctype_ getenv mktemp perror qsort swab isatty isapipe stty \
	  sleep malloc calloc atof ecvt crypt ctime loadav rand execvp getlogin \
	  l3 mon mpx nlist tell timezone ttyname ttyslot getwd regex siglist psignal
# NB: fakcu (dummy no-op _cleanup) and fakfp (dummy fptrap) are deliberately
# EXCLUDED -- they are "if none supplied" fallbacks that, once byte-repro member
# ordering is gone, would shadow the REAL _cleanup (flsbuf.o, flushes stdio on
# exit) and the real fptrap (fpsim.o).  Dropping them lets the real ones win.
GENS	= abort cuexit setjmp modf11 ldexp11 frexp11 ldfps
# stdio/: the buffered-I/O layer.  getpwent is compiled without -O (as 2.9 did).
STDIOC	= printf fprintf data strout flsbuf fputc fopen freopen fdopen fgets \
	  fputs fgetc getchar putchar puts gets ungetc fseek ftell rew setbuf \
	  clrerr filbuf findiop endopen rdwr getw putw sprintf gcvt scanf doscan \
	  getpass getpw getpwnam getpwuid getgrent getgrgid getgrnam maxusers \
	  popen system tabify ucount fstab
STDIOS	= doprnt fltpr ffltpr
# sys/: the syscall stubs, assembled against the syscall-number table sys.s.
SYS	= write read open close creat lseek exit sbrk unlink fstat stat lstat \
	  dup pipe fork wait getpid getuid getgid setuid setgid access chmod \
	  chown chdir link kill ioctl umask time stime sync alarm pause nice \
	  times mknod chroot execv execl signal gldav acct execle execve lfstat \
	  lock mount mpxcall phys profil ptrace quota umount utime killpg getppid \
	  qfstat qstat rtp submit reboot renice ucall vfork vhangup
# nonfpcrt/: software long-arithmetic (EIS, no FPU) + csv/cerror -- the no-FPU
# default; fpsim (the FP interpreter) rides along for programs that trap FP.
ARITH	= aldiv almul alrem ldiv lmul lrem mcount

# ---- the archive object list (order is not load-bearing) -------------------
GEN_O	= $(addprefix common/gen/,   $(addsuffix .o, ${GENC} ${GENS} errlst))
STDIO_O	= $(addprefix common/stdio/, $(addsuffix .o, ${STDIOC} ${STDIOS} getpwent))
SYS_O	= $(addprefix common/sys/,   $(addsuffix .o, ${SYS}))
CRT_O	= $(addprefix common/nonfpcrt/, $(addsuffix .o, csv cerror ${ARITH}))
ARCHIVE	= ${GEN_O} ${STDIO_O} ${SYS_O} ${CRT_O} fpsim/fpsim.o

all: headers libc

dirs:
	mkdir -p ${LIB} ${INC}

# Matched header set, installed flat where cpp's getincdir() finds it.
headers: dirs
	mkdir -p ${INC}/sys ${INC}/local
	for f in include/*.h;        do cp -f $$f ${INC}/;       done
	for f in include/sys/*.h;    do cp -f $$f ${INC}/sys/;   done
	-for f in include/local/*.h; do cp -f $$f ${INC}/local/; done
	chmod -R u+w ${INC}

libc: headers dirs
	# crt0 flavours -- one set, installed flat; each stamps __univ at startup.
	# crt0.s defines `exit = 1.' itself and needs no sys.s.
	for f in ${CRT0S}; do ${ASM} -o ${LIB}/$$f.o common/csu/$$f.s; done
	# C sources (cc refuses `-o foo.o', so compile in place via a subshell).
	# -O runs c2, as the authentic 2.9 libc build does.
	cd common/gen   && ${CURDIR}/${CC} -O -c $(addsuffix .c,${GENC})
	# errlst.c: 2.9's sys_errlist[] closing brace is inside #ifdef UCB_NET.
	cd common/gen   && ${CURDIR}/${CC} -O -DUCB_NET -c errlst.c
	cd common/stdio && ${CURDIR}/${CC} -O -c $(addsuffix .c,${STDIOC})
	# getpwent.c -- built without -O, as the target was.
	cd common/stdio && ${CURDIR}/${CC} -c getpwent.c
	# hand-written asm (cuexit does `sys exit', so it needs the number table).
	${ASM} -o common/gen/cuexit.o ${LSYS} common/gen/cuexit.s
	for f in $(filter-out cuexit,${GENS}); do ${ASM} -o common/gen/$$f.o common/gen/$$f.s; done
	for f in ${STDIOS}; do ${ASM} -o common/stdio/$$f.o common/stdio/$$f.s; done
	# syscall stubs: mkdual.py wraps each pristine V7 stub into a DUAL-
	# convention stub (V7 body for __univ<210, a 4BSD `sys N.'-with-stack-args
	# head for >=210, dispatched at run time), reading the 4.x numbers from
	# tools/sysnums.tsv.  Then assemble the generated stubs against sys.s.
	python3 tools/mkdual.py build/sys ${SYS}
	for f in ${SYS}; do ${ASM} -o common/sys/$$f.o ${LSYS} build/sys/$$f.s; done
	# software long-arith + csv/cerror (the no-FPU flavour) and the FP interp.
	for f in csv cerror ${ARITH}; do ${ASM} -o common/nonfpcrt/$$f.o ${LSYS} common/nonfpcrt/$$f.s; done
	${ASM} -o fpsim/fpsim.o ${LSYS} fpsim/fp1.s fpsim/fp2.s fpsim/fp3.s fpsim/fpx.s
	# one archive + table of contents
	rm -f ${LIB}/libc.a
	${AR} rc ${LIB}/libc.a ${ARCHIVE}
	${RANLIB} ${LIB}/libc.a

check:
	@echo "pdp11-libc: exercised by the cc end-to-end suite (../../tests) and oracle/cross-universe.sh"

clean:
	rm -f common/*/*.o fpsim/*.o

.PHONY: all dirs headers libc check clean
