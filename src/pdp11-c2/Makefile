# pdp11-c2 -- Ritchie cc peephole optimizer (host tool); cc -O runs it
include ../common/config.mk

CFLAGS = $(OPT) $(COMPAT) -I.
TARGET = $(BIN)/pdp11-c2
OBJS   = c20.o c21.o

all: $(TARGET)
$(TARGET): $(OBJS)
	@mkdir -p $(BIN)
	$(HOSTCC) $(OPT) -o $@ $(OBJS)
$(OBJS): c2.h
.c.o:
	$(HOSTCC) $(CFLAGS) -c -o $@ $<
check: $(TARGET)
	@if [ -x tests/run.sh ]; then cd tests && ./run.sh; else echo "pdp11-c2: no tests yet"; fi
clean:
	rm -f $(TARGET) $(OBJS)
.PHONY: all check clean
