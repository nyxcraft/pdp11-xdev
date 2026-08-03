/*
 * Cross build configuration header (replaces the per-system <whoami.h>).
 *
 * The authentic 2.8BSD whoami.h does `#include "sys/localopts.h"'; we mirror
 * that with the verbatim authentic localopts.h under cross/sys/.  Its only
 * symbol the C compiler and binutils reference is MENLO_OVLY (text overlays);
 * everything else there is kernel/FS configuration that the tools ignore.
 *
 * MENLO_OVLY is REQUIRED for the compiler to be internally consistent: c0
 * unconditionally emits SETSTK as `-maxauto+STAUTO' (its "MENLO_OVLY bug
 * fix"), and c1 only matches that with the MENLO formula `t = geti()'.  With
 * MENLO_OVLY undefined, c1's `geti()-6' makes `sub $-6,sp' move sp up into
 * the csv-saved registers, so a callee passing an argument through (sp)
 * (e.g. fclose -> fflush/close) clobbers the caller's saved r4.  The overlay
 * code it also enables stays dormant: ovlyflag needs c1 argc>4 (cc passes 4),
 * STAUTO=-8 needs the -V flag (never passed), and the overlay switch
 * templates differ only by a cosmetic tab.  (nn_type/nn_ovno used by the
 * binutils' MENLO paths are aliased to n_type/n_ovly in <a.out.h>.)
 */
#ifndef PDP11
#define PDP11	70		/* machine type, as the authentic whoami.h sets */
#endif
#include <sys/localopts.h>
