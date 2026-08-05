/ timetext.s -- `sys time' must return the seconds in r0:r1 ONLY.  No
/ UNIX kernel ever wrote memory for it (V6/V7/2BSD/Ultrix gtime() all
/ just set the register pair; the C library stores through tloc itself).
/ An earlier apsim treated the word after a DIRECT `sys time' as an
/ optional tloc pointer -- but that word is the next INSTRUCTION, and the
/ "store" shredded 4 bytes of text with the time.  Native Ultrix-11 2.0
/ binaries (whose libc uses the direct form) died of this: ls went dark,
/ factor printed garbage.  Exit 0 = ok, else the failing case number.
.globl	start
.text
start:
	mov	$1,r5
	sys	13.		/ direct time -- the two words after this
	mov	$52525,r4	/ trap are the ones the old apsim shredded
	cmp	r4,$52525
	jne	fail
	inc	r5
	cmp	r1,$2.		/ deterministic clock: seconds = 0:2
	jne	fail
	inc	r5
	tst	r0		/ high word 0
	jne	fail
	clr	r0
	sys	1
fail:	mov	r5,r0
	sys	1
