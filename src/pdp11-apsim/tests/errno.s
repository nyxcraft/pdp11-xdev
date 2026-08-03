/ errno.s -- errno fidelity probe: opening a nonexistent file must set the
/ carry and leave the ERA errno (ENOENT = 2) in r0 -- not the blanket 1
/ (EPERM) the pre-universe apsim reported, and not a raw Linux number.
/ Exit 0 = ok; the failing check number is the exit status.
.globl start
start:
	sys	5; name; 0
	bcc	bad1		/ must fail
	cmp	r0,$2.		/ ENOENT
	bne	bad2
	clr	r0
	sys	1
bad1:	mov	$1.,r0
	sys	1
bad2:	mov	$2.,r0
	sys	1
name:	<no/such/file\0>
.even
