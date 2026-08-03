# pdp11-c0 -- Ritchie cc pass 1: parser / front end (host tool)
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -fms-extensions -I$(COMMON)/cross -I.
TARGET = $(BIN)/pdp11-c0
OBJS   = c00.o c01.o c02.o c03.o c04.o c05.o

all: $(TARGET)
$(TARGET): $(OBJS)
	@mkdir -p $(BIN)
	$(HOSTCC) $(OPT) -o $@ $(OBJS)
$(OBJS): c0.h
.c.o:
	$(HOSTCC) $(CFLAGS) -c -o $@ $<
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-c0: no tests yet"; fi
clean:
	rm -f $(TARGET) $(OBJS)
.PHONY: all check clean
