# pdp11-xstr -- shared-strings extractor, UCB 1978 (host tool)
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -I$(COMMON)/cross
TARGET = $(BIN)/pdp11-xstr
DEPS   = xstr.c $(COMMON)/cross/a.out.h $(COMMON)/cross/ar.h

all: $(TARGET)
$(TARGET): $(DEPS)
	@mkdir -p $(BIN)
	$(HOSTCC) $(CFLAGS) -o $@ xstr.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-xstr: no tests yet"; fi
clean:
	rm -f $(TARGET)
.PHONY: all check clean
