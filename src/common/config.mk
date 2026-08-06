# Shared build configuration, included by every tool Makefile as its first
# line (include ../common/config.mk).  Paths are relative to a tool directory
# (src/pdp11-<tool>/).
#
# HOSTCC is the host C compiler used to build the tools themselves.  The host
# tools have been modernized to clean C99 (ANSI prototypes, explicit types,
# proper headers; libc stays K&R, since it is target code for our own cc), so
# COMPAT no longer carries any -Wno-* suppressions -- every tool compiles
# warning-free.  Three semantic flags remain because they are CORRECTNESS for
# this code, not dialect:
#
#   -std=gnu99                 C99 with the traditional Unix feature macros, so
#                              the BSD types (u_short/u_char/u_int) and mktemp
#                              resolve without a _DEFAULT_SOURCE define
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

COMPAT = -std=gnu99 -fno-strict-aliasing -fwrapv -fcommon

# Locations, relative to a tool directory
COMMON = ../common
BIN    = ../../bin
