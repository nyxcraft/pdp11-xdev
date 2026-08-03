/*
 * Cross-build shim for <sys/param.h>.
 *
 * The 2.9BSD userland tools (strip) want BSIZE, which the host's
 * <sys/param.h> does not define.  Pull in the host header first (so tools that
 * rely on host param.h contents keep working), then add the 2.9BSD BSIZE.
 */
#ifndef	_CROSS_SYS_PARAM_H_
#define	_CROSS_SYS_PARAM_H_

#include_next <sys/param.h>

#ifndef	BSIZE
#define	BSIZE	512		/* 2.9BSD secondary block size (bytes) */
#endif

#endif	/* _CROSS_SYS_PARAM_H_ */
