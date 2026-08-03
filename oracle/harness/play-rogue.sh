#!/bin/sh
# Play the original 2.8BSD rogue (rogue3.4.obj, 0411 separate I&D) under apsim.
# apsim emulates the terminal (gtty/stty/ioctl sgttyb, gldav load-average, the
# signal/alarm stubs); a faux root supplies /etc/passwd (so getpwuid finds the
# player) and /etc/termcap (an ansi/vt100 entry).  Run this from a real terminal
# -- apsim puts it in raw mode and restores on exit.
set -e
TOP=$(cd "$(dirname "$0")/.." && pwd)
APSIM="$TOP/../bin/pdp11-apsim"
ROGUE=${1:-"$HOME/rogue3.4/rogue3.4.obj"}
ROOT=${APSIM_ROOT:-"$HOME/native-as/root"}
mkdir -p "$ROOT/etc" "$ROOT/tmp" "$ROOT/usr/games"
[ -f "$ROOT/etc/passwd" ] || printf 'root:*:0:0:Operator:/:/bin/sh\nrogue:*:1:1:Rogue Player:/usr/games:/bin/sh\n' > "$ROOT/etc/passwd"
[ -f "$ROOT/etc/termcap" ] || cat > "$ROOT/etc/termcap" <<'TC'
va|ansi|vt100|ansi/vt100 terminal:\
	:co#80:li#24:am:bs:\
	:cm=\E[%i%d;%dH:cl=\E[H\E[J:ce=\E[K:cd=\E[J:\
	:up=\E[A:nd=\E[C:ho=\E[H:so=\E[7m:se=\E[m:us=\E[4m:ue=\E[m:
TC
APSIM_ROOT="$ROOT" APSIM_ENV="TERM=ansi HOME=/usr/games ROGUEOPTS=" exec "$APSIM" "$ROGUE"
