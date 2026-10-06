/*--------------------------------------------------------------------
 *    The MB-system:	mb_xdr_export.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * What mb_xdr_win32 exports on Windows: exactly the functions (and variables) declared MBXDR_API -- the ones
 * other MB-System libraries, programs and modules, or a host such as InteractiveGMT (by name, through
 * its FFI), use from outside the DLL. Nothing else leaves it: the build does not export every symbol.
 * CMake defines mb_xdr_win32_EXPORTS while building the mb_xdr_win32 DLL itself. Elsewhere, and in a static build (MBXDR_STATIC), it is empty.
 */
#ifndef MB_XDR_EXPORT_H
#define MB_XDR_EXPORT_H

#if defined(_WIN32) && !defined(MBXDR_STATIC)
#	ifdef mb_xdr_win32_EXPORTS
#		define MBXDR_API __declspec(dllexport)
#	else
#		define MBXDR_API __declspec(dllimport)
#	endif
#else
#	define MBXDR_API
#endif

#endif
