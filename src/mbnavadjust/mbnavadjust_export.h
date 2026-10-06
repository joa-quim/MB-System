/*--------------------------------------------------------------------
 *    The MB-system:	mbnavadjust_export.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * What mbnavadjust_core exports on Windows: exactly the functions (and variables) declared MBNAVADJUST_API -- the ones
 * other MB-System libraries, programs and modules, or a host such as InteractiveGMT (by name, through
 * its FFI), use from outside the DLL. Nothing else leaves it: the build does not export every symbol.
 * CMake defines mbnavadjust_core_EXPORTS while building the mbnavadjust_core DLL itself. Elsewhere, and in a static build (MBNAVADJUST_STATIC), it is empty.
 */
#ifndef MBNAVADJUST_EXPORT_H
#define MBNAVADJUST_EXPORT_H

#if defined(_WIN32) && !defined(MBNAVADJUST_STATIC)
#	ifdef mbnavadjust_core_EXPORTS
#		define MBNAVADJUST_API __declspec(dllexport)
#	else
#		define MBNAVADJUST_API __declspec(dllimport)
#	endif
#else
#	define MBNAVADJUST_API
#endif

#endif
