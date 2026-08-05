/*	@(#)cuexit.s	2.1	SCCS id keyword	*/
/ C library -- exit

/ exit(code): flush stdio via _cleanup, then hand off to _exit(code).  Routing
/ through the dual-convention __exit (rather than a hardcoded V7 `sys exit' with
/ the code in r0) makes the status use the era's convention -- r0 for V7..2.9,
/ but 2(sp) for the 2.10/2.11 stackargs kernels, where a bare `sys exit' would
/ take the code from the stack and ignore r0.

.globl	_exit
.globl	__cleanup
.globl	__exit

_exit:
	mov	r5,-(sp)
	mov	sp,r5
	jsr	pc,__cleanup
	mov	4(r5),-(sp)
	jsr	pc,__exit
	/*NOTREACHED*/

