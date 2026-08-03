/*	@(#)kill.s	2.2	SCCS id keyword	*/
/ C library -- kill

.globl	_kill, cerror

_kill:
	mov	r5,-(sp)
	mov	sp,r5
	mov	4(sp),r0
	mov	6(sp),8f
	sys	0; 9f
	bec	1f
	jmp	cerror
1:
	clr	r0
	mov	(sp)+,r5
	rts	pc

.data
9:
	sys	kill; 8:..
