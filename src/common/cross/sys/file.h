/*
 * Cross-build subset of 2.9BSD <sys/file.h> for host tools.
 *
 * Only the open/lseek/access flag macros the 2.9BSD userland tools use are
 * provided; the kernel file-table structs (struct file, ...) are omitted --
 * they don't compile on an LP64 host and no host tool needs them.
 *
 * The 2.9BSD flag *values* coincide with the host's (O_RDONLY==0,
 * FSEEK_ABSOLUTE==SEEK_SET==0, etc.), so open()/lseek() get the right host
 * semantics when the tools pass these names through.
 */
#ifndef _CROSS_SYS_FILE_H_
#define _CROSS_SYS_FILE_H_

/* flags supplied to access call */
#define FACCESS_EXISTS 0x0
#define FACCESS_EXECUTE 0x1
#define FACCESS_WRITE 0x2
#define FACCESS_READ 0x4

/* flags supplied to lseek call */
#define FSEEK_ABSOLUTE 0x0 /* absolute offset (== SEEK_SET) */
#define FSEEK_RELATIVE 0x1 /* relative to current (== SEEK_CUR) */
#define FSEEK_EOF 0x2	   /* relative to eof (== SEEK_END) */
/* The host's <fcntl.h> also defines these lseek-whence names under
 * _DEFAULT_SOURCE; guard so a tool that includes both does not redefine
 * them (the values agree: L_SET==SEEK_SET==0, ...). */
#ifndef L_SET
#define L_SET FSEEK_ABSOLUTE
#endif
#ifndef L_INCR
#define L_INCR FSEEK_RELATIVE
#endif
#ifndef L_XTND
#define L_XTND FSEEK_EOF
#endif

/* flags supplied to open call */
#define FATT_RDONLY 0x0 /* open for reading only (== O_RDONLY) */
#define FATT_WRONLY 0x1 /* open for writing only (== O_WRONLY) */
#define FATT_RDWR 0x2	/* open for reading and writing (== O_RDWR) */

#endif /* _CROSS_SYS_FILE_H_ */
