/*	@(#)getpid.s	2.2	SCCS id keyword	*/
/ getpid -- get process ID

.globl	_getpid

_getpid:
	mov	r5,-(sp)
	mov	sp,r5
	sys	getpid
	mov	(sp)+,r5
	rts	pc
