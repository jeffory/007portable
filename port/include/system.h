#ifndef PORT_SYSTEM_H
#define PORT_SYSTEM_H

/**
 * @file system.h
 * Log/arg/time services fast3d expects (PD-port interface, GE-port
 * implementation in port/src/system.c).
 */

/* fast3d sources use the N64 fixed-width names (u32 etc.) */
#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

enum logLevel {
    LOG_NOTE,
    LOG_WARNING,
    LOG_ERROR,
};

void sysLogPrintf(int level, const char *fmt, ...);
void sysFatalError(const char *fmt, ...) __attribute__((noreturn));

int sysArgCheck(const char *arg);
const char *sysArgGetString(const char *arg);

void sysSleep(unsigned long long usec);
void sysCpuRelax(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_H */
