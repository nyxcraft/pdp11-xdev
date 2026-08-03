/ ptrace channel probe (bsd211 stack-arg convention).  The parent forks a
/ child that declares PT_TRACE_ME and then hits a BPT (breakpoint).  The
/ parent wait()s for the trace-stop, reads a known word (0x1234) out of the
/ child's D-space via PT_READ_D, single-steps once, then continues the child
/ to exit.  Success = the read-back word matches -> exit 0.
.globl start
start:
	sys	2.		/ fork
		br child	/ child returns here (r0=ppid)
	/ parent: r0 = child pid
	mov	r0,r5		/ save child pid

	/ wait4(-1, &st, 0, 0) -- expect the child trace-stopped (WIFSTOPPED)
	clr	-(sp)		/ rusage
	clr	-(sp)		/ options
	mov	$st,-(sp)	/ &status
	mov	$177777,-(sp)	/ pid = -1
	clr	-(sp)		/ filler
	sys	7.
	add	$12,sp

	/ ptrace(PT_READ_D=2, childpid, &magic_in_child, 0)
	clr	-(sp)		/ data
	mov	$magic,-(sp)	/ addr (same layout in both -- fork copied D-space)
	mov	r5,-(sp)	/ pid
	mov	$2,-(sp)	/ PT_READ_D
	clr	-(sp)		/ ret filler
	sys	26.
	add	$12,sp
	cmp	r0,$011064	/ 0x1234 = 011064 octal
	bne	fail

	/ ptrace(PT_STEP=9, childpid, 1, 0) -- single-step
	clr	-(sp)
	mov	$1,-(sp)	/ addr==1 -> keep PC
	mov	r5,-(sp)
	mov	$9,-(sp)	/ PT_STEP
	clr	-(sp)
	sys	26.
	add	$12,sp

	/ wait4 -- expect another trace-stop (post-step SIGTRAP)
	clr	-(sp)
	clr	-(sp)
	mov	$st,-(sp)
	mov	$177777,-(sp)
	clr	-(sp)
	sys	7.
	add	$12,sp

	/ ptrace(PT_CONTINUE=7, childpid, 1, 0)
	clr	-(sp)
	mov	$1,-(sp)
	mov	r5,-(sp)
	mov	$7,-(sp)	/ PT_CONTINUE
	clr	-(sp)
	sys	26.
	add	$12,sp

	/ wait4 -- expect the child to exit
	clr	-(sp)
	clr	-(sp)
	mov	$st,-(sp)
	mov	$177777,-(sp)
	clr	-(sp)
	sys	7.
	add	$12,sp

	clr	-(sp)		/ parent exit 0
	clr	-(sp)
	sys	1.
fail:
	mov	$1,-(sp)
	clr	-(sp)
	sys	1.

child:
	/ PT_TRACE_ME (ptrace(0,0,0,0))
	clr	-(sp)
	clr	-(sp)
	clr	-(sp)
	clr	-(sp)		/ req = PT_TRACE_ME = 0
	clr	-(sp)		/ ret filler
	sys	26.
	add	$12,sp
	bpt			/ breakpoint -> trace-stop; parent inspects here
	nop			/ the single-step lands here
	nop
	clr	-(sp)		/ child exit 0
	clr	-(sp)
	sys	1.
magic:	011064			/ 0x1234, the word the parent reads back
st:	0			/ wait status
