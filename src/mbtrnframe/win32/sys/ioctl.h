/*--------------------------------------------------------------------
 *    The MB-system:  sys/ioctl.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * MSVC substitute for POSIX <sys/ioctl.h>, on the TRN include path for WIN32
 * only. The only ioctl the TRN code performs is on sockets, which Winsock
 * spells ioctlsocket(); that comes from mbtrn_w32_sockets.h.
 */
#ifndef MBTRN_W32_SYS_IOCTL_H
#define MBTRN_W32_SYS_IOCTL_H
#include "mbtrn_w32_sockets.h"
#endif
