# pdp11-c1 -- Ritchie cc pass 2: PDP-11 code generator (host tool).
# The codegen table is authored in table.s (a template language); cvopt
# expands it and mktab converts the result to C so it can be compiled for
# the host instead of assembled for the PDP-11.
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -fms-extensions -I$(COMMON)/cross -I.
TARGET = $(BIN)/pdp11-c1
OBJS   = c10.o c11.o c12.o c13.o table.o softfp.o

all: $(TARGET)
$(TARGET): $(OBJS)
	@mkdir -p $(BIN)
	$(HOSTCC) $(OPT) -o $@ $(OBJS)
c10.o c11.o c12.o c13.o table.o: c1.h
.c.o:
	$(HOSTCC) $(CFLAGS) -c -o $@ $<
table.c: table.i mktab
	./mktab table.i table.c
table.i: table.s cvopt
	./cvopt table.s table.i
cvopt: cvopt.c
	$(HOSTCC) $(OPT) $(COMPAT) -o $@ cvopt.c
mktab: mktab.c
	$(HOSTCC) $(OPT) -o $@ mktab.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-c1: no tests yet"; fi
clean:
	rm -f $(TARGET) $(OBJS) table.i table.c cvopt mktab
.PHONY: all check clean
