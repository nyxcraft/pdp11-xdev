#!/usr/bin/env python3
"""Generate universe.h from universes.tsv.

The TSV is the single source of truth for universe names (see the header
comment there).  Run via `make -C src/common`; `make -C src/common check`
regenerates and diffs against the committed header so a stale universe.h
cannot slip through.
"""
import sys

def main(src, dst):
    rows = []
    with open(src) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 4:
                sys.exit(f"mkuniverse: bad row (need 4 tab-separated fields): {line!r}")
            rows.append(parts)

    with open(dst, "w") as out:
        out.write("/* GENERATED from universes.tsv by mkuniverse.py -- do not edit. */\n")
        out.write("#ifndef PDP11_UNIVERSE_H\n#define PDP11_UNIVERSE_H\n\n")
        out.write("/* X(name, id, status, desc) for every universe.  status is one of\n")
        out.write(" * \"full\", \"sim\", \"planned\" -- see universes.tsv. */\n")
        out.write("#define PDP11_UNIVERSES(X) \\\n")
        for name, uid, status, desc in rows:
            out.write(f'\tX({name}, {uid}, "{status}", "{desc}") \\\n')
        out.write("\t/* end */\n\n")
        for name, uid, status, desc in rows:
            out.write(f"#define PDP11_UNIV_{name.upper()}\t{uid}\n")
        out.write("\n/* The universe assumed when PDP11_UNIVERSE is unset. */\n")
        out.write('#define PDP11_UNIV_DEFAULT_NAME\t"bsd29"\n')
        out.write("\n#endif /* PDP11_UNIVERSE_H */\n")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: mkuniverse.py universes.tsv universe.h")
    main(sys.argv[1], sys.argv[2])
