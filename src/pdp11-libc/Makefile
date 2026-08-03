# pdp11-libc -- per-universe target C libraries (target code, not a host tool).
#
# Each "full" universe has its own authentic source tree and build recipe
# under <universe>/: the era's libc, crt0, system headers, curses, and
# termlib, compiled and assembled with the cross tools in ../../bin and
# installed to ../../lib/<universe> and ../../include/<universe>.

UNIVERSES = bsd28 bsd29

all: $(UNIVERSES)

$(UNIVERSES):
	@$(MAKE) -C $@

headers:
	@for u in $(UNIVERSES); do $(MAKE) -C $$u headers || exit 1; done

check:
	@echo "pdp11-libc: exercised by the cc end-to-end suite (tests/)"

clean:
	@for u in $(UNIVERSES); do $(MAKE) -C $$u clean; done

.PHONY: all headers check clean $(UNIVERSES)
