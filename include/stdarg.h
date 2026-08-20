/* PC port: forward to the compiler's stdarg.h unconditionally — it
 * implements the __need___va_list re-include protocol glibc relies on. */
#include_next <stdarg.h>
