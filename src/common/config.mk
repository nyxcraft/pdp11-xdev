# Shared build configuration, included by every tool Makefile as its first
# line (include ../common/config.mk).  Paths are relative to a tool directory
# (src/pdp11-<tool>/).
#
# HOSTCC is the host C compiler used to build the tools themselves.  The host
# tools have been modernized to clean, STRICT ISO C99 (ANSI prototypes,
# explicit types, no BSD types, POSIX/XSI entry points declared explicitly in
# the sources rather than via a feature-test macro) -- so COMPAT is plain
# `-std=c99' with NO _DEFAULT_SOURCE / _GNU_SOURCE and NO -Wno-* suppressions:
# the whole tree builds warning-free.  (libc stays K&R -- it is target code for
# our own cc.)  Three semantic flags remain because they are CORRECTNESS for
# this code, not dialect:
#
#   -std=c99                   strict ISO C99
#   -fno-strict-aliasing       the code type-puns through unions and int/char*
#                              casts (the on-disk a.out/ar word access); TBAA
#                              would miscompile it
#   -fwrapv                    signed overflow is assumed to wrap (hash
#                              functions, 16-bit arithmetic emulation)
#   -fcommon                   tentative definitions appear in several .c
#                              files of the same tool (pre-C99 linkage model)
#
# Two tools carry a single scoped `#pragma GCC diagnostic' each for a
# genuinely-deliberate low-level pattern (word-wise packed on-disk struct
# access, a provably-contradictory sprintf-buffer cycle); grep the sources.

HOSTCC ?= cc
OPT    ?= -O
YACC   ?= yacc -Wno-yacc -Wno-other -Wno-conflicts-sr

COMPAT = -std=c99 -fno-strict-aliasing -fwrapv -fcommon

# Locations, relative to a tool directory
COMMON = ../common
BIN    = ../../bin
