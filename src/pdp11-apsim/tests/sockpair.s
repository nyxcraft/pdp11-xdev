/ socketpair(AF_UNIX, SOCK_STREAM, 0, sv); write "hi!" to sv[0]; read from
/ sv[1]; echo what we read to stdout.  bsd211 stack-arg convention: the
/ kernel reads args at 2(sp), so push args then a return-address filler.
.globl start
start:
	mov	$sv,-(sp)
	clr	-(sp)		/ protocol 0
	mov	$1,-(sp)	/ SOCK_STREAM
	mov	$1,-(sp)	/ AF_UNIX
	clr	-(sp)		/ ret-addr filler (args at 2(sp))
	sys	135.		/ socketpair
	add	$12,sp
	bcs	fail

	mov	$3,-(sp)	/ write(sv[0], msg, 3)
	mov	$msg,-(sp)
	mov	sv,-(sp)
	clr	-(sp)
	sys	4
	add	$10,sp

	mov	$8,-(sp)	/ read(sv[1], buf, 8)
	mov	$buf,-(sp)
	mov	sv+2,-(sp)
	clr	-(sp)
	sys	3
	add	$8,sp
	bcs	fail
	mov	r0,r4

	mov	r4,-(sp)	/ write(1, buf, count)
	mov	$buf,-(sp)
	mov	$1,-(sp)
	clr	-(sp)
	sys	4
	add	$10,sp

	clr	-(sp)
	clr	-(sp)
	sys	1
fail:
	mov	$1,-(sp)
	clr	-(sp)
	sys	1
msg:	<hi!>
	.even
sv:	0; 0
buf:	.=.+8
