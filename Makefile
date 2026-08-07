# pdp11-xdev -- host-side PDP-11 cross-development toolchain.
#
#   make            build every host tool into ./bin
#   make libc       build the one universal target C library into ./lib
#                   (and install the era headers into ./include) -- needs
#                   the tools, so it implies `make all`
#   make check      every tool's regression suite + the end-to-end suite
#   make clean      remove build products
#
# Each tool lives in src/pdp11-<tool>/ with its own Makefile; this file only
# fans out.  Shared configuration is src/common/config.mk; the universe
# table is src/common/universes.tsv (see docs/design.md).

TOOLS = pdp11-as pdp11-ld pdp11-ar pdp11-ranlib pdp11-nm pdp11-size \
	pdp11-strip pdp11-das pdp11-objcopy pdp11-xstr pdp11-cpp pdp11-c0 \
	pdp11-c1 pdp11-c2 pdp11-cc pdp11-s5fs pdp11-apsim

all: $(TOOLS)

$(TOOLS):
	@$(MAKE) -C src/$@

# Target-side: the matched headers into include/ and one universal C
# library + crt0 into lib/ (flat -- the universe is selected at link via
# __univ).  Built with the cross tools themselves, so it depends on `all`.
libc: all
	@$(MAKE) -C src/pdp11-libc

headers:
	@$(MAKE) -C src/pdp11-libc headers

# ---- install -------------------------------------------------------------
# Self-contained tree under $(PREFIX)/share/pdp11-xdev/{bin,lib,include} so the
# target headers and libc never pollute the host's /usr/local/{include,lib}.
# The tools in $(PREFIX)/bin are RELATIVE symlinks into it; each resolves its
# sibling lib/ and include/ from its own real location via /proc/self/exe, so
# the tree relocates (DESTDIR staging, package moves) without breaking.  Only
# the prefixed pdp11-* names are installed -- cc derives its pass prefix from
# argv[0], so a bare `cc' would look for `c0' instead of `pdp11-c0'.
PREFIX  ?= /usr/local
DESTDIR ?=
pkgdir = $(DESTDIR)$(PREFIX)/share/pdp11-xdev
bindir = $(DESTDIR)$(PREFIX)/bin

install: libc
	mkdir -p $(pkgdir)/bin $(pkgdir)/lib $(pkgdir)/include $(bindir)
	cp bin/pdp11-* $(pkgdir)/bin/
	cp -R lib/. $(pkgdir)/lib/
	cp -R include/. $(pkgdir)/include/
	@for f in bin/pdp11-*; do b=`basename $$f`; \
		ln -sf ../share/pdp11-xdev/bin/$$b $(bindir)/$$b; \
		echo "  $(PREFIX)/bin/$$b -> ../share/pdp11-xdev/bin/$$b"; done

uninstall:
	rm -rf $(pkgdir)
	@for f in bin/pdp11-*; do b=`basename $$f`; rm -f $(bindir)/$$b; done

check: libc
	@$(MAKE) -C src/common check
	@for t in $(TOOLS); do $(MAKE) -C src/$$t check || exit 1; done
	@$(MAKE) -C src/pdp11-libc check
	@sh tests/run.sh
	@sh oracle/cross-universe.sh

# apsim under AddressSanitizer + UBSan: the suite + the loader fuzz corpus
# on the hostile-input paths, then a clean rebuild.  Separate from `check`
# (it rebuilds apsim twice); run it after touching the simulator.
check-san:
	@$(MAKE) -C src/pdp11-apsim check-san

# Fuzz the object parsers (ld/ar/nm/size/strip/objcopy) on a malformed a.out +
# archive corpus, built under ASan+UBSan.  Hermetic; restores plain builds.
fuzz:
	@sh tests/fuzz/run.sh

# Rebuild the static documentation site into gh-pages/public/ (committed) and
# fail on any broken internal link.  Needs markdown-it-py: pip install markdown-it-py
docs:
	@python3 gh-pages/build_site.py
	@python3 gh-pages/check_links.py

clean:
	@for t in $(TOOLS); do $(MAKE) -C src/$$t clean; done
	@$(MAKE) -C src/pdp11-libc clean
	@$(MAKE) -C src/common clean
	rm -rf bin include lib

.PHONY: all libc headers install uninstall check check-san fuzz docs clean $(TOOLS)
