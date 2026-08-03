/*	@(#)quota.s	2.1	SCCS id keyword	*/
/ C library -- quota

/ error = quota(flag, current, max);

.globl	_quota, cerror

_quota:
	mov	r5,-(sp)
	mov	sp,r5
	mov	4(r5),0f
	mov	6(r5),0f+2
	mov	10(r5),0f+4
	mov	12(r5),0f+6
	mov	14(r5),0f+10
	sys	local; 9f
	bec	1f
	jmp	cerror
1:
	clr	r0
	mov	(sp)+,r5
	rts	pc
.data
9:
	sys	quota; 0:..; ..; ..; ..; ..
