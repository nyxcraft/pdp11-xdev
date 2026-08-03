/ gate211.s -- universe-numbering gate.  sys 64 is getpagesize in the
/ 4.3-numbered eras (bsd210/bsd211) and nosys before them.  Run under
/ -u bsd211 it must succeed with 1024 (exit 0); run under the default
/ bsd29 universe the same binary must FAIL (nonzero exit) -- a wrong
/ success would mean era numbering leaked across personalities.
/ bsd211 passes syscall args on the stack: push code then a dummy word
/ (the return-address slot a libc stub would occupy).
.globl start
start:
	sys	64.
	bcs	1f
	cmp	r0,$1024.
	bne	1f
	clr	-(sp)		/ exit(0), stack convention
	tst	-(sp)
	sys	1
1:	mov	$1.,r0		/ inline-convention exit(1) (default universe)
	sys	1
