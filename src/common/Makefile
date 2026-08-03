# src/common -- shared single-source-of-truth files.
#
# The only build step here is regenerating universe.h from universes.tsv.
# The generated header is COMMITTED (tools include it without a generation
# step); `make check` fails if the committed copy is stale.

all: universe.h

universe.h: universes.tsv mkuniverse.py
	python3 mkuniverse.py universes.tsv universe.h

check:
	@python3 mkuniverse.py universes.tsv universe.h.tmp && \
	if diff -u universe.h universe.h.tmp; then \
		rm -f universe.h.tmp; echo "universe.h up to date"; \
	else \
		rm -f universe.h.tmp; \
		echo "universe.h STALE -- run: make -C src/common"; exit 1; \
	fi

clean:
	rm -f universe.h.tmp

.PHONY: all check clean
