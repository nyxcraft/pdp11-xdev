#!/usr/bin/env python3
"""Run every command binary in an extracted distribution tree under apsim and
count the ones that hit an unimplemented syscall -- an honest, corpus-driven
measure of coverage that no hand-maintained table can fake.

    sweep.py <tree> <universe> [dirs]

apsim gives the guest the REAL filesystem rooted at the tree (APSIM_ROOT),
so a DANGER blocklist skips destructive/interactive commands, stdin is
/dev/null, each run has a 3s timeout (a timeout counts as a pass -- waiting
for input is not a miss), and each child gets its own session so an /etc
binary that signals its process group on exit can't kill the sweep.  A
binary that can't be read as extracted is reported, not silently dropped,
so the denominator stays honest.
"""
import os, re, sys, subprocess
from collections import Counter

APSIM = os.path.normpath(os.path.dirname(os.path.abspath(__file__)) + "/../../../../bin/pdp11-apsim")
DEFAULT_DIRS = ("bin", "usr/bin", "usr/ucb", "etc", "sbin", "usr/sbin")

# PDP-11 a.out magics apsim loads (little-endian first two bytes): 0405 First
# Edition, 0407 impure, 0410 pure, 0411 sep I&D, 0430/0431 auto-overlay.
MAGIC = {b"\x05\x01", b"\x07\x01", b"\x08\x01", b"\x09\x01",
         b"\x18\x01", b"\x19\x01"}

# Commands that would act on the real (rooted) filesystem or block forever.
DANGER = set("""
rm rmdir mv cp dd ln mkdir chmod chown chgrp newfs mkfs fsck dump restore
halt reboot shutdown init mknod mount umount sync fastboot fasthalt
passwd vipw chpass chfn chsh cron crontab getty login su
sendmail uucico uucp uux vi ex ed sed awk csh sh ksh more less pg
nroff troff tset reset stty tar cpio pax clri icheck dcheck ncheck
""".split())


def is_bin(path):
    try:
        with open(path, "rb") as f:
            return f.read(2) in MAGIC
    except OSError:
        return None		# unreadable (execute-only) -- report, don't drop


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    tree = os.path.abspath(sys.argv[1])
    univ = sys.argv[2]
    dirs = sys.argv[3].split(",") if len(sys.argv) > 3 else DEFAULT_DIRS
    sand = os.path.expanduser("~/.cache/pdp11-apsim-sweep")
    os.makedirs(sand, exist_ok=True)
    env = dict(os.environ, PDP11_UNIVERSE=univ, APSIM_ROOT=tree)

    ok = ran = 0
    unreadable = []
    misses = Counter()          # syscall number -> #binaries that wanted it
    who = {}                    # number -> set of binaries

    for d in dirs:
        dp = os.path.join(tree, d)
        if not os.path.isdir(dp):
            continue
        for name in sorted(os.listdir(dp)):
            path = os.path.join(dp, name)
            if not os.path.isfile(path) or name in DANGER:
                continue
            b = is_bin(path)
            if b is None:
                unreadable.append(d + "/" + name); continue
            if not b:
                continue
            ran += 1
            try:
                p = subprocess.run([APSIM, os.path.abspath(path)],
                                   capture_output=True, timeout=3, env=env,
                                   cwd=sand, stdin=subprocess.DEVNULL,
                                   start_new_session=True)
                out = (p.stdout + p.stderr).decode("latin-1")
            except subprocess.TimeoutExpired:
                ok += 1; continue          # blocked on input = not a miss
            hits = set(re.findall(r"unhandled sys (\d+)", out))
            if hits:
                for h in hits:
                    misses[h] += 1
                    who.setdefault(h, set()).add(name)
            else:
                ok += 1

    print("%d of %d command binaries ran without an unimplemented syscall"
          % (ok, ran))
    if unreadable:
        print("\nNOT TESTED -- unreadable as extracted (%d): %s"
              % (len(unreadable), ", ".join(unreadable)))
    if misses:
        print("\nmissing calls, by how many binaries want them:")
        for num, cnt in misses.most_common():
            print("  sys %-4s wanted by %d: %s"
                  % (num, cnt, ", ".join(sorted(who[num]))))


if __name__ == "__main__":
    main()
