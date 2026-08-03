/*	@(#)filbuf.c	2.2	SCCS id keyword	*/
#include	<stdio.h>

char	*malloc();


int
_filbuf(iop)
	register FILE *iop;
{
	static char smallbuf[_NFILE];

	if (iop->_flag & _IORW)
		iop->_flag |= _IOREAD;

	if ((iop->_flag & _IOREAD) == 0 || iop->_flag & _IOSTRG)
		return(EOF);

tryagain:
	if (iop->_base == NULL) {
		if (iop->_flag & _IONBF) {
			iop->_base = &smallbuf[fileno(iop)];
			goto tryagain;
		}
		if ((iop->_base = malloc(BUFSIZ)) == NULL) {
			iop->_flag |= _IONBF;
			goto tryagain;
		}
		iop->_flag |= _IOMYBUF;
	}
	iop->_ptr = iop->_base;
#ifdef UCB_STDIO
	if (ftype(iop) & (_IOTTY|_IOPIPE))
		flushout();
#endif
	iop->_cnt = read(fileno(iop), iop->_ptr, iop->_flag&_IONBF?1:BUFSIZ);
	if (--iop->_cnt < 0) {
		if (iop->_cnt == -1) {
			iop->_flag |= _IOEOF;
			if (iop->_flag & _IORW)
				iop->_flag &= ~_IOREAD;
		} else
			iop->_flag |= _IOERR;
		iop->_cnt = 0;
		return(EOF);
	}
	return(*iop->_ptr++ & 0377);
}

#ifdef UCB_STDIO
static flushout()
{
	register FILE *iop;
	extern FILE *_lastbuf;

	for(iop = _iob; iop < _lastbuf; iop++)
		if (ftype(iop)&(_IOTTY|_IOPIPE))
			fflush(iop);
}

static	ftype(iop)
FILE *iop;
{
	int fd = iop->_file;
	int type;
	if (iop->_flag&(_IOSTRG|_IOTTY|_IOPIPE|_IODISK))
		return(iop->_flag);
	if (isatty(fd))
		type = _IOTTY;
	else if (isapipe(fd))
		type = _IOPIPE;
	else
		type = _IODISK;
	return(iop->_flag |= type);
}
#endif
