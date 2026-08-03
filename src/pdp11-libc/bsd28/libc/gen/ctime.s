.globl
.bss
_cbuf:
.=.+32
.data
_dmsize:
37
34
37
36
37
36
37
37
36
37
36
37
_daytab:
5
515
.even
72
457
.even
.globl	_ctime
.text
_ctime:
~~ctime:
jsr	r5,csv
~t=4
mov	4(r5),(sp)
jsr	pc,*$_localti
mov	r0,(sp)
jsr	pc,*$_asctime
jmp	cret
.globl	_localti
_localti:
~~localtim:
jsr	r5,csv
~tim=4
sub	$20,sp
~dayno=r4
~ct=r3
~daylbegi=r2
~daylend=177770
~copyt=177764
~systime=177752
mov	r5,(sp)
add	$177752,(sp)
jsr	pc,*$_ftime
mov	$74,-(sp)
sxt	-(sp)
mov	177760(r5),-(sp)
sxt	-(sp)
jsr	pc,lmul
add	$10,sp
mov	r1,-(sp)
mov	r0,-(sp)
mov	4(r5),r0
mov	+2(r0),r1
mov	(r0),r0
sub	(sp)+,r0
sub	(sp)+,r1
sbc	r0
mov	r0,177764(r5)
mov	r1,177766(r5)
mov	r5,(sp)
add	$177764,(sp)
jsr	pc,*$_gmtime
mov	r0,r3
mov	16(r3),r4
mov	$167,r2
mov	$457,177770(r5)
cmp	$112,12(r3)
jeq	L10000
cmp	$113,12(r3)
jne	L7
L10000:mov	12(r3),r0
ash	$2,r0
mov	-450+_daytab(r0),r2
mov	12(r3),r0
ash	$2,r0
mov	-446+_daytab(r0),177770(r5)
L7:mov	r2,(sp)
mov	r3,-(sp)
jsr	pc,*$_sunday
tst	(sp)+
mov	r0,r2
mov	177770(r5),(sp)
mov	r3,-(sp)
jsr	pc,*$_sunday
tst	(sp)+
mov	r0,177770(r5)
tst	177762(r5)
jeq	L8
cmp	r2,r4
jlt	L10001
cmp	r2,r4
jne	L8
cmp	$2,4(r3)
jgt	L8
L10001:cmp	177770(r5),r4
jgt	L10002
cmp	177770(r5),r4
jne	L8
cmp	$1,4(r3)
jle	L8
L10002:add	$7020,177766(r5)
adc	177764(r5)
mov	r5,(sp)
add	$177764,(sp)
jsr	pc,*$_gmtime
mov	r0,r3
inc	20(r3)
L8:mov	r3,r0
jmp	cret
.globl
_sunday:
~~sunday:
jsr	r5,csv
mov	4(r5),r4
~t=r4
mov	6(r5),r3
~d=r3
cmp	$72,r3
jgt	L12
mov	12(r4),(sp)
jsr	pc,*$_dysize
add	$-555,r0
add	r0,r3
L12:mov	r3,r1
sub	16(r4),r1
add	14(r4),r1
add	$1274,r1
sxt	r0
div	$7,r0
mov	r1,-(sp)
mov	r3,r0
sub	(sp)+,r0
jmp	cret
.globl	_gmtime
_gmtime:
~~gmtime:
jsr	r5,csv
~tim=4
sub	$10,sp
~d0=r4
~d1=r3
~hms=177766
~day=177762
~tp=r2
.bss
L16:.=.+22
.text
~xtime=L16
mov	$50600,-(sp)
mov	$1,-(sp)
mov	4(r5),r0
mov	+2(r0),r1
mov	(r0),r0
mov	r1,-(sp)
mov	r0,-(sp)
jsr	pc,lrem
add	$10,sp
mov	r0,177766(r5)
mov	r1,177770(r5)
mov	$50600,-(sp)
mov	$1,-(sp)
mov	4(r5),r0
mov	+2(r0),r1
mov	(r0),r0
mov	r1,-(sp)
mov	r0,-(sp)
jsr	pc,ldiv
add	$10,sp
mov	r0,177762(r5)
mov	r1,177764(r5)
tst	177766(r5)
jge	L17
add	$1,177766(r5)
add	$50600,177770(r5)
adc	177766(r5)
sub	$1,177764(r5)
sbc	177762(r5)
L17:mov	$L16,r2
mov	$74,-(sp)
sxt	-(sp)
mov	177770(r5),-(sp)
mov	177766(r5),-(sp)
jsr	pc,lrem
add	$10,sp
mov	r1,(r2)+
mov	$74,-(sp)
sxt	-(sp)
mov	177770(r5),-(sp)
mov	177766(r5),-(sp)
jsr	pc,ldiv
add	$10,sp
mov	r1,r3
mov	r3,r1
sxt	r0
div	$74,r0
mov	r1,(r2)+
mov	r3,r1
sxt	r0
div	$74,r0
mov	r0,r3
mov	r3,(r2)+
mov	$7,-(sp)
sxt	-(sp)
mov	177764(r5),r1
mov	177762(r5),r0
add	$160,r0
add	$4,r1
adc	r0
mov	r1,-(sp)
mov	r0,-(sp)
jsr	pc,lrem
add	$10,sp
mov	r1,14+L16
tst	177762(r5)
jlt	L18
mov	$106,r3
L19:mov	r3,(sp)
jsr	pc,*$_dysize
mov	r0,r1
sxt	r0
cmp	r0,177762(r5)
jgt	L22
jlt	L10003
cmp	r1,177764(r5)
jhi	L22
L10003:mov	r3,(sp)
jsr	pc,*$_dysize
mov	r0,r1
sxt	r0
sub	r0,177762(r5)
sub	r1,177764(r5)
sbc	177762(r5)
inc	r3
jbr	L19
L18:mov	$106,r3
jbr	L23
L20001:mov	r3,(sp)
dec	(sp)
jsr	pc,*$_dysize
mov	r0,r1
sxt	r0
add	r0,177762(r5)
add	r1,177764(r5)
adc	177762(r5)
dec	r3
L23:tst	177762(r5)
jlt	L20001
L22:mov	r3,12+L16
mov	177764(r5),r4
mov	r4,16+L16
mov	r3,(sp)
jsr	pc,*$_dysize
cmp	$556,r0
jne	L26
mov	$35,2+_dmsize
L26:clr	r3
jbr	L27
L20003:mov	r3,r0
asl	r0
sub	_dmsize(r0),r4
inc	r3
L27:mov	r3,r0
asl	r0
cmp	r4,_dmsize(r0)
jge	L20003
mov	$34,2+_dmsize
mov	r4,r0
inc	r0
mov	r0,(r2)+
mov	r3,(r2)+
clr	20+L16
mov	$L16,r0
jmp	cret
.globl	_asctime
_asctime:
~~asctime:
jsr	r5,csv
~t=4
~cp=r4
~ncp=r3
~tp=r2
mov	$_cbuf,r4
mov	$L35,r3
L33:movb	(r3)+,(r4)+
jne	L33
mov	4(r5),r1
mov	14(r1),r1
mul	$3,r1
mov	r1,r3
add	$L36,r3
mov	$_cbuf,r4
movb	(r3)+,(r4)+
movb	(r3)+,(r4)+
movb	(r3)+,(r4)+
inc	r4
mov	4(r5),r2
add	$10,r2
mov	(r2),r1
mul	$3,r1
mov	r1,r3
add	$L37,r3
movb	(r3)+,(r4)+
movb	(r3)+,(r4)+
movb	(r3)+,(r4)+
mov	-(r2),(sp)
mov	r4,-(sp)
jsr	pc,*$_ct_numb
tst	(sp)+
mov	r0,r4
mov	-(r2),(sp)
add	$144,(sp)
mov	r4,-(sp)
jsr	pc,*$_ct_numb
tst	(sp)+
mov	r0,r4
mov	-(r2),(sp)
add	$144,(sp)
mov	r4,-(sp)
jsr	pc,*$_ct_numb
tst	(sp)+
mov	r0,r4
mov	-(r2),(sp)
add	$144,(sp)
mov	r4,-(sp)
jsr	pc,*$_ct_numb
tst	(sp)+
mov	r0,r4
mov	4(r5),r0
cmp	$144,12(r0)
jgt	L38
movb	$62,1(r4)
movb	$60,2(r4)
L38:add	$2,r4
mov	4(r5),r0
mov	12(r0),r0
add	$144,r0
mov	r0,(sp)
mov	r4,-(sp)
jsr	pc,*$_ct_numb
tst	(sp)+
mov	r0,r4
mov	$_cbuf,r0
jmp	cret
.globl	_dysize
_dysize:
~~dysize:
jsr	r5,csv
~y=4
mov	4(r5),r1
sxt	r0
div	$4,r0
tst	r1
jne	L42
mov	$556,r0
L41:jmp	cret
L42:mov	$555,r0
jbr	L41
.globl
_ct_numb:
~~ct_numb:
jsr	r5,csv
mov	4(r5),r4
~cp=r4
~n=6
inc	r4
cmp	$12,6(r5)
jgt	L46
mov	6(r5),r1
sxt	r0
div	$12,r0
mov	r0,r1
sxt	r0
div	$12,r0
add	$60,r1
mov	r1,r0
movb	r0,(r4)+
jbr	L47
L46:movb	$40,(r4)+
L47:mov	6(r5),r1
sxt	r0
div	$12,r0
add	$60,r1
mov	r1,r0
movb	r0,(r4)+
mov	r4,r0
jmp	cret
.globl
.data
L35:.byte 104,141,171,40,115,157,156,40,60,60,40,60,60,72
.byte 60,60,72,60,60,40,61,71,60,60,12,0
L36:.byte 123,165,156,115,157,156,124,165,145,127,145,144,124,150
.byte 165,106,162,151,123,141,164,0
L37:.byte 112,141,156,106,145,142,115,141,162,101,160,162,115,141
.byte 171,112,165,156,112,165,154,101,165,147,123,145,160,117,143
.byte 164,116,157,166,104,145,143,0
