#!/usr/bin/env python3
"""Which of each era's own syscalls does apsim leave silent?

apsim's dispatch is a canonical V7-numbered switch in apsim.c, reached by
the renumbering eras (2.10/2.11/sys3) through remap tables that rewrite the
guest number to a canonical one or a C_* extension id.  A guest number is
"answered" if it either hits a canonical `case` directly (pass-through) or
is rewritten by its era's remap table to something that has a case.  This
script joins that against numbers.tsv and reports the era numbers that
would fall through to the dispatcher's `default:` (ENOSYS).

"Answered" and "implemented" are not the same thing: an era number remapped
to C_NOSYS is answered by a *deliberate* ENOSYS, and C_OK by a benign no-op.
Those are reported separately from the true gaps -- a table that conflated
them would hide the distinction this file exists to keep.

Caveat: the indirect call (`sys 0`) is decoded before the switch, and First
Edition binaries dispatch through do_v1syscall(); case-counting reports
those missing when they are not.  Re-check a suspect number with a probe.
"""
import os, re, csv

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(HERE + "/../../apsim.c")
TSV = HERE + "/numbers.tsv"

src = open(SRC).read()

# The canonical switch lives in do_syscall(); slice from there so the
# signal-number map and the two-tab stack-arg classifier don't pollute
# the case set.  Canonical labels are one-tab `case N:` (numbers) and
# `case C_XXX:` (extension ids).
body = src[src.index("static void do_syscall("):]
# match `case N:` / `case C_XXX:` anywhere (several labels share a line,
# e.g. `case 18: case 28: case C_LSTAT:`); the body starts at do_syscall so
# the signal-number map above it is excluded.
num_cases = {int(m.group(1)) for m in re.finditer(r"\bcase (\d+):", body)}
ext_cases = set(re.findall(r"\bcase (C_[A-Z0-9_]+):", body))


def remap(name):
    """Parse a `static const struct sremap NAME[] = { ... };` table into
    {guest: canon_token}.  Entries come in three forms -- the literal
    `{g,c,f}` and the `R_(g,c)` / `RS_(g,c)` convenience macros -- so match
    all three.  canon_token is a number-string or a C_* name."""
    m = re.search(r"struct sremap " + name + r"\[\] = \{(.*?)\n\};", src, re.S)
    out = {}
    if not m:
        return out
    body = m.group(1)
    for g, c in re.findall(r"\{\s*(\w+)\s*,\s*(\w+)\s*,", body):
        if not (g == "0" and c == "0"):
            out[int(g)] = c
    for g, c in re.findall(r"\bRS?_\(\s*(\w+)\s*,\s*(\w+)\s*\)", body):
        out[int(g)] = c
    return out


# syscall-table slots that are not real calls -- unused/reserved holes and
# renamed-away entries.  Never counted as gaps (the guest never issues them).
NONCALL = {"x", "unused", "nosys", "reserved", "used", "indir", "old",
           "csw", "switch"}


REMAPS = {"bsd210": remap("Bsd210Remap"),
          "bsd211": remap("Bsd211Remap"),
          "sys3":   remap("Sys3Remap")}


def answered(canon):
    """Is a canonical target (int or C_* token) handled by a real case?"""
    if isinstance(canon, int):
        return canon in num_cases
    if canon.isdigit():
        return int(canon) in num_cases
    return canon in ext_cases


# numbers.tsv, grouped by universe
by_univ = {}
with open(TSV) as fh:
    for r in csv.DictReader(fh, delimiter="\t"):
        by_univ.setdefault(r["universe"], {})[int(r["number"])] = r["name"]

print("apsim canonical dispatch: %d numeric cases, %d C_* cases\n"
      % (len(num_cases), len(ext_cases)))

for u in ("v5", "v6", "v7", "bsd28", "bsd29", "bsd210", "bsd211", "sys3"):
    nums = by_univ.get(u)
    if not nums:
        continue
    rmap = REMAPS.get(u, {})
    missing, refused, noop = [], [], []
    for n, name in sorted(nums.items()):
        if name in NONCALL:
            continue
        if n in rmap:
            canon = rmap[n]
            if canon == "C_NOSYS":
                refused.append((n, name))
            elif canon == "C_OK":
                noop.append((n, name))
            elif not answered(canon):
                missing.append((n, name, canon))
        else:
            # pass-through: needs a canonical numeric case
            if n not in num_cases:
                missing.append((n, name, n))
    tag = "%-7s %3d numbers:" % (u, len(nums))
    if not missing and not refused:
        print("%s all answered (%d no-op stubs)" % (tag, len(noop)))
    else:
        print("%s %d unanswered, %d refused (ENOSYS), %d no-op"
              % (tag, len(missing), len(refused), len(noop)))
        for n, name, canon in missing:
            print("    UNANSWERED %3d %-12s -> default: (canon %s)" % (n, name, canon))
        for n, name in refused:
            print("    refused    %3d %-12s -> C_NOSYS (deliberate ENOSYS)" % (n, name))
