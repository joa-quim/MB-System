/*--------------------------------------------------------------------
 *    The MB-system:	mbbsio_export.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * What mbbsio exports on Windows: exactly the functions (and variables) declared MBBSIO_API -- the ones
 * other MB-System libraries, programs and modules, or a host such as InteractiveGMT (by name, through
 * its FFI), use from outside the DLL. Nothing else leaves it: the build does not export every symbol.
 * CMake defines mbbsio_EXPORTS while building the mbbsio DLL itself. Elsewhere, and in a static build (MBBSIO_STATIC), it is empty.
 */
#ifndef MBBSIO_EXPORT_H
#define MBBSIO_EXPORT_H

#if defined(_WIN32) && !defined(MBBSIO_STATIC)
#	ifdef mbbsio_EXPORTS
#		define MBBSIO_API __declspec(dllexport)
#	else
#		define MBBSIO_API __declspec(dllimport)
#	endif
#else
#	define MBBSIO_API
#endif

#endif
