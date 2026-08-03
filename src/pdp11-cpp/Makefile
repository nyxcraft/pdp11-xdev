# pdp11-cpp -- the 2.9BSD C preprocessor (host tool).  Built with -DFLEXNAMES
# (128-char macro names, matching the native 2.9 cpp).  Default include
# directory resolves to ../include/<universe> relative to the binary.
include ../common/config.mk

UCBSRC = $(COMMON)/ucbpath/concat.c $(COMMON)/ucbpath/openl.c \
         $(COMMON)/ucbpath/openlp.c $(COMMON)/ucbpath/ucbpath.c
CFLAGS = $(OPT) $(COMPAT) -I$(COMMON) -Dunix=1 -Dpdp11=1 -DFLEXNAMES
TARGET = $(BIN)/pdp11-cpp

all: $(TARGET)
$(TARGET): cpp.o cpy.o
	@mkdir -p $(BIN)
	$(HOSTCC) $(OPT) -o $@ cpp.o cpy.o $(UCBSRC)
cpp.o: cpp.c $(COMMON)/universe.h
	$(HOSTCC) $(CFLAGS) -c -o $@ cpp.c
cpy.o: cpy.c yylex.c
	$(HOSTCC) $(CFLAGS) -c -o $@ cpy.c
cpy.c: cpy.y
	$(YACC) cpy.y && mv y.tab.c cpy.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-cpp: no tests yet"; fi
clean:
	rm -f $(TARGET) cpp.o cpy.o cpy.c
.PHONY: all check clean
