/*--------------------------------------------------------------------
 *    The MB-system:  unistd_w.h
 *
 *    MSVC substitute for POSIX <unistd.h>. MSVC ships no <unistd.h>;
 *    this provides the small subset MB-System sources rely on. MinGW-w64
 *    has a real <unistd.h>, so it is selected at each call site with
 *    _MSC_VER, never _WIN32:
 *
 *        #ifdef _MSC_VER
 *        #include "unistd_w.h"
 *        #else
 *        #include <unistd.h>
 *        #endif
 *
 *    The other POSIX names MSVC lacks (strtok_r, popen, sleep, fseeko,
 *    S_ISDIR, ...) are mapped in mb_define.h under #ifdef _MSC_VER.
 *--------------------------------------------------------------------*/
#ifndef UNISTD_W_H
#define UNISTD_W_H

#include <direct.h>
#include <process.h>
#include <io.h>

/* POSIX declares getopt(), optarg, optind and friends in <unistd.h>, so
 * code that includes only <unistd.h> expects them here.
 * The GMT modules (src/gmt) define MB_NO_SYSTEM_GETOPT: GMT parses their
 * options, and getopt-win32 (vcpkg, conda-forge) does "#define option option_a",
 * which renames the 'option' member of GMT's struct GMT_OPTION. */
#ifndef MB_NO_SYSTEM_GETOPT
#include <getopt.h>
#endif

#ifndef PATH_MAX
#  define PATH_MAX 1024
#endif

#ifdef _MSC_VER
#  ifndef R_OK
#    define R_OK 04
#    define W_OK 02
#    define X_OK 01
#    define F_OK 00
#  endif
#endif

#endif /* UNISTD_W_H */
