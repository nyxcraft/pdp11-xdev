# Shared build configuration, included by every tool Makefile as its first
# line (include ../common/config.mk).  Paths are relative to a tool directory
# (src/pdp11-<tool>/).
#
# HOSTCC is the host C compiler used to build the tools themselves; the
# vintage sources are K&R C, so COMPAT carries the dialect and semantic
# flags they need on a modern GCC/Clang:
#
#   -std=gnu89                 the sources predate C99 (implicit int,
#                              old-style definitions, $ in identifiers)
#   -fno-strict-aliasing       the code type-puns through unions and int/char*
#                              casts everywhere; TBAA would miscompile it
#   -fwrapv                    signed overflow is assumed to wrap (hash
#                              functions, 16-bit arithmetic emulation)
#   -fcommon                   tentative definitions appear in several .c
#                              files of the same tool (pre-C99 linkage model)
#
# The -Wno-* set silences diagnostics that are idiomatic in 1981 C rather
# than bugs (int-as-pointer, implicit declarations, old varargs).  Real
# porting bugs were found by reading and by the regression suites, not by
# warnings; see NOTES.md.

HOSTCC ?= cc
OPT    ?= -O
YACC   ?= yacc -Wno-yacc -Wno-other -Wno-conflicts-sr

COMPAT = -std=gnu89 -Wno-int-conversion -Wno-incompatible-pointer-types \
         -Wno-unused-result -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
         -Wno-format -Wno-endif-labels -Wno-builtin-declaration-mismatch \
         -Wno-return-type -Wno-implicit-int -Wno-implicit-function-declaration \
         -fno-strict-aliasing -fwrapv -fcommon

# Locations, relative to a tool directory
COMMON = ../common
BIN    = ../../bin
