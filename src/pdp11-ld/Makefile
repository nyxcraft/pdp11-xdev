# pdp11-ld -- link editor, authentic 2.9BSD ld.c ported to LP64 (host tool).
# Overlay support (-Z/-L, magic 0430) is built in; the roundov() LP64 fix
# makes overlay links work.  Resolves -lx via lib/<universe>/ next to bin/.
include ../common/config.mk

UCBSRC = $(COMMON)/ucbpath/concat.c $(COMMON)/ucbpath/openl.c \
         $(COMMON)/ucbpath/openlp.c $(COMMON)/ucbpath/ucbpath.c
CFLAGS = $(OPT) $(COMPAT) -I$(COMMON)/cross -I$(COMMON)
TARGET = $(BIN)/pdp11-ld

all: $(TARGET)
$(TARGET): ld.c $(COMMON)/cross/a.out.h $(COMMON)/cross/ar.h $(COMMON)/universe.h
	@mkdir -p $(BIN)
	$(HOSTCC) $(CFLAGS) -o $@ ld.c $(UCBSRC)
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-ld: no tests yet"; fi
clean:
	rm -f $(TARGET)
.PHONY: all check clean
