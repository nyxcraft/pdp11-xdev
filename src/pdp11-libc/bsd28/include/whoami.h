#define UCB_SYSNAME	Y
/* Hashed-passwd + default-shell site config, matching the host rogue was built
 * on.  Both are string values (getmap's TBL[], getpwent's pw_shell); they live
 * in the data segment, so they don't affect text byte-matching -- only what the
 * passwd routines link/use.  Their presence is what selects the UCB_PWHASH path. */
#define	UCB_PWHASH	"/etc/pw_map"
#define	UCB_SHELL	"/bin/csh"
#define	CORY	1
#define	sysname	"ucbcory"
#ifndef PDP11
#	define	PDP11	70
#endif
#include "sys/localopts.h"
# define BERKELEY
