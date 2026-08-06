# Shared build configuration, included by every tool Makefile as its first
# line (include ../common/config.mk).  Paths are relative to a tool directory
# (src/pdp11-<tool>/).
#
# HOSTCC is the host C compiler used to build the tools themselves.  The host
# tools have been modernized to clean C99 (ANSI prototypes, explicit types, no
# BSD types); every function they call is either ISO C99 or POSIX, and the
# POSIX ones come from their standard headers -- no hand-written prototypes.
# The whole tree builds warning-free with NO -Wno-* suppressions.  (libc stays
# K&R -- it is target code for our own cc.)  The dialect + correctness flags:
#
#   -std=c99                   strict ISO C99
#   -D_POSIX_C_SOURCE=200809L  request the POSIX.1-2008 standard, so the
#                              standard headers declare the POSIX functions
#                              (readlink, setenv, mkstemp, popen, strdup,
#                              open_memstream) that -std=c99 alone hides.  This
#                              is the POSIX feature-test macro, NOT the glibc
#                              _DEFAULT_SOURCE grab-bag -- it exposes exactly
#                              the standard, and does not bring back BSD types.
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

COMPAT = -std=c99 -D_POSIX_C_SOURCE=200809L -fno-strict-aliasing -fwrapv -fcommon

# Locations, relative to a tool directory
COMMON = ../common
BIN    = ../../bin
