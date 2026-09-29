/*--------------------------------------------------------------------
 *    The MB-system:  mbtrn_w32_sockets.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Common Winsock2 base for the BSD-socket headers the TRN sources include.
 *
 * The TRN networking code is written against POSIX sockets. Winsock2 supplies
 * the same names for almost all of it (socket/bind/listen/accept/connect/
 * send/recv/sendto/recvfrom/setsockopt/select, the AF_*, SOCK_*, SO_* and
 * IPPROTO_* constants, struct sockaddr_in, htons and friends), so the shim
 * headers in this directory mostly just pull in winsock2.h and fill the few
 * genuine gaps:
 *
 *   socklen_t      Winsock uses int for the address-length arguments.
 *   MSG_NOSIGNAL   No SIGPIPE on Windows, so the flag is a no-op.
 *   ssize_t        Not defined by MSVC.
 *
 * What this header deliberately does NOT do is redefine close(): the TRN
 * sources call close() on ordinary file descriptors far more often than on
 * sockets, so a blanket macro would break file I/O. Socket descriptors are
 * closed through mbtrn_w32_closesocket() at the call sites that need it.
 *
 * Winsock also needs an explicit startup call before any socket is created;
 * mbtrn_w32_socket_startup() does that once per process and is safe to call
 * repeatedly.
 */

#ifndef MBTRN_W32_SOCKETS_H
#define MBTRN_W32_SOCKETS_H

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>

/* Winsock spells the address-length type as int. */
#ifndef MBTRN_W32_HAVE_SOCKLEN_T
#define MBTRN_W32_HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

/* MSVC has no ssize_t. */
#ifndef MBTRN_W32_HAVE_SSIZE_T
#define MBTRN_W32_HAVE_SSIZE_T
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

/* Winsock has no per-call MSG_DONTWAIT; non-blocking is a socket property.
 * Give the flag a bit of its own, outside the range Winsock uses for its
 * own MSG_* values, and let msock_recvfrom() honour it by switching the
 * socket to non-blocking for the duration of the call. Defining it as 0
 * would silently turn a polling read into a blocking one. */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x10000000
#endif

/* There is no SIGPIPE on Windows, so suppressing it is a no-op. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* POSIX shutdown() how-values; Winsock spells them SD_*. */
#ifndef SHUT_RD
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif

/* Winsock reports errors through WSAGetLastError() rather than errno. */
#ifndef MBTRN_W32_SOCKET_ERRNO
#define MBTRN_W32_SOCKET_ERRNO WSAGetLastError()
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Initialises Winsock the first time it is called. Returns 0 on success and
 * the WSAStartup error code otherwise. Calling it more than once is harmless. */
static __inline int mbtrn_w32_socket_startup(void) {
	static volatile LONG started = 0;
	WSADATA wsa_data;
	int status;

	if (InterlockedCompareExchange(&started, 1, 0) != 0) {
		return 0; /* another call already did it */
	}
	status = WSAStartup(MAKEWORD(2, 2), &wsa_data);
	if (status != 0) {
		InterlockedExchange(&started, 0); /* let a later call retry */
	}
	return status;
}

/* Closes a socket descriptor. Separate from close() on purpose: see above. */
static __inline int mbtrn_w32_closesocket(int fd) { return closesocket((SOCKET)fd); }

/* Replacement for fcntl(fd, F_SETFL, O_NONBLOCK) and its inverse. */
static __inline int mbtrn_w32_set_nonblocking(int fd, int nonblocking) {
	u_long mode = nonblocking ? 1UL : 0UL;
	return ioctlsocket((SOCKET)fd, FIONBIO, &mode);
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */

#endif /* MBTRN_W32_SOCKETS_H */
