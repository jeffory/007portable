/* PC port: forward to the host stdlib.h (its own guard); the IDO decls
 * below are C89-gated and break C++/glibc-internal consumers. */
#include_next <stdlib.h>
