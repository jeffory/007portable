/* PORT: this used to be the SGI/IDO limits.h (unsigned-char CHAR_MIN 0,
 * MIPS _MIPS_SZLONG switches). On PC it must NOT shadow the toolchain's
 * header: bionic's fortify layer needs SSIZE_MAX from the real one, and we
 * compile -fsigned-char anyway. Nothing in the tree uses the SGI-only
 * macros (checked), so just delegate. */
#ifndef _GE_LIMITS_WRAP
#define _GE_LIMITS_WRAP
#include_next <limits.h>
/* the SGI header also carried FLT_MAX/DBL_MAX & co., and IDO-era code
 * includes <limits.h> expecting them */
#include <float.h>
#endif
