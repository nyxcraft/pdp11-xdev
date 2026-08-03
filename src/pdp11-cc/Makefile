# pdp11-cc -- compiler driver: cpp -> c0 -> c1 [-> c2] -> as -> ld (host tool).
# Resolves its sibling passes relative to its own location; --universe/-u (or
# $PDP11_UNIVERSE) selects the target era's headers, crt0, and libraries.
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -I$(COMMON)/cross -I$(COMMON)
TARGET = $(BIN)/pdp11-cc

all: $(TARGET)
$(TARGET): cc.c $(COMMON)/universe.h
	@mkdir -p $(BIN)
	$(HOSTCC) $(CFLAGS) -o $@ cc.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-cc: no tests yet"; fi
clean:
	rm -f $(TARGET)
.PHONY: all check clean
