#
# pdp11-libc (bsd29 universe) -- the authentic target C library, headers,
# curses and termlib for this era, built WITH the cross tools in ../../../bin.
#
# Derived from the 2.9BSD toolchain's top-level Makefile (see NOTES.md
# for provenance); paths adapted to the pdp11-xdev layout.  The recipes and
# member orders are preserved -- the archive member order is load-bearing for
# byte-identical reproduction of native binaries.
#

UNIV	= bsd29
export PDP11_UNIVERSE = bsd29

BIN	= ../../../bin
LIB	= ../../../lib/bsd29
INC	= ../../../include/bsd29
PREFIX	= pdp11

all: headers libc libc-era curses termlib


dirs:
	mkdir -p ${BIN} ${LIB} ${INC}

# Target (PDP-11) system headers, installed to usr/include where cpp's
# getincdir() finds them (resolved relative to the cpp binary, the same
# scheme cc/ld use) -- mirrors the vax project's `headers' target.  The full
# authentic 2.8BSD header set lives in include/ (and include/sys/); install it
# all so the libc sources find <ctype.h>, <errno.h>, <signal.h>, <sys/...>,
# etc. instead of leaking to the host's /usr/include.
headers: dirs
	mkdir -p ${INC}/sys ${INC}/local
	for f in include/*.h;       do cp -f $$f ${INC}/;       done
	for f in include/sys/*.h;   do cp -f $$f ${INC}/sys/;   done
	for f in include/local/*.h; do cp -f $$f ${INC}/local/; done
	chmod -R u+w ${INC}

ASM  = ${BIN}/${PREFIX}-as
CC   = ${BIN}/${PREFIX}-cc
LSYS = libc/include/sys.s

# gen/ C sources (string, memory, numeric, char-class, small utilities)
LIBC_GENC = strcmp strlen strcpy strcat strncmp strncpy strncat index rindex \
	    atoi atol abs ctype_ getenv mktemp perror errlst qsort swab isatty \
	    isapipe stty sleep malloc calloc atof ecvt \
	    crypt ctime loadav rand \
	    execvp getlogin l3 mon mpx nlist tell timezone ttyname ttyslot \
	    getwd regex siglist psignal

# stdio/ C sources (the full buffered-I/O layer minus the scanf/db family)
LIBC_STDIOC = printf fprintf data strout flsbuf fputc fopen freopen fdopen \
	      fgets fputs fgetc getchar putchar puts gets ungetc fseek ftell rew \
	      setbuf clrerr filbuf findiop endopen rdwr getw putw sprintf gcvt scanf doscan \
	      getpass getpw getpwent getpwnam getpwuid \
	      getgrent getgrgid getgrnam maxusers popen system tabify ucount \
	    fstab

# sys/ syscall stubs (assembled with the sys.s number defs)
LIBC_SYS = write read open close creat lseek exit sbrk unlink fstat stat lstat \
	   dup pipe fork wait getpid getuid getgid setuid setgid access chmod \
	   chown chdir link kill ioctl umask time stime sync alarm pause nice \
	   times mknod chroot execv execl signal gldav \
	   acct execle execve lfstat lock mount mpxcall phys profil ptrace quota \
	   umount utime \
	    killpg getppid qfstat qstat rtp submit reboot renice ucall vfork vhangup

# crt/ long-arithmetic + profiler-counter helpers (c1 emits calls to these)
LIBC_ARITH = lmul ldiv lrem almul aldiv alrem mcount

# Full object list for the archive, in the EXACT member order 2.8's `mklib'
# uses (~/bsd/tuhs/2.8/usr/kernel/src/libc/mklib).  This order is NOT cosmetic:
# `ld' lays out the archive members it pulls in ARCHIVE ORDER (ranlib's
# __.SYMDEF only fixes resolution, not layout), so the member order must match
# 2.8's or the linked library text/data diverges from rogue3.4.obj.  getmap.o
# (a passwd helper not in 2.8's mklib) goes immediately after getpwent.o: in the
# target, getmap's TBL[]="/etc/pw_map" data sits right after getpwent's strings
# (/bin/csh), so getmap.o is adjacent to getpwent.o in the archive.
LIBC_OBJS = \
	libc/stdio/tabify.o libc/stdio/maxusers.o libc/stdio/ucount.o libc/stdio/getgrgid.o \
	libc/stdio/getgrnam.o libc/stdio/getgrent.o libc/stdio/getpass.o libc/stdio/getpwnam.o \
	libc/stdio/getpwuid.o libc/stdio/getpwent.o libc/gen/timezone.o libc/stdio/fgetc.o \
	libc/stdio/fputc.o libc/stdio/getchar.o libc/stdio/putchar.o libc/stdio/popen.o \
	libc/stdio/freopen.o libc/stdio/fgets.o libc/stdio/fputs.o libc/stdio/getpw.o \
	libc/stdio/fseek.o libc/stdio/ftell.o libc/stdio/rew.o libc/stdio/rdwr.o \
	libc/stdio/system.o libc/stdio/fopen.o libc/stdio/fdopen.o libc/stdio/scanf.o \
	libc/stdio/doscan.o libc/stdio/fprintf.o libc/stdio/gets.o libc/stdio/getw.o \
	libc/stdio/printf.o libc/stdio/puts.o libc/stdio/putw.o libc/stdio/sprintf.o \
	libc/stdio/ungetc.o libc/stdio/filbuf.o libc/stdio/setbuf.o libc/stdio/fltpr.o \
	libc/stdio/doprnt.o libc/stdio/gcvt.o libc/stdio/ffltpr.o libc/stdio/strout.o \
	libc/stdio/flsbuf.o libc/stdio/endopen.o libc/stdio/findiop.o libc/stdio/clrerr.o \
	libc/stdio/data.o libc/gen/cuexit.o libc/gen/execvp.o libc/gen/getenv.o \
	libc/gen/getlogin.o libc/gen/perror.o libc/gen/sleep.o libc/gen/ttyslot.o \
	libc/gen/ttyname.o libc/gen/abort.o libc/gen/abs.o libc/gen/atof.o \
	libc/gen/atoi.o libc/gen/atol.o libc/gen/crypt.o libc/gen/ctime.o \
	libc/gen/calloc.o libc/gen/malloc.o libc/gen/ecvt.o libc/gen/errlst.o \
	libc/gen/fakcu.o libc/gen/fakfp.o libc/gen/frexp11.o libc/gen/isatty.o \
	libc/gen/isapipe.o libc/gen/l3.o libc/gen/ldexp11.o libc/gen/ldfps.o \
	libc/gen/mktemp.o libc/gen/modf11.o libc/gen/mon.o libc/gen/mpx.o \
	libc/gen/nlist.o libc/gen/qsort.o libc/gen/rand.o libc/gen/setjmp.o \
	libc/gen/stty.o libc/gen/swab.o libc/gen/tell.o libc/gen/ctype_.o \
	libc/gen/index.o libc/gen/rindex.o libc/gen/strcat.o libc/gen/strncat.o \
	libc/gen/strcmp.o libc/gen/strncmp.o libc/gen/strcpy.o libc/gen/strncpy.o \
	libc/gen/strlen.o libc/sys/access.o libc/sys/acct.o libc/sys/alarm.o \
	libc/sys/chdir.o libc/sys/chroot.o libc/sys/chmod.o libc/sys/chown.o \
	libc/sys/close.o libc/sys/creat.o libc/sys/dup.o libc/sys/execl.o \
	libc/sys/execle.o libc/sys/execv.o libc/sys/execve.o libc/sys/exit.o \
	libc/sys/fork.o libc/sys/fstat.o libc/sys/getgid.o libc/sys/getpid.o \
	libc/sys/getuid.o libc/sys/ioctl.o libc/sys/kill.o libc/sys/link.o \
	libc/sys/lock.o libc/sys/lseek.o libc/sys/lfstat.o libc/sys/lstat.o \
	libc/sys/mknod.o libc/sys/mount.o libc/sys/mpxcall.o libc/sys/nice.o \
	libc/sys/open.o libc/sys/pause.o libc/sys/phys.o libc/sys/pipe.o \
	libc/sys/profil.o libc/sys/ptrace.o libc/sys/quota.o libc/sys/read.o \
	libc/sys/sbrk.o libc/sys/setgid.o libc/sys/setuid.o libc/sys/signal.o \
	libc/sys/stat.o libc/sys/stime.o libc/sys/sync.o libc/sys/time.o \
	libc/sys/times.o libc/sys/umask.o libc/sys/umount.o libc/sys/unlink.o \
	libc/sys/utime.o libc/sys/wait.o libc/sys/write.o libc/crt/aldiv.o \
	libc/crt/almul.o libc/crt/alrem.o libc/crt/cerror.o libc/crt/ldiv.o \
	libc/crt/lmul.o libc/crt/lrem.o libc/crt/mcount.o libc/sys/gldav.o \
	libc/crt/csv.o libc/gen/loadav.o

# libnonfpc.a is the same library with the crt taken from nonfpcrt/ (rogue's
# older software ldiv/lrem) instead of crt/, plus the fpsim interpreter -- the
# authentic non-FP `fmklib' build.  Same mklib member order; crt members come
# from nonfpcrt/, fpsim.o appended last.
LIBNONFPC_OBJS = \
	libc/gen/abort.o libc/gen/abs.o libc/sys/acct.o libc/nonfpcrt/aldiv.o \
	libc/nonfpcrt/alrem.o libc/gen/atol.o libc/gen/calloc.o libc/sys/chdir.o \
	libc/sys/chmod.o libc/sys/chown.o libc/sys/chroot.o libc/stdio/clrerr.o \
	libc/gen/crypt.o libc/gen/ctime.o libc/sys/execle.o libc/sys/execve.o \
	libc/gen/execvp.o libc/sys/execv.o libc/stdio/fstab.o libc/stdio/fgetc.o \
	fpsim/fpsim.o libc/stdio/fputc.o libc/stdio/fputs.o libc/stdio/freopen.o \
	libc/gen/frexp11.o libc/stdio/fseek.o libc/stdio/ftell.o libc/stdio/getchar.o \
	libc/gen/getenv.o libc/sys/getgid.o libc/stdio/getgrgid.o libc/stdio/getgrnam.o \
	libc/stdio/getgrent.o libc/gen/getlogin.o libc/stdio/getpass.o \
	libc/gen/getwd.o libc/stdio/fprintf.o libc/stdio/getpw.o libc/stdio/getpwnam.o \
	libc/stdio/getpwuid.o libc/stdio/getpwent.o libc/gen/atoi.o libc/stdio/fgets.o \
	libc/stdio/gets.o libc/sys/getuid.o libc/stdio/getw.o libc/gen/index.o \
	libc/gen/isapipe.o libc/sys/kill.o libc/sys/killpg.o libc/gen/l3.o \
	libc/gen/ldfps.o libc/sys/link.o libc/gen/loadav.o libc/sys/gldav.o \
	libc/sys/lock.o libc/nonfpcrt/lrem.o libc/nonfpcrt/mcount.o libc/sys/mknod.o \
	libc/gen/mktemp.o libc/sys/access.o libc/sys/getpid.o libc/sys/getppid.o \
	libc/gen/mon.o libc/nonfpcrt/ldiv.o libc/sys/mount.o libc/gen/mpx.o \
	libc/sys/mpxcall.o libc/sys/nice.o libc/gen/nlist.o libc/nonfpcrt/almul.o \
	libc/sys/phys.o libc/stdio/popen.o libc/sys/dup.o libc/stdio/fdopen.o \
	libc/sys/pipe.o libc/stdio/printf.o libc/sys/profil.o libc/sys/ptrace.o \
	libc/stdio/putchar.o libc/stdio/puts.o libc/stdio/putw.o libc/sys/qfstat.o \
	libc/gen/qsort.o libc/sys/qstat.o libc/sys/quota.o libc/gen/rand.o \
	libc/gen/regex.o libc/nonfpcrt/lmul.o libc/stdio/rew.o libc/sys/rtp.o \
	libc/stdio/scanf.o libc/stdio/doscan.o libc/gen/atof.o libc/gen/ctype_.o \
	libc/gen/ldexp11.o libc/stdio/setbuf.o libc/sys/setgid.o libc/sys/setuid.o \
	libc/gen/sleep.o libc/sys/alarm.o libc/sys/pause.o libc/gen/setjmp.o \
	libc/gen/siglist.o libc/sys/stime.o libc/gen/strncat.o libc/gen/strncmp.o \
	libc/gen/strncpy.o libc/sys/submit.o libc/gen/swab.o libc/sys/sync.o \
	libc/stdio/system.o libc/sys/execl.o libc/sys/exit.o libc/sys/fork.o \
	libc/sys/signal.o libc/stdio/tabify.o libc/gen/tell.o libc/sys/time.o \
	libc/sys/times.o libc/gen/timezone.o libc/stdio/sprintf.o libc/stdio/fltpr.o \
	libc/stdio/doprnt.o libc/stdio/ffltpr.o libc/stdio/strout.o libc/gen/ttyslot.o \
	libc/gen/rindex.o libc/gen/strcmp.o libc/gen/ttyname.o libc/sys/fstat.o \
	libc/sys/stat.o libc/gen/strcat.o libc/gen/strcpy.o libc/stdio/fopen.o \
	libc/stdio/endopen.o libc/sys/creat.o libc/stdio/findiop.o libc/sys/lseek.o \
	libc/sys/open.o libc/gen/perror.o libc/gen/psignal.o libc/gen/errlst.o \
	libc/stdio/rdwr.o libc/stdio/filbuf.o libc/stdio/flsbuf.o libc/gen/cuexit.o \
	libc/gen/fakcu.o libc/sys/close.o libc/stdio/data.o libc/gen/isatty.o \
	libc/gen/malloc.o libc/sys/read.o libc/sys/reboot.o libc/sys/renice.o \
	libc/sys/sbrk.o libc/gen/strlen.o libc/gen/stty.o libc/sys/ioctl.o \
	libc/sys/ucall.o libc/sys/umask.o libc/sys/umount.o libc/stdio/ungetc.o \
	libc/sys/unlink.o libc/sys/utime.o libc/sys/vfork.o libc/sys/vhangup.o \
	libc/sys/wait.o libc/sys/write.o libc/nonfpcrt/cerror.o libc/stdio/gcvt.o \
	libc/gen/ecvt.o libc/gen/modf11.o libc/nonfpcrt/csv.o libc/stdio/ucount.o \
	libc/stdio/maxusers.o libc/sys/lfstat.o libc/sys/lstat.o libc/gen/fakfp.o



# ---------------------------------------------------------------------
# Floating point.  rogue3.4.obj was linked against the non-FP libc -- in 2.8
# the `fmklib' build: the nonfpcrt software csv/ldiv/lrem (an older
# long-division revision) plus the bundled fpsim interpreter -- archived as
# the ordinary libc.a.  There is NO separate library: cc links it by default,
# with the ordinary crt0.o (rogue uses the plain crt0, not the fpsim-trap
# fcrt0; fpsim ends up in the archive but rogue pulls none of it).  This is
# the default here (NOFP=1); set NOFP=0 to build the FP `mklib' flavor.
# Replaces the earlier workaround (a side libnonfpc.a linked via -lnonfpc),
# which scattered the no-FPU decision across build/link commands.
# ---------------------------------------------------------------------
NOFP ?= 1
ifeq (${NOFP},1)
LIBC_ARCHIVE_OBJS = ${LIBNONFPC_OBJS}
else
LIBC_ARCHIVE_OBJS = ${LIBC_OBJS}
endif

libc: headers dirs
	# crt0.s defines `exit = 1.' itself and only does `sys exit', so it needs NO
	# sys.s -- prepending ${LSYS} dumped all 95 syscall-number absolutes into the
	# symbol table (1328-byte crt0.o).  Assembled alone it is byte-identical to the
	# native /lib/crt0.o (200 bytes, 5 symbols).
	${ASM} -o ${LIB}/crt0.o libc/csu/crt0.s
	# C sources (cc refuses `-o foo.o', so compile in-place via a subshell).
	# -O runs c2, as the authentic 2.9 libc `compall' does (`cc -c -O' on every
	# file); this is what makes the library byte-identical to 2.9's.
	cd libc/gen   && ${CURDIR}/${CC} -O -c $(addsuffix .c,$(filter-out errlst,${LIBC_GENC}))
	# errlst.c: 2.9BSD's sys_errlist[] closing brace sits inside #ifdef UCB_NET,
	# so it only compiles with networking on -- the shipped 2.9 build had it.
	# Build with -DUCB_NET (the extra network error strings are harmless data).
	cd libc/gen   && ${CURDIR}/${CC} -O -DUCB_NET -c errlst.c
	cd libc/stdio && ${CURDIR}/${CC} -O -c $(addsuffix .c,$(filter-out getpwent,${LIBC_STDIOC}))
	# getpwent.c -- the big passwd parser; built WITHOUT -O, as the target was
	# (authentic 2.8 c2 couldn't optimize this one file).
	cd libc/stdio && ${CURDIR}/${CC} -c getpwent.c
	# hand-written asm in gen/ and stdio/ (cuexit does `sys exit').  The FP
	# helpers keep 2.8's `11' suffix (frexp11/ldexp11/modf11 are the PDP-11
	# versions); fakcu/fakfp/ldfps are the FP-trap/cleanup stubs, ffltpr the
	# fake (non-FP) float-print companion to fltpr.
	${ASM} -o libc/gen/cuexit.o   ${LSYS} libc/gen/cuexit.s
	${ASM} -o libc/gen/setjmp.o            libc/gen/setjmp.s
	${ASM} -o libc/gen/abort.o             libc/gen/abort.s
	${ASM} -o libc/gen/modf11.o            libc/gen/modf11.s
	${ASM} -o libc/gen/ldexp11.o           libc/gen/ldexp11.s
	${ASM} -o libc/gen/frexp11.o           libc/gen/frexp11.s
	${ASM} -o libc/gen/fakcu.o             libc/gen/fakcu.s
	${ASM} -o libc/gen/fakfp.o             libc/gen/fakfp.s
	${ASM} -o libc/gen/ldfps.o             libc/gen/ldfps.s
	${ASM} -o libc/stdio/doprnt.o          libc/stdio/doprnt.s
	# fltpr: the authentic float-print hooks (pfloat/pscien/pgen) doprnt calls
	# for %f/%e/%g; it also defines `fltused' (replacing the old fltstub).
	${ASM} -o libc/stdio/fltpr.o           libc/stdio/fltpr.s
	${ASM} -o libc/stdio/ffltpr.o          libc/stdio/ffltpr.s
	# syscall stubs
	for f in ${LIBC_SYS}; do ${ASM} -o libc/sys/$$f.o ${LSYS} libc/sys/$$f.s; done
	# crt: csv + cerror + long arithmetic.  Both the FP (crt/) and the non-FP
	# (nonfpcrt/) flavors are assembled; LIBC_ARCHIVE_OBJS selects one per NOFP.
	${ASM} -o libc/crt/csv.o               libc/crt/csv.s
	${ASM} -o libc/crt/cerror.o   ${LSYS} libc/crt/cerror.s
	for f in ${LIBC_ARITH}; do ${ASM} -o libc/crt/$$f.o libc/crt/$$f.s; done
	# non-FP crt (rogue's older software ldiv/lrem) + the fpsim FP interpreter,
	# linked into libc.a when NOFP=1.
	for f in csv cerror ${LIBC_ARITH}; do ${ASM} -o libc/nonfpcrt/$$f.o ${LSYS} libc/nonfpcrt/$$f.s; done
	${ASM} -o fpsim/fpsim.o ${LSYS} fpsim/fp1.s fpsim/fp2.s fpsim/fp3.s fpsim/fpx.s
	# archive + table of contents (LIBC_ARCHIVE_OBJS = non-FP fmklib set when NOFP=1)
	rm -f ${LIB}/libc.a
	${BIN}/${PREFIX}-ar rc ${LIB}/libc.a ${LIBC_ARCHIVE_OBJS}
	${BIN}/${PREFIX}-ranlib ${LIB}/libc.a

# ---------------------------------------------------------------------
# (Retired) libnonfpc.a / -lnonfpc.  The no-FPU library is now the DEFAULT
# libc.a (NOFP=1, above); cc links it naturally with crt0.o, so the old
# -lnonfpc workaround is gone.  `make libnonfpc' is kept as an alias for
# `make libc' so older invocations still work.
# ---------------------------------------------------------------------
libnonfpc: libc

# ---------------------------------------------------------------------
# libc-era.a -- the SHIPPED-ERA C library (a second libc for testing).
#
# The default libc.a above is built from our source tree: the final 2.9
# sys.s (libc/include/sys.s, 95 defs, with the UCB_NET calls, installed
# Aug 1983).  But the binaries that shipped on the 2.9 tape were linked
# MONTHS earlier, against an earlier sys.s -- the /lib/libc.a on the image
# is a mix of four sys.s revisions dated Feb-Sep 1983, dominated (59/68
# syscall stubs) by the Feb-Mar "rev0" set: 79 defs, no networking, and the
# local syscalls still on their old numbers (lstat=12, renice=17, ucall=18,
# lfstat=11 before the qfstat rename).  That earlier revision is
# reconstructed verbatim in libc/include/sys-era.s and reproduces those
# stubs byte-for-byte.
#
# libc-era.a = libc.a with the syscall stubs reassembled against sys-era.s
# and the long-arithmetic helpers / csv / cerror / cuexit assembled with NO
# sys.s (the shipped libc never flooded those with the number defs -- our
# default NONFP recipe does).  Link a program with -lc-era instead of -lc
# (and crt0-era.o instead of crt0.o) to reproduce an era binary; link -lc to
# get the current-source library.  See NOTES / memory for the rev breakdown.
LSYS_ERA = libc/include/sys-era.s
ERAOBJ   = libc/era-obj
libc-era: libc
	rm -rf ${ERAOBJ} && mkdir -p ${ERAOBJ}
	# syscall stubs against the era (rev0) sys.s
	for f in ${LIBC_SYS}; do ${ASM} -o ${ERAOBJ}/$$f.o ${LSYS_ERA} libc/sys/$$f.s; done
	# cuexit + long arithmetic + csv + cerror WITHOUT the sys.s flood
	${ASM} -o ${ERAOBJ}/cuexit.o libc/gen/cuexit.s
	for f in csv cerror ${LIBC_ARITH}; do ${ASM} -o ${ERAOBJ}/$$f.o libc/nonfpcrt/$$f.s; done
	# era errlst.o -- the earlier errlst.c the shipped libc used (37 entries,
	# no ELOOP, "Quota exceeded"; recovered from the shipped errlst.o).
	cd libc/gen && ${CURDIR}/${CC} -O -c errlst-era.c
	cp libc/gen/errlst-era.o ${ERAOBJ}/errlst.o && rm -f libc/gen/errlst-era.o
	# start from a copy of libc.a and replace those members in place (ar r
	# keeps each existing member's position; ranlib rebuilds the toc)
	rm -f ${LIB}/libc-era.a && cp ${LIB}/libc.a ${LIB}/libc-era.a
	${BIN}/${PREFIX}-ar r ${LIB}/libc-era.a ${ERAOBJ}/*.o
	${BIN}/${PREFIX}-ranlib ${LIB}/libc-era.a
	# era crt0 is identical to crt0.o: crt0.s defines `exit = 1.' locally and needs
	# no sys.s (era or otherwise), so no ${LSYS_ERA} -- byte-identical to native.
	${ASM} -o ${LIB}/crt0-era.o libc/csu/crt0.s

# ---------------------------------------------------------------------
# curses + termlib -- the screen libraries rogue (and other programs) link,
# built from the authentic 2.9BSD lib/curses/ and ucb/termlib/ sources.
# 2.9 names the termcap library libtermcap.a and hard-links libtermlib.a to it
# (-ltermlib); the termcap *database* lives elsewhere and is unrelated.
# Compile options mirror the 2.9BSD Makefiles exactly:
#   curses:  CFLAGS = -O           (lib/curses/Makefile, CFL empty)
#   termlib: CFLAGS = -O -DCM_N -DCM_GT -DCM_B -DCM_D   (ucb/termlib/Makefile)
# The CM_* cursor-motion defines apply to all three termlib members.
# ---------------------------------------------------------------------
# CURSES_OBJS order = 2.9 lib/curses/Makefile OBJS order, verified identical
# (NOT alphabetical).  ld lays out pulled archive members in archive order, so
# the member order must match 2.9's or the linked library data/text layout
# diverges (a `*.o' glob expands alphabetically and breaks byte-matching).
CURSES_OBJS = box.o clear.o initscr.o endwin.o mvprintw.o mvscanw.o mvwin.o \
	newwin.o overlay.o overwrite.o printw.o scanw.o refresh.o \
	touchwin.o erase.o clrtobot.o clrtoeol.o cr_put.o cr_tty.o \
	longname.o delwin.o insertln.o deleteln.o scroll.o getstr.o \
	getch.o addstr.o addch.o move.o curses.o unctrl.o standout.o \
	tstp.o insch.o delch.o
# libcurses -- the 2.9BSD curses library.  Source is the **v1.7** curses
# (libcurses/, from 2.8BSD usr/src/libcurses; curses.h @(#)curses.h 1.7) --
# the version 2.9 actually SHIPPED (its usr/lib/libcurses.a) and that
# usr/70/rogue links.  2.9's own usr/src/lib/curses is the later v1.11, which
# was never rebuilt into the shipped library; the v1.7 build here replaces it.
# Compiled with our cross toolchain it reproduces the native 2.9 libcurses.a
# 35/35 members byte-for-byte (full objects, verified).
#
# MENLO_JCL (job control) reaches curses purely through the header tree --
# NO -D.  The real 2.9 curses.h -- the copy the shipped libcurses.a archives
# (@(#)curses.h 1.7, 4286 bytes) -- has `# include <whoami.h>', which pulls
# sys/localopts.h and with it MENLO_JCL.  That include exists in NO source
# tree; it was a 2.9 build-tree edit, recovered from the archived header.
# libcurses/curses.h here is that version.  initscr.c includes curses.ext
# (-> curses.h -> whoami.h) before <signal.h>, so it picks MENLO_JCL up on
# its own.  tstp.c includes <signal.h> first (every historical tstp.c does),
# too early for the header, so the same build-tree edit adds `# include
# <whoami.h>' at the top of libcurses/tstp.c -- recovered the same way (the
# shipped tstp.o has the SIGTSTP job-control code, unreachable without it).
# Plain `cc -O' now reproduces the shipped 2.9 libcurses.a 35/35 members
# byte-for-byte (full objects, verified) with no flags.
curses: headers dirs
	cd curses && ${CURDIR}/${CC} -O -c *.c
	# Assemble the archive in old-2BSD format (mkcursesa.py): our ${PREFIX}-ar
	# writes a modern little-endian archive, but the shipped 2.9 libcurses.a is
	# old format (0xff65, middle-endian headers, {name[8];off} __.SYMDEF, the
	# curses headers archived ahead of the objects).  With that + our objects,
	# the result is byte-for-byte identical to ~/bsd/2.9/usr/lib/libcurses.a.
	python3 curses/mkcursesa.py curses ${LIB}/libcurses.a

termlib: headers dirs
	# 2.9BSD ucb/termlib/Makefile: CFLAGS = -O -DCM_N -DCM_GT -DCM_B -DCM_D,
	# applied to all three members (termcap.c/tgoto.c/tputs.c).
	cd termlib && ${CURDIR}/${CC} -O -DCM_N -DCM_GT -DCM_B -DCM_D -c termcap.c tgoto.c tputs.c
	# old-2BSD archive format (mktermliba.py, as with curses); byte-for-byte
	# identical to ~/bsd/2.9/usr/lib/libtermlib.a (== libtermcap.a).
	python3 termlib/mktermliba.py termlib ${LIB}/libtermlib.a
	rm -f ${LIB}/libtermcap.a
	cp ${LIB}/libtermlib.a ${LIB}/libtermcap.a

clean:
	rm -f libc/*.o libc/*/*.o fpsim/*.o curses/*.o termlib/*.o
	rm -rf libc/era-obj

.PHONY: all headers dirs libc libnonfpc libc-era curses termlib clean
