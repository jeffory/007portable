#ifdef TARGET_N64
#ifndef _STRING_H_
#define _STRING_H_
#include <PR/ultratypes.h>

extern void *memcpy(void *, const void *, size_t);
extern unsigned char *strchr(const unsigned char *, int);
extern size_t strlen(const unsigned char *);

#endif
#else
/* PC port: forward to the host libc string.h (it has its own guard) */
#include_next <string.h>
#endif
