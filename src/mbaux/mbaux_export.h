/*--------------------------------------------------------------------
 *    The MB-system:	mbaux_export.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * What mbaux exports on Windows: exactly the functions (and variables) declared MBAUX_API -- the ones
 * other MB-System libraries, programs and modules, or a host such as InteractiveGMT (by name, through
 * its FFI), use from outside the DLL. Nothing else leaves it: the build does not export every symbol.
 * CMake defines mbaux_EXPORTS while building the mbaux DLL itself. Elsewhere, and in a static build (MBAUX_STATIC), it is empty.
 */
#ifndef MBAUX_EXPORT_H
#define MBAUX_EXPORT_H

#if defined(_WIN32) && !defined(MBAUX_STATIC)
#	ifdef mbaux_EXPORTS
#		define MBAUX_API __declspec(dllexport)
#	else
#		define MBAUX_API __declspec(dllimport)
#	endif
#else
#	define MBAUX_API
#endif

#endif
