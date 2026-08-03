/ late-hardware golden test: exit code 0 = all pass, else first failing test
.globl start
.text
start:
	mov	$1,r5
/ 1: mfpt
	mfpt
	cmp	r0,$5
	jne	fail
	inc	r5
/ 2: spl
	spl	7
	inc	r5
/ 3: tstset
	mov	$52,cell
	tstset	cell
	jcs	fail
	cmp	r0,$52
	jne	fail
	cmp	cell,$53
	jne	fail
	inc	r5
/ 4: wrtlck
	mov	$1234,r0
	wrtlck	cell
	cmp	cell,$1234
	jne	fail
	inc	r5
/ 5: fis 2.5+1.5=4.0
	mov	$fstk,r3
	fadd	r3
	cmp	r3,$fstk+4
	jne	fail
	cmp	fstk+4,$40600
	jne	fail
	tst	fstk+6
	jne	fail
	inc	r5
/ 6: movc "HELLO"(5)->8 fill '.'
	mov	$5,r0
	mov	$src1,r1
	mov	$10,r2
	mov	$dst1,r3
	mov	$56,r4
	movc
	tst	r0
	jne	fail
	cmpb	dst1+4,$'O
	jne	fail
	cmpb	dst1+5,$56
	jne	fail
	cmpb	dst1+7,$56
	jne	fail
	inc	r5
/ 7: locc 'L' in "HELLO"
	mov	$5,r0
	mov	$src1,r1
	mov	$114,r4
	locc
	cmp	r0,$3
	jne	fail
	cmp	r1,$src1+2
	jne	fail
	inc	r5
/ 8: cmpc "HELLO" vs "HELP"
	mov	$5,r0
	mov	$src1,r1
	mov	$4,r2
	mov	$src2,r3
	mov	$40,r4
	cmpc
	jcc	fail
	cmp	r0,$2
	jne	fail
	inc	r5
/ 9: matc "LL" in "HELLO"
	mov	$5,r0
	mov	$src1,r1
	mov	$2,r2
	mov	$pat,r3
	matc
	cmp	r0,$3
	jne	fail
	cmp	r1,$src1+2
	jne	fail
	inc	r5
/ 10: l2dr
	mov	$dtab,r0
	l2dr	r0
	cmp	r0,$5
	jne	fail
	cmp	r1,$src1
	jne	fail
	cmp	r2,$2
	jne	fail
	cmp	r3,$pat
	jne	fail
	inc	r5
/ 11: addn "123"+"877"="1000" (unsigned zoned, type 1 = 010000)
	mov	r5,r5sav
	mov	$3+10000,r0
	mov	$n123,r1
	mov	$3+10000,r2
	mov	$n877,r3
	mov	$4+10000,r4
	mov	$nres,r5
	addn
	mov	r5sav,r5
	cmpb	nres,$61
	jne	fail
	cmpb	nres+1,$60
	jne	fail
	cmpb	nres+2,$60
	jne	fail
	cmpb	nres+3,$60
	jne	fail
	inc	r5
/ 12: cvtlp 12345 -> packed(5, type 6 = 060000); cvtpl back
	mov	$5+60000,r0
	mov	$pres,r1
	clr	r2
	mov	$30071,r3		/ 12345. = 0o30071
	cvtlp
	mov	$5+60000,r0
	mov	$pres,r1
	cvtpl
	cmp	r3,$30071
	jne	fail
	tst	r2
	jne	fail
	inc	r5
/ 13: mulp packed 25*25=625
	mov	r5,r5sav
	mov	$2+60000,r0
	mov	$p25,r1
	mov	$2+60000,r2
	mov	$p25b,r3
	mov	$3+60000,r4
	mov	$pmres,r5
	mulp
	mov	r5sav,r5
	mov	$3+60000,r0
	mov	$pmres,r1
	cvtpl
	cmp	r3,$1161		/ 625. = 0o1161
	jne	fail
	inc	r5
/ 14: ashp: 625 >>1 with round digit 5 -> 63  (shift -1, rnd 5: 62.5 -> 63)
	mov	r5,r5sav
	mov	$3+60000,r0
	mov	$pmres,r1
	mov	$2+60000,r2
	mov	$pares,r3
	mov	$2400+377,r4		/ rnd 5 <<8 | count -1: 5*400=2400, -1=377
	bic	$100000,r4
	mov	$2777,r4		/ rnd=5 (bits 11:8), cnt=0377(-1)
	ashp
	mov	r5sav,r5
	mov	$2+60000,r0
	mov	$pares,r1
	cvtpl
	cmp	r3,$77			/ 63. = 0o77
	jne	fail
	inc	r5
/ 15: wait/reset no-ops; mark: call-with-args cleanup
	wait
	reset
	inc	r5
/ 16: mfps/mtps: set CCs via mtps, read back via mfps
	mtps	$16			/ N=1 Z=1 V=1 C=0
	jpl	fail
	mfps	r0
	cmp	r0,$16
	jne	fail
	inc	r5
/ 17: mfpi/mtpi push-pop
	mov	$4321,cell
	mfpi	cell
	mtpi	cell2
	cmp	cell2,$4321
	jne	fail
	inc	r5
/ 18: stst -> zero pair
	mov	$-1,stw
	mov	$-1,stw+2
	stst	stw
	tst	stw
	jne	fail
	tst	stw+2
	jne	fail
	inc	r5
/ 19: mark 0: SP = PC (word after mark), PC = R5, R5 = (SP)+
	mov	r5,r5sav
	mov	sp,spsav
	mov	$mcont,r5
	mark	0
m5v:	054321			/ mark parks SP here; this word -> R5
mcont:
	cmp	r5,$54321
	jne	fail3
	cmp	sp,$m5v+2
	jne	fail3
	mov	spsav,sp
	mov	r5sav,r5
	inc	r5
	br	pass
fail3:	mov	spsav,sp
	mov	r5sav,r5
	jbr	fail

pass:
	clr	r0
	sys	1
fail:
	mov	r5,r0
	sys	1

.data
cell:	0
fstk:	040300; 0		/ 1.5 (top of FIS stack)
	040440; 0		/ 2.5
	0; 0
src1:	.byte	'H,'E,'L,'L,'O
src2:	.byte	'H,'E,'L,'P
pat:	.byte	'L,'L
	.even
dst1:	.byte	0,0,0,0,0,0,0,0
n123:	.byte	'1,'2,'3
n877:	.byte	'8,'7,'7
nres:	.byte	0,0,0,0
p25:	.byte	002,0134
p25b:	.byte	002,0134
pres:	.byte	0,0,0
pmres:	.byte	0,0
pares:	.byte	0,0
	.even
dtab:	5; src1; 2; pat
r5sav:	0
spsav:	0
cell2:	0
stw:	0; 0
