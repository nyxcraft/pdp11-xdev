# pdp11-apsim -- user-mode PDP-11 simulator (host tool).  Runs 2BSD a.out
# (0407/0410/0411/0430) and First Edition (0405) binaries; bit-faithful
# FP11 D-format arithmetic; ~60-call 2.8BSD syscall layer with real host
# fork/wait/pipe.  -lm is required by the FIS (KEV11) path.
include ../common/config.mk

TARGET = $(BIN)/pdp11-apsim

all: $(TARGET)
$(TARGET): apsim.c $(COMMON)/universe.h
	@mkdir -p $(BIN)
	$(HOSTCC) $(OPT) -I$(COMMON) -o $@ apsim.c -lm
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-apsim: no tests yet"; fi
clean:
	rm -f $(TARGET)
.PHONY: all check clean
