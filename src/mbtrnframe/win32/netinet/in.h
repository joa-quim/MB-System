/*--------------------------------------------------------------------
 *    The MB-system:  netinet/in.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * MSVC substitute for POSIX <netinet/in.h>, on the TRN include path for WIN32 only.
 * struct sockaddr_in, struct ip_mreq and the IPPROTO_* constants come from Winsock2.
 */
#ifndef MBTRN_W32_NETINET_IN_H
#define MBTRN_W32_NETINET_IN_H
#include "mbtrn_w32_sockets.h"
#endif
