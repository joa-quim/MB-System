/*--------------------------------------------------------------------
 *    The MB-system:	trn_export.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * What each TRN library exports on Windows: exactly the functions, classes and variables declared
 * with its <LIB>_API macro -- the ones another TRN library or program uses from outside the DLL.
 * Nothing else leaves it: the build does not export every symbol. CMake defines <lib>_EXPORTS while
 * building that library's DLL, so its macro is dllexport there and dllimport everywhere else.
 * Elsewhere, and in a static build (TRN_STATIC), every macro is empty.
 *
 * mb1_msg.c is compiled into both trnw and mb1, and mb71_msg.c into both r7kr and mb1r, so their
 * macros export from either library.
 */
#ifndef TRN_EXPORT_H
#define TRN_EXPORT_H

#if defined(_WIN32) && !defined(TRN_STATIC)
#	define TRN_DLL_EXPORT __declspec(dllexport)
#	define TRN_DLL_IMPORT __declspec(dllimport)
#else
#	define TRN_DLL_EXPORT
#	define TRN_DLL_IMPORT
#endif

#ifdef mbtrnframe_EXPORTS
#	define MBTRNFRAME_API TRN_DLL_EXPORT
#else
#	define MBTRNFRAME_API TRN_DLL_IMPORT
#endif

#ifdef geolib_EXPORTS
#	define GEOLIB_API TRN_DLL_EXPORT
#else
#	define GEOLIB_API TRN_DLL_IMPORT
#endif

#ifdef newmat_EXPORTS
#	define NEWMAT_API TRN_DLL_EXPORT
#else
#	define NEWMAT_API TRN_DLL_IMPORT
#endif

#ifdef qnx_EXPORTS
#	define QNX_API TRN_DLL_EXPORT
#else
#	define QNX_API TRN_DLL_IMPORT
#endif

#ifdef tnav_EXPORTS
#	define TNAV_API TRN_DLL_EXPORT
#else
#	define TNAV_API TRN_DLL_IMPORT
#endif

#ifdef geocon_EXPORTS
#	define GEOCON_API TRN_DLL_EXPORT
#else
#	define GEOCON_API TRN_DLL_IMPORT
#endif

#ifdef trnw_EXPORTS
#	define TRNW_API TRN_DLL_EXPORT
#else
#	define TRNW_API TRN_DLL_IMPORT
#endif

#if defined(trnw_EXPORTS) || defined(mb1_EXPORTS)
#	define MB1_API TRN_DLL_EXPORT
#else
#	define MB1_API TRN_DLL_IMPORT
#endif

#ifdef netif_EXPORTS
#	define NETIF_API TRN_DLL_EXPORT
#else
#	define NETIF_API TRN_DLL_IMPORT
#endif

#ifdef trnucli_EXPORTS
#	define TRNUCLI_API TRN_DLL_EXPORT
#else
#	define TRNUCLI_API TRN_DLL_IMPORT
#endif

#ifdef trncli_EXPORTS
#	define TRNCLI_API TRN_DLL_EXPORT
#else
#	define TRNCLI_API TRN_DLL_IMPORT
#endif

#ifdef trnwcli_EXPORTS
#	define TRNWCLI_API TRN_DLL_EXPORT
#else
#	define TRNWCLI_API TRN_DLL_IMPORT
#endif

#ifdef r7kr_EXPORTS
#	define R7KR_API TRN_DLL_EXPORT
#else
#	define R7KR_API TRN_DLL_IMPORT
#endif

#if defined(r7kr_EXPORTS) || defined(mb1r_EXPORTS)
#	define MB71_API TRN_DLL_EXPORT
#else
#	define MB71_API TRN_DLL_IMPORT
#endif

#ifdef mb1r_EXPORTS
#	define MB1R_API TRN_DLL_EXPORT
#else
#	define MB1R_API TRN_DLL_IMPORT
#endif

#endif
