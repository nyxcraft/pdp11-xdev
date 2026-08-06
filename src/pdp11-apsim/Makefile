# pdp11-apsim -- user-mode PDP-11 simulator (host tool).  Runs 2BSD a.out
# (0407/0410/0411/0430) and First Edition (0405) binaries; bit-faithful
# FP11 D-format arithmetic; ~60-call 2.8BSD syscall layer with real host
# fork/wait/pipe.  No host FP is used anywhere -- FP11 and FIS both run on
# the exact softfloat -- so there is no libm dependency.
#
#   make            build $(BIN)/pdp11-apsim
#   make check      the regression suite (tests/run.sh)
#   make check-san  rebuild with ASan+UBSan, run the suite + the loader
#                   fuzz smoke under the sanitizers, then rebuild clean.
#                   Leak detection stays off (the guest address space and
#                   tables are never freed by design); the value is
#                   memory-corruption detection on the hostile-input
#                   paths -- the a.out loader parses vintage and
#                   dump-carved binaries.
#   make fuzz-smoke deterministic malformed-a.out sweep through the real
#                   loader (tests/fuzz-load.sh); needs nothing on disk.
include ../common/config.mk

TARGET = $(BIN)/pdp11-apsim

# SAN=1: AddressSanitizer + UBSan, non-recoverable so a probe fails loudly.
ifeq ($(SAN),1)
SANFLAGS = -g -fsanitize=address,undefined -fno-sanitize-recover=undefined
endif

all: $(TARGET)
$(TARGET): apsim.c $(COMMON)/universe.h
	@mkdir -p $(BIN)
	$(HOSTCC) $(OPT) $(SANFLAGS) -I$(COMMON) -o $@ apsim.c
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-apsim: no tests yet"; fi

fuzz-smoke: $(TARGET)
	@sh tests/fuzz-load.sh

check-san:
	$(MAKE) SAN=1 clean all
	$(MAKE) -C ../pdp11-as
	cd tests && ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
	            UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./run.sh
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(MAKE) fuzz-smoke
	$(MAKE) clean all

clean:
	rm -f $(TARGET)
.PHONY: all check check-san fuzz-smoke clean
