#!/usr/bin/env python3
"""Generate universe.h from universes.tsv.

The TSV is the single source of truth for universe names (see the header
comment there).  Run via `make -C src/common`; `make -C src/common check`
regenerates and diffs against the committed header so a stale universe.h
cannot slip through.

Emitted pieces:
  PDP11_UNIVERSES(X)        X(name, id, status, desc)      -- cc/cpp/ld
  enum pdp11_kern           era-ascending kernel personalities -- apsim
  PDP11_UNIVERSE_TABLE(X)   X("name", id, "status", PDP11_K_*, "desc")
  PDP11_UNIVERSE_ALIASES(X) X("alias", "canonical")
"""
import sys

# Kernel personalities in ERA ORDER -- consumers compare with <=/>= (e.g.
# "BSD errno numbering from bsd210 on"), so the order is part of the contract.
KERNS = ["v1", "v56", "v7", "sys3", "ultrix", "bsd2x", "bsd210", "bsd211"]

def main(src, dst):
    rows = []
    with open(src) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 6:
                sys.exit(f"mkuniverse: bad row (need 6 tab-separated fields): {line!r}")
            name, uid, status, kern, aliases, desc = parts
            if kern not in KERNS:
                sys.exit(f"mkuniverse: unknown kern {kern!r} in row {name!r}")
            rows.append((name, uid, status, kern, aliases, desc))

    ids = [r[1] for r in rows]
    if len(set(ids)) != len(ids):
        sys.exit("mkuniverse: duplicate universe ids")

    with open(dst, "w") as out:
        out.write("/* GENERATED from universes.tsv by mkuniverse.py -- do not edit. */\n")
        out.write("#ifndef PDP11_UNIVERSE_H\n#define PDP11_UNIVERSE_H\n\n")

        out.write("/* X(name, id, status, desc) for every universe.  status is one of\n")
        out.write(" * \"full\", \"sim\", \"planned\" -- see universes.tsv. */\n")
        out.write("#define PDP11_UNIVERSES(X) \\\n")
        for name, uid, status, kern, aliases, desc in rows:
            out.write(f'\tX({name}, {uid}, "{status}", "{desc}") \\\n')
        out.write("\t/* end */\n\n")

        for name, uid, status, kern, aliases, desc in rows:
            out.write(f"#define PDP11_UNIV_{name.upper()}\t{uid}\n")

        out.write("\n/* apsim kernel personalities, in ERA ORDER (comparisons like\n")
        out.write(" * `Kern >= PDP11_K_BSD210' select everything from that era on). */\n")
        out.write("enum pdp11_kern {\n")
        for k in KERNS:
            out.write(f"\tPDP11_K_{k.upper()},\n")
        out.write("};\n\n")

        out.write("/* X(name, id, status, kern, desc) -- the full table for apsim. */\n")
        out.write("#define PDP11_UNIVERSE_TABLE(X) \\\n")
        for name, uid, status, kern, aliases, desc in rows:
            out.write(f'\tX("{name}", {uid}, "{status}", PDP11_K_{kern.upper()}, "{desc}") \\\n')
        out.write("\t/* end */\n\n")

        out.write("/* X(alias, canonical) -- accepted alternate spellings. */\n")
        out.write("#define PDP11_UNIVERSE_ALIASES(X) \\\n")
        for name, uid, status, kern, aliases, desc in rows:
            if aliases != "-":
                for a in aliases.split(","):
                    out.write(f'\tX("{a}", "{name}") \\\n')
        out.write("\t/* end */\n\n")

        out.write("/* The universe assumed when PDP11_UNIVERSE is unset. */\n")
        out.write('#define PDP11_UNIV_DEFAULT_NAME\t"bsd29"\n')
        out.write("\n#endif /* PDP11_UNIVERSE_H */\n")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: mkuniverse.py universes.tsv universe.h")
    main(sys.argv[1], sys.argv[2])
