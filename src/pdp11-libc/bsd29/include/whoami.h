#define _WHOAMI			/* so param.h won't include us again */

#define VIRUS
/* #define MYNAME "virus"		/* for uucp */
#define PDP11	40
#define	NONFP			/* no floating point unit */

#ifdef	KERNEL
#    include "localopts.h"
#else
#    include <sys/localopts.h>
#endif
