/*--------------------------------------------------------------------
 *    The MB-system:  arpa/inet.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * MSVC substitute for POSIX <arpa/inet.h>, on the TRN include path for WIN32 only.
 * inet_addr/inet_ntoa/inet_pton/inet_ntop and htons and friends all come from Winsock2.
 */
#ifndef MBTRN_W32_ARPA_INET_H
#define MBTRN_W32_ARPA_INET_H
#include "mbtrn_w32_sockets.h"
#endif
