#!/bin/sh
# mkroot.sh -- populate the apsim runtime root.
#
# The skeleton (etc/passwd, etc/group, etc/termcap, etc/motd, empty bin
# dirs) is checked in.  This script adds optional content:
#
#   --rogue DIR?      install the reconstructed rogue 3.4 into usr/games
#                     (default: ~/rogue3.4)
#
# 2.8BSD shipped as source; /bin userland binaries arrive here as the
# toolchain (and eventually the self-hosted bootstrap) builds them.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/root"
RG="$HOME/rogue3.4"
DO_ROGUE=no
while [ $# -gt 0 ]; do
	case "$1" in
	--rogue) DO_ROGUE=yes; shift;;
	*) echo "usage: mkroot.sh [--rogue]" >&2; exit 1;;
	esac
done
mkdir -p "$ROOT/tmp" "$ROOT/usr/tmp"
chmod 1777 "$ROOT/tmp" "$ROOT/usr/tmp" 2>/dev/null
if [ "$DO_ROGUE" = yes ]; then
	OBJ="$RG/rogue3.4.obj"
	if [ -f "$OBJ" ]; then
		cp "$OBJ" "$ROOT/usr/games/rogue"
		chmod 755 "$ROOT/usr/games/rogue"
		echo "installed usr/games/rogue ($(wc -c < "$OBJ") bytes)"
	else
		echo "mkroot.sh: $OBJ not found" >&2
	fi
fi
echo "root ready: $ROOT"
