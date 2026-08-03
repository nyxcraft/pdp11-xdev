# pdp11-as -- PDP-11 assembler, C reimplementation of the 2BSD as (host tool).
# Historically parameterized: --isa / --sys / --aout / --std select the
# instruction set, syscall keyword table, and object format from First
# Edition (1972) through 2.11BSD.  See docs/std.md.
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -I.
TARGET = $(BIN)/pdp11-as

all: $(TARGET)
$(TARGET): as.c optab.h extoptab.h
	@mkdir -p $(BIN)
	$(HOSTCC) $(CFLAGS) -o $@ as.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-as: no tests yet"; fi
clean:
	rm -f $(TARGET)
.PHONY: all check clean
