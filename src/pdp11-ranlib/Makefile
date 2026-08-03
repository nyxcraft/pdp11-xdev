# pdp11-ranlib -- __.SYMDEF table-of-contents builder (host tool)
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -I$(COMMON)/cross
TARGET = $(BIN)/pdp11-ranlib
DEPS   = ranlib.c $(COMMON)/cross/a.out.h $(COMMON)/cross/ar.h

all: $(TARGET)
$(TARGET): $(DEPS)
	@mkdir -p $(BIN)
	$(HOSTCC) $(CFLAGS) -o $@ ranlib.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-ranlib: no tests yet"; fi
clean:
	rm -f $(TARGET)
.PHONY: all check clean
