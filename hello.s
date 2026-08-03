.globl	_main
.text
_main:
~~main:
jsr	r5,csv
jbr	L1
L2:mov	$L4,(sp)
jsr	pc,*$_printf
clr	r0
jbr	L3
L3:jmp	cret
L1:jbr	L2
.globl
.data
L4:.byte 150,145,154,154,157,12,0
