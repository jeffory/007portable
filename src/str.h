#ifndef _STR_H_
#define _STR_H_

/* PORT: was N64-era declarations for src/str.c's hand-rolled libc clones
 * (the N64 had no libc). On PC the real libc provides all of them, and
 * redeclaring fortify-macro'd functions breaks Darwin — delegate. str.c
 * is no longer built. */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#endif
