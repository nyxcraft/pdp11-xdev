# pdp11-objcopy -- copy / extract a PDP-11 a.out (host tool)
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -I$(COMMON)/cross
TARGET = $(BIN)/pdp11-objcopy
DEPS   = objcopy.c $(COMMON)/cross/a.out.h

all: $(TARGET)
$(TARGET): $(DEPS)
	@mkdir -p $(BIN)
	$(HOSTCC) $(CFLAGS) -o $@ objcopy.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-objcopy: no tests yet"; fi
clean:
	rm -f $(TARGET)
.PHONY: all check clean
