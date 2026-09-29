/*--------------------------------------------------------------------
 *    The MB-system:  sys/select.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * MSVC substitute for POSIX <sys/select.h>, on the TRN include path for WIN32 only.
 * select() and the fd_set macros come from Winsock2.
 */
#ifndef MBTRN_W32_SYS_SELECT_H
#define MBTRN_W32_SYS_SELECT_H
#include "mbtrn_w32_sockets.h"
#endif
