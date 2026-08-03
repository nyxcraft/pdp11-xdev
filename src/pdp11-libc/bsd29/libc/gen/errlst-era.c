/* @(#)errlst-era.c -- ERA (early-1983) sys_errlist for libc-era.a */
/*
 * The default errlst.c is Berkeley 4.4 (82/04/01) built -DUCB_NET: it carries
 * ELOOP ("Too many levels of symbolic links"), the "Disk quota exceeded"
 * wording, and the full socket-error set (38-66).  The libc that SHIPPED on the
 * 2.9 tape (/lib/libc.a, extracted from the rootdump) was built from an EARLIER
 * errlst.c -- recovered verbatim from that errlst.o's data segment: only 37
 * entries (0..EWOULDBLOCK), NO symbolic-link error (symlinks did not exist yet
 * -- same rev0 fingerprint as sys-era.s), and EQUOT still worded "Quota
 * exceeded".  Compiling this file -O yields an errlst.o byte-identical to the
 * shipped member (text 0, data 728).  Used only by `make libc-era'; the default
 * libc.a keeps the newer errlst.c.  See NOTES / memory.
 */
char	*sys_errlist[] = {
	"Error 0",
	"Not owner",				/*  1 - EPERM */
	"No such file or directory",		/*  2 - ENOENT */
	"No such process",			/*  3 - ESRCH */
	"Interrupted system call",		/*  4 - EINTR */
	"I/O error",				/*  5 - EIO */
	"No such device or address",		/*  6 - ENXIO */
	"Arg list too long",			/*  7 - E2BIG */
	"Exec format error",			/*  8 - ENOEXEC */
	"Bad file number",			/*  9 - EBADF */
	"No children",				/* 10 - ECHILD */
	"No more processes",			/* 11 - EAGAIN */
	"Not enough core",			/* 12 - ENOMEM */
	"Permission denied",			/* 13 - EACCES */
	"Bad address",				/* 14 - EFAULT */
	"Block device required",		/* 15 - ENOTBLK */
	"Exclusive use facility busy",		/* 16 - EBUSY */
	"File exists",				/* 17 - EEXIST */
	"Cross-device link",			/* 18 - EXDEV */
	"No such device",			/* 19 - ENODEV */
	"Not a directory",			/* 20 - ENOTDIR */
	"Is a directory",			/* 21 - EISDIR */
	"Invalid argument",			/* 22 - EINVAL */
	"File table overflow",			/* 23 - ENFILE */
	"Too many open files",			/* 24 - EMFILE */
	"Inappropriate ioctl for device",	/* 25 - ENOTTY */
	"Text file busy",			/* 26 - ETXTBSY */
	"File too large",			/* 27 - EFBIG */
	"No space left on device",		/* 28 - ENOSPC */
	"Illegal seek",				/* 29 - ESPIPE */
	"Read-only file system",		/* 30 - EROFS */
	"Too many links",			/* 31 - EMLINK */
	"Broken pipe",				/* 32 - EPIPE */
	"Argument too large",			/* 33 - EDOM */
	"Result too large",			/* 34 - ERANGE */
	"Quota exceeded",			/* 35 - EQUOT */
	"Operation would block",		/* 36 - EWOULDBLOCK */
};
int	sys_nerr = { sizeof sys_errlist/sizeof sys_errlist[0] };
