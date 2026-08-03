#	@(#)makefile	2.3	SCCS id keyword
BIN=	/usr/ucb/bin
LIB=	/usr/ucb/lib
all:	as as2 ovas ovas2

cmp:	as as2
	cmp as /bin/as
	cmp as2 /lib/as2
	rm as as2 a.out

install:	as as2 ovas ovas2
	cp as ${BIN}/as
	cp as2 ${LIB}/as2
	cp ovas ${BIN}/ovas
	cp ovas2 ${LIB}/ovas2
	rm as as2 ovas ovas2 a.out

as:
	as /usr/include/sys.s as1?.s
	ld -n -s a.out -o as

ovas:
	as /usr/include/sys.s ovas11.s as1[2-9].s
	ld -n -s a.out -o ovas

as2:
	as /usr/include/sys.s as2?.s
	ld -n -s a.out -o as2

ovas2:
	as /usr/include/sys.s as2[1-6].s ovas27.s as2[89].s
	ld -n -s a.out -o ovas2

clean:
	rm -f a.out
