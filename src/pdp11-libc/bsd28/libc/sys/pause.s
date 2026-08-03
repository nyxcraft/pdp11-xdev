/*	@(#)pause.s	2.2	SCCS id keyword	*/
/ C library - pause

.globl	_pause

_pause:
	mov	r5,-(sp)
	mov	sp,r5
	sys	pause
	mov	(sp)+,r5
	rts	pc
