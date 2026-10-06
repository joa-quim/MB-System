/*--------------------------------------------------------------------
 *    The MB-system:	mbio_export.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * What mbio exports on Windows: exactly the functions (and variables) declared MBIO_API -- the ones
 * other MB-System libraries, programs and modules, or a host such as InteractiveGMT (by name, through
 * its FFI), use from outside the DLL. Nothing else leaves it: the build does not export every symbol.
 * CMake defines mbio_EXPORTS while building the mbio DLL itself. Elsewhere, and in a static build
 * (MBIO_STATIC), the macro is empty.
 *
 * MBBITPACK_API is the same for the mbbitpack DLL: its C API is declared here in mbio's public header
 * (mb_define.h), mbio being its only user, and mb_bitpack.cc takes the macro from this file too.
 */
#ifndef MBIO_EXPORT_H
#define MBIO_EXPORT_H

#if defined(_WIN32) && !defined(MBIO_STATIC)
#	ifdef mbio_EXPORTS
#		define MBIO_API __declspec(dllexport)
#	else
#		define MBIO_API __declspec(dllimport)
#	endif
#else
#	define MBIO_API
#endif

#if defined(_WIN32) && !defined(MBBITPACK_STATIC)
#	ifdef mbbitpack_EXPORTS
#		define MBBITPACK_API __declspec(dllexport)
#	else
#		define MBBITPACK_API __declspec(dllimport)
#	endif
#else
#	define MBBITPACK_API
#endif

#endif
