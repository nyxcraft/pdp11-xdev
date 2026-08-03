# pdp11-xdev -- host-side PDP-11 cross-development toolchain.
#
#   make            build every host tool into ./bin
#   make libc       build the per-universe target libraries into ./lib
#                   (and install the era headers into ./include) -- needs
#                   the tools, so it implies `make all`
#   make check      every tool's regression suite + the end-to-end suite
#   make clean      remove build products
#
# Each tool lives in src/pdp11-<tool>/ with its own Makefile; this file only
# fans out.  Shared configuration is src/common/config.mk; the universe
# table is src/common/universes.tsv (see docs/design.md).

TOOLS = pdp11-as pdp11-ld pdp11-ar pdp11-ranlib pdp11-nm pdp11-size \
	pdp11-strip pdp11-das pdp11-dcc pdp11-xstr pdp11-cpp pdp11-c0 \
	pdp11-c1 pdp11-c2 pdp11-cc pdp11-s5fs pdp11-apsim

all: $(TOOLS)

$(TOOLS):
	@$(MAKE) -C src/$@

# Target-side: per-universe headers into include/<universe>/ and the C
# library, crt0, curses, termlib into lib/<universe>/.  Built with the
# cross tools themselves, so it depends on `all`.
libc: all
	@$(MAKE) -C src/pdp11-libc

headers:
	@$(MAKE) -C src/pdp11-libc headers

check: libc
	@$(MAKE) -C src/common check
	@for t in $(TOOLS); do $(MAKE) -C src/$$t check || exit 1; done
	@$(MAKE) -C src/pdp11-libc check
	@sh tests/run.sh

clean:
	@for t in $(TOOLS); do $(MAKE) -C src/$$t clean; done
	@$(MAKE) -C src/pdp11-libc clean
	@$(MAKE) -C src/common clean
	rm -rf bin include lib

.PHONY: all libc headers check clean $(TOOLS)
