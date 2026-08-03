# pdp11-nm -- a.out/archive symbol table lister (host tool)
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -I$(COMMON)/cross
TARGET = $(BIN)/pdp11-nm
DEPS   = nm.c $(COMMON)/cross/a.out.h $(COMMON)/cross/ar.h

all: $(TARGET)
$(TARGET): $(DEPS)
	@mkdir -p $(BIN)
	$(HOSTCC) $(CFLAGS) -o $@ nm.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-nm: no tests yet"; fi
clean:
	rm -f $(TARGET)
.PHONY: all check clean
