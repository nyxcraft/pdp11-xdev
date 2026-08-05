/ fpimm.s -- FP11 addressing + FT(chop) regression probe.  Exit 0 = all
/ pass, else the first failing case number in the exit status.
/
/ Pins two real bugs found via the native Ultrix-11 factor(1):
/   * $literal operands of the INTEGER-side FP forms (ldfps, movif/LDCIF,
/     ldexp) were resolved through the word-operand path, which returned
/     the immediate TOKEN -- ld2() of it read M[0], so `ldfps $240' loaded
/     the first text word as the status register (flipping FL on), and
/     `movif $10.' converted garbage.  The store forms would have WRITTEN
/     M[0] the same way.
/   * The FT (truncate) mode bit was ignored: every result rounded.
/     factor's Newton sqrt loop is written for chopped FP11 arithmetic
/     and oscillates forever without it.
.globl	start
.text
start:
	mov	$1,r5
/ 1: ldfps $literal loads the LITERAL (not M[0]); read back via stfps
	ldfps	$240		/ FD + FT (double, chop)
	stfps	r0
	cmp	r0,$240
	jne	fail
	inc	r5
/ 2: movif $literal (LDCIF) converts the literal; back out via movfi
	movif	$12.,r0
	movfi	r0,r1
	cmp	r1,$12.
	jne	fail
	inc	r5
/ 3: FT set: (2^28+1)^2 needs 57 mantissa bits -- the low bit must CHOP.
/    result - 2^56 == 2^29 exactly.  (ldexp $n sets the value to
/    0.5 * 2^n; the $literal is itself the other half of the regression.)
	movif	$1,r1
	ldexp	$29.,r1		/ fr1 = 2^28
	movif	$1,r2
	addf	r2,r1		/ fr1 = 2^28+1  (exact)
	mulf	r1,r1		/ fr1 = 2^56+2^29+1 -> chopped to 2^56+2^29
	movif	$1,r3
	ldexp	$57.,r3		/ fr3 = 2^56
	subf	r3,r1		/ fr1 = 2^29 iff chopped
	movif	$1,r2
	ldexp	$30.,r2		/ fr2 = 2^29
	cmpf	r2,r1
	cfcc			/ FP CC -> CPU CC (the branch reads CPU flags)
	jne	fail
	inc	r5
/ 4: FT clear: the same square ROUNDS -- result - 2^56 == 2^29 + 2
	ldfps	$200		/ FD only
	movif	$1,r1
	ldexp	$29.,r1
	movif	$1,r2
	addf	r2,r1
	mulf	r1,r1		/ rounds: 2^56+2^29+2
	movif	$1,r3
	ldexp	$57.,r3
	subf	r3,r1		/ 2^29+2
	movif	$1,r2
	ldexp	$30.,r2		/ 2^29
	movif	$2,r0
	addf	r0,r2		/ 2^29+2
	cmpf	r2,r1
	cfcc			/ FP CC -> CPU CC (the branch reads CPU flags)
	jne	fail
	clr	r0
	sys	1
fail:	mov	r5,r0
	sys	1
