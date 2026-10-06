/*--------------------------------------------------------------------
 *    The MB-system:	mb_gmt_simrad.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Translation of an old Simrad (MB_SYS_SIMRAD: formats 51, 53, 54) data store into a Simrad2
 * (MB_SYS_SIMRAD2) store, for writing as MBF_EM300MBA (format 57). Moved here from the mbcopy
 * module (mbcopy_simrad_to_simrad2) so that mbcopy and mbpreprocess translate through ONE function.
 */

#ifndef MB_GMT_SIMRAD_H
#define MB_GMT_SIMRAD_H

#include "gmt_dev.h"
#include "mb_define.h"   /* mb_s_char / mb_u_char, which mbsys_simrad.h uses without including it */
#include "mbsys_simrad.h"
#include "mbsys_simrad2.h"

int mb_gmt_simrad_to_simrad2(struct GMTAPI_CTRL *API, int verbose, struct mbsys_simrad_struct *istore,
                             struct mbsys_simrad2_struct *ostore, int *error);

#endif
