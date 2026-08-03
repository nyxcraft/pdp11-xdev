/*	@(#)stat.s	2.2	SCCS id keyword	*/
/ C library -- stat

/ error = stat(string, statbuf);

.globl	_stat
.globl	cerror

_stat:
	mov	r5,-(sp)
	mov	sp,r5
	mov	4(r5),0f
	mov	6(r5),0f+2
	sys	0; 9f
	bec	1f
	jmp	cerror
1:
	clr	r0
	mov	(sp)+,r5
	rts	pc
.data
9:
	sys	stat; 0:..; ..
