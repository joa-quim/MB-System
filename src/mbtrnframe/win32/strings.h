/*--------------------------------------------------------------------
 *    The MB-system:  strings.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * MSVC substitute for POSIX <strings.h>, on the TRN include path for WIN32
 * only. MSVC provides the same functions under the underscore-prefixed names.
 */

#ifndef MBTRN_W32_STRINGS_H
#define MBTRN_W32_STRINGS_H

#if defined(_WIN32)

#include <string.h>

#ifndef strcasecmp
#define strcasecmp  _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif

#endif /* _WIN32 */

#endif /* MBTRN_W32_STRINGS_H */
