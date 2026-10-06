/*--------------------------------------------------------------------
 *    The MB-system:	mbgsf_export.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * What mbgsf exports on Windows: exactly the functions (and variables) declared MBGSF_API -- the ones
 * other MB-System libraries, programs and modules, or a host such as InteractiveGMT (by name, through
 * its FFI), use from outside the DLL. Nothing else leaves it: the build does not export every symbol.
 * CMake defines mbgsf_EXPORTS while building the mbgsf DLL itself. Elsewhere, and in a static build (MBGSF_STATIC), it is empty.
 */
#ifndef MBGSF_EXPORT_H
#define MBGSF_EXPORT_H

#if defined(_WIN32) && !defined(MBGSF_STATIC)
#	ifdef mbgsf_EXPORTS
#		define MBGSF_API __declspec(dllexport)
#	else
#		define MBGSF_API __declspec(dllimport)
#	endif
#else
#	define MBGSF_API
#endif

#endif
