# pdp11-ar -- archiver, authentic 2BSD format (host tool)
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -I$(COMMON)/cross
TARGET = $(BIN)/pdp11-ar
DEPS   = ar.c $(COMMON)/cross/a.out.h $(COMMON)/cross/ar.h

all: $(TARGET)
$(TARGET): $(DEPS)
	@mkdir -p $(BIN)
	$(HOSTCC) $(CFLAGS) -o $@ ar.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-ar: no tests yet"; fi
clean:
	rm -f $(TARGET)
.PHONY: all check clean
