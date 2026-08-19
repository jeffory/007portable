#ifndef _MATH_ASINACOS_H_
#define _MATH_ASINACOS_H_

#include <ultra64.h>

#ifndef TARGET_N64
/* PC port: these s16 variants collide with the libm prototypes that the
 * host math.h declares; rename them out of the way. */
#define acos geAcosS16
#define asin geAsinS16
#endif

u16 acos(s16 arg0);
s16 asin(s16 arg0);

#endif
