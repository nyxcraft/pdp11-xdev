# pdp11-strip -- symbol table and relocation stripper (host tool)
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -I$(COMMON)/cross
TARGET = $(BIN)/pdp11-strip
DEPS   = strip.c $(COMMON)/cross/a.out.h $(COMMON)/cross/ar.h

all: $(TARGET)
$(TARGET): $(DEPS)
	@mkdir -p $(BIN)
	$(HOSTCC) $(CFLAGS) -o $@ strip.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-strip: no tests yet"; fi
clean:
	rm -f $(TARGET)
.PHONY: all check clean
