#include "mb_xdr_export.h"
/*--------------------------------------------------------------------
 *    The MB-system:  mb_types_win32.h 7/10/2026
 *
 *    Copyright (c) 1996-2026 by
 *    David W. Caress (caress@mbari.org)
 *      Monterey Bay Aquarium Research Institute
 *      Moss Landing, California, USA
 *    Dale N. Chayes 
 *      Center for Coastal and Ocean Mapping
 *      University of New Hampshire
 *      Durham, New Hampshire, USA
 *    Christian dos Santos Ferreira
 *      MARUM
 *      University of Bremen
 *      Bremen Germany
 *     
 *    MB-System was created by Caress and Chayes in 1992 at the
 *      Lamont-Doherty Earth Observatory
 *      Columbia University
 *      Palisades, NY 10964
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/**
 * @file 
 * @brief Define XDR functions that are standard on POSIX but not present on Windows 
 * 
 * Author:  D. W. Caress
 * Date:  July 10, 2026
 *
 * Derived from source file xdr_win32.h distributed as part of the Atlas SURF format
 * source code found in mbsystem/src/surf/
 */

/*
/  See mbsystem/src/surf/README file for copying and redistribution conditions.
*/

#ifndef XDRH_included
#define XDRH_included

#include <stdio.h>

#include "mb_types_win32.h"

#ifndef TRUE
#define TRUE  (1==1)
#endif
#ifndef FALSE
#define FALSE (0==1)
#endif



typedef enum
{
	XDR_ENCODE=0,
	XDR_DECODE=1,
	XDR_FREE=2
}XdrOp;


typedef struct
{
  XdrOp     x_op;
  char*   x_public;
  char*   x_private;
  char*   x_base;
  int       x_handy;
} XDR;


MBXDR_API void xdrstdio_create(XDR* xdrs,FILE* file,XdrOp op);
MBXDR_API void xdr_destroy(XDR* xdrs);
int xdr_long(XDR* xdrs,long* lp);
MBXDR_API int xdr_u_long(XDR* xdrs,u_long* ulp);
MBXDR_API int xdr_short(XDR* xdrs,short* sp);
MBXDR_API int xdr_u_short(XDR* xdrs,u_short* ulp);
MBXDR_API int xdr_int(XDR* xdrs,int* ip);
MBXDR_API int xdr_u_int(XDR* xdrs,u_int* ip);
MBXDR_API int xdr_char(XDR* xdrs,char* cp);
int xdr_u_char(XDR* xdrs,u_char* cp);
MBXDR_API int xdr_opaque(XDR* xdrs,char* cp,unsigned int cnt);
MBXDR_API int xdr_bytes(XDR* xdrs,char** cpp,u_int* sizep,u_int maxsize);
MBXDR_API int xdr_double(XDR* xdrs,double *dp);
MBXDR_API int xdr_float(XDR* xdrs,float *fp);

#endif
