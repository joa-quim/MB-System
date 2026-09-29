/*--------------------------------------------------------------------
 *    The MB-system:  sys/time.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * MSVC substitute for POSIX <sys/time.h>, on the TRN include path for WIN32
 * only. struct timeval comes from Winsock2; gettimeofday() and the rest of the
 * POSIX time functions come from mbtrn_w32_time.h.
 */
#ifndef MBTRN_W32_SYS_TIME_H
#define MBTRN_W32_SYS_TIME_H
#include "mbtrn_w32_time.h"
#endif
