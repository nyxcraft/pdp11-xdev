/*	@(#)sync.s	2.2	SCCS id keyword	*/
.globl	_sync

_sync:
	mov	r5,-(sp)
	mov	sp,r5
	sys	sync
	mov	(sp)+,r5
	rts	pc
