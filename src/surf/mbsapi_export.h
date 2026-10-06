/*--------------------------------------------------------------------
 *    The MB-system:	mbsapi_export.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * What mbsapi exports on Windows: exactly the functions (and variables) declared MBSAPI_API -- the ones
 * other MB-System libraries, programs and modules, or a host such as InteractiveGMT (by name, through
 * its FFI), use from outside the DLL. Nothing else leaves it: the build does not export every symbol.
 * CMake defines mbsapi_EXPORTS while building the mbsapi DLL itself. Elsewhere, and in a static build (MBSAPI_STATIC), it is empty.
 */
#ifndef MBSAPI_EXPORT_H
#define MBSAPI_EXPORT_H

#if defined(_WIN32) && !defined(MBSAPI_STATIC)
#	ifdef mbsapi_EXPORTS
#		define MBSAPI_API __declspec(dllexport)
#	else
#		define MBSAPI_API __declspec(dllimport)
#	endif
#else
#	define MBSAPI_API
#endif

#endif
