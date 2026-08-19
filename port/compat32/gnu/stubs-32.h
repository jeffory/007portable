/* Stand-in for glibc's gnu/stubs-32.h so the port can COMPILE on hosts
 * without glibc-devel.i686. The real header only defines __stub_* macros
 * marking syscalls with no 32-bit implementation; an empty file is safe.
 *
 * LINKING still requires the real 32-bit runtime:
 *   sudo dnf install glibc-devel.i686 libgcc.i686
 *
 * This directory is only added to the include path when the real header is
 * missing (see CMakeLists.txt).
 */
