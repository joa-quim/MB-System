/*--------------------------------------------------------------------
 *    The MB-system:  mbtrn_w32_compat.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Force-included into every TRN translation unit on MSVC (see the /FI option
 * in the TRN CMakeLists), in the same way the tree already force-includes
 * mb_define.h.
 *
 * It covers the POSIX names the TRN sources use without including the header
 * that would declare them on a POSIX system. Being first in every unit also
 * guarantees winsock2.h is seen before windows.h, which is required: the
 * reverse order pulls in the incompatible winsock.h declarations.
 *
 * Layout matters here. Constants come first, because the wrapper functions
 * below reference them; the functions follow.
 *
 * Anything needing substantial behaviour lives in a shim header next to this
 * one (mbtrn_w32_sockets.h, mbtrn_w32_time.h, mbtrn_w32_queue.h, libgen.h).
 */

#ifndef MBTRN_W32_COMPAT_H
#define MBTRN_W32_COMPAT_H

#if defined(_WIN32)

#include "mbtrn_w32_sockets.h"   /* winsock2 first, plus socklen_t and ssize_t */
#include "mbtrn_w32_time.h"      /* gettimeofday, clock_gettime, nanosleep, ... */
#include "strings.h"             /* strcasecmp, strncasecmp */

#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>            /* PRIu32 and friends */
#include <malloc.h>              /* alloca */
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---------------------------------------------------------------- constants */

/* alloca() replaces the variable-length arrays MSVC does not support. */
#ifndef alloca
#define alloca _alloca
#endif

/* mode_t is not among the POSIX types MSVC supplies. */
#ifndef HAVE_MODE_T
#define HAVE_MODE_T 1   /* also stops pthreads-win32 semaphore.h redefining it */
typedef unsigned short mode_t;
#endif

/* Signals Windows does not define. They cannot actually be raised here, but
 * the sources reference them when installing handlers, so they need numbers
 * that do not collide with the ones MSVC does define (1-22). */
#ifndef SIGHUP
#define SIGHUP  101
#endif
#ifndef SIGQUIT
#define SIGQUIT 103
#endif
#ifndef SIGPIPE
#define SIGPIPE 113
#endif
#ifndef SIGALRM
#define SIGALRM 114
#endif
#ifndef SIGKILL
#define SIGKILL 109
#endif
#ifndef SIGUSR1
#define SIGUSR1 130
#endif
#ifndef SIGUSR2
#define SIGUSR2 131
#endif
#ifndef SIGSTOP
#define SIGSTOP 117
#endif
#ifndef SIGCONT
#define SIGCONT 119
#endif

/* Winsock has no SO_REUSEPORT. SO_REUSEADDR is the closest equivalent: on
 * Windows it already allows the multiple binds SO_REUSEPORT is used for here
 * (the multicast receivers in trnw). */
#ifndef SO_REUSEPORT
#define SO_REUSEPORT SO_REUSEADDR
#endif

/* fcntl() file-status commands and the one flag that matters. */
#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x0004
#endif

/* POSIX open() flags with no Windows equivalent. Writes are not forced to
 * disk, so these are accepted and ignored rather than silently changing the
 * open mode. */
#ifndef O_SYNC
#define O_SYNC 0
#endif
#ifndef O_DSYNC
#define O_DSYNC 0
#endif
#ifndef O_RSYNC
#define O_RSYNC 0
#endif
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

/* POSIX permission bits. MSVC has only _S_IREAD/_S_IWRITE, and Windows has no
 * group or other class, so the group/other bits map to the owner's. */
#ifndef S_IRWXU
#define S_IRWXU (_S_IREAD | _S_IWRITE | _S_IEXEC)
#endif
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif
#ifndef S_IXUSR
#define S_IXUSR _S_IEXEC
#endif
#ifndef S_IRWXG
#define S_IRWXG 0
#endif
#ifndef S_IRGRP
#define S_IRGRP 0
#endif
#ifndef S_IWGRP
#define S_IWGRP 0
#endif
#ifndef S_IXGRP
#define S_IXGRP 0
#endif
#ifndef S_IRWXO
#define S_IRWXO 0
#endif
#ifndef S_IROTH
#define S_IROTH 0
#endif
#ifndef S_IWOTH
#define S_IWOTH 0
#endif
#ifndef S_IXOTH
#define S_IXOTH 0
#endif

/* MSVC's <sys/stat.h> has only the _S_IF* bit constants. */
#ifndef S_ISDIR
#define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISCHR
#define S_ISCHR(m)  (((m) & _S_IFMT) == _S_IFCHR)
#endif
/* stat() follows links and there is no lstat(), so nothing is ever reported
 * as a link; lstat is the same call. */
#ifndef S_ISLNK
#define S_ISLNK(m)  (0)
#endif
#ifndef lstat
#define lstat stat
#endif

/* POSIX mkdir() takes a mode; the Windows one does not. */
#ifndef mkdir
#define mkdir(path, mode) _mkdir(path)
#endif

/* POSIX sleep() takes seconds; Win32 Sleep() takes milliseconds. mb_define.h
 * also defines sleep, but only inside its #ifndef CMAKE_BUILD_SYSTEM Autotools
 * section, which this build does not compile. */
#ifndef sleep
#define sleep(seconds) Sleep((DWORD)(seconds) * 1000)
#endif

/* MSVC spells the stdio locking calls with an underscore. */
/* MSVC spells the reentrant tokenizer strtok_s; same signature. */
#ifndef strtok_r
#define strtok_r strtok_s
#endif

#ifndef flockfile
#define flockfile(fp)   _lock_file(fp)
#endif
#ifndef funlockfile
#define funlockfile(fp) _unlock_file(fp)
#endif

#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

/* ---------------------------------------------------------------- functions */

#ifdef __cplusplus
extern "C" {
#endif

/* Symbolic links exist on Windows but behind the Win32 API rather than the C
 * runtime, and creating one needs administrator rights or developer mode -
 * hence the unprivileged-create flag. Failures are reported the POSIX way so
 * callers that already handle them keep working. */
static __inline int symlink(const char *target, const char *linkpath) {
	DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
	DWORD attributes = GetFileAttributesA(target);

	if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
		flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
	}
	if (CreateSymbolicLinkA(linkpath, target, flags)) return 0;
	errno = EACCES;   /* most often: not elevated and developer mode is off */
	return -1;
}

/* Resolves a link to its target. Returns the length written, or -1. */
static __inline int readlink(const char *path, char *buf, size_t bufsize) {
	HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	DWORD n;

	if (h == INVALID_HANDLE_VALUE) {
		errno = ENOENT;
		return -1;
	}
	n = GetFinalPathNameByHandleA(h, buf, (DWORD)bufsize, FILE_NAME_OPENED);
	CloseHandle(h);
	if (n == 0 || n >= (DWORD)bufsize) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return (int)n;
}

/* POSIX setenv/unsetenv over the MSVC _putenv_s. */
static __inline int setenv(const char *name, const char *value, int overwrite) {
	if (!overwrite) {
		size_t len = 0;
		if (getenv_s(&len, NULL, 0, name) == 0 && len > 0) return 0;
	}
	return _putenv_s(name, value ? value : "");
}

static __inline int unsetenv(const char *name) { return _putenv_s(name, ""); }

/* fsync/ftruncate map onto the MSVC low-level I/O equivalents. */
static __inline int fsync(int fd) { return _commit(fd); }
static __inline int ftruncate(int fd, long long length) { return _chsize_s(fd, length); }

/* vdprintf writes a formatted string straight to a descriptor. MSVC has no
 * equivalent, so format into a buffer and _write it. */
static __inline int vdprintf(int fd, const char *format, va_list ap) {
	char stackbuf[1024];
	char *buf = stackbuf;
	int needed;
	int written;
	va_list copy;

	va_copy(copy, ap);
	needed = _vscprintf(format, copy);
	va_end(copy);
	if (needed < 0) return -1;

	if ((size_t)needed + 1 > sizeof(stackbuf)) {
		buf = (char *)malloc((size_t)needed + 1);
		if (buf == NULL) return -1;
	}
	vsnprintf(buf, (size_t)needed + 1, format, ap);
	written = _write(fd, buf, (unsigned int)needed);
	if (buf != stackbuf) free(buf);
	return written;
}

/* fcntl(). Only the non-blocking flag is meaningful for the sockets the TRN
 * code applies it to, so F_SETFL maps onto ioctlsocket(FIONBIO). */
static __inline int mbtrn_w32_fcntl(int fd, int cmd, int arg) {
	if (cmd == F_SETFL) return mbtrn_w32_set_nonblocking(fd, (arg & O_NONBLOCK) ? 1 : 0);
	if (cmd == F_GETFL) return 0;   /* Winsock cannot read the flags back */
	return 0;
}

/* Winsock types the option value as const char*, while POSIX code passes
 * whatever the option needs (int*, struct timeval*, ...). Wrapping beats
 * casting at every call site. The wrappers are defined before the macros so
 * their own bodies are not rewritten. */
static __inline int mbtrn_w32_setsockopt(SOCKET s, int level, int optname, const void *optval, int optlen) {
	return setsockopt(s, level, optname, (const char *)optval, optlen);
}

static __inline int mbtrn_w32_getsockopt(SOCKET s, int level, int optname, void *optval, int *optlen) {
	return getsockopt(s, level, optname, (char *)optval, optlen);
}

/* strsignal(). Windows has no table of signal names. */
static __inline const char *strsignal(int signo) {
	switch (signo) {
		case SIGINT:  return "Interrupt";
		case SIGILL:  return "Illegal instruction";
		case SIGFPE:  return "Floating point exception";
		case SIGSEGV: return "Segmentation fault";
		case SIGTERM: return "Terminated";
		case SIGABRT: return "Aborted";
		case SIGHUP:  return "Hangup";
		case SIGQUIT: return "Quit";
		case SIGPIPE: return "Broken pipe";
		case SIGALRM: return "Alarm clock";
		case SIGKILL: return "Killed";
		case SIGUSR1: return "User defined signal 1";
		case SIGUSR2: return "User defined signal 2";
		default:      return "Unknown signal";
	}
}

/* strptime(). Supports the directives the TRN sources use - %Y %m %d %j %H
 * %M %S, whitespace and literal characters - which covers their single
 * format string "%Y %j %H:%M:%S". Unsupported directives make it stop and
 * return NULL rather than quietly mis-parsing. Note %j sets tm_yday; the
 * caller is expected to run the result through mktime(), as r7kc.c does. */
static __inline char *strptime(const char *buf, const char *format, struct tm *tm) {
	const char *b = buf;
	const char *f = format;

	if (b == NULL || f == NULL || tm == NULL) return NULL;

	while (*f != '\0') {
		if (*f != '%') {            /* not a % directive */
			if (*f == ' ') {        /* space matches any run of whitespace */
				while (*b == ' ' || *b == '\t') b++;
				f++;
				continue;
			}
			if (*b != *f) return NULL;
			b++; f++;
			continue;
		}

		f++;                             /* step over the % */
		{
			int value = 0;
			int digits = 0;
			int maxdigits;

			switch (*f) {
				case 'Y': maxdigits = 4; break;   /* Y */
				case 'j': maxdigits = 3; break;  /* j */
				case 'm':                        /* m */
				case 'd':                        /* d */
				case 'H':                         /* H */
				case 'M':                         /* M */
				case 'S': maxdigits = 2; break;   /* S */
				default: return NULL;                  /* unsupported directive */
			}

			while (*b == ' ') b++;
			while (digits < maxdigits && *b >= '0' && *b <= '9') {
				value = value * 10 + (*b - '0');
				b++; digits++;
			}
			if (digits == 0) return NULL;

			switch (*f) {
				case 'Y': tm->tm_year = value - 1900; break;
				case 'j': tm->tm_yday = value - 1; break;
				case 'm': tm->tm_mon = value - 1; break;
				case 'd': tm->tm_mday = value; break;
				case 'H': tm->tm_hour = value; break;
				case 'M': tm->tm_min = value; break;
				case 'S': tm->tm_sec = value; break;
				default: return NULL;
			}
			f++;
		}
	}
	return (char *)b;
}

/* sigaction() over signal(). Windows raises only a handful of signals, so the
 * mask and flags have nowhere to apply and are accepted but ignored. */
#ifndef MBTRN_W32_HAVE_SIGACTION
#define MBTRN_W32_HAVE_SIGACTION
typedef int sigset_t;

struct sigaction {
	void (*sa_handler)(int);
	sigset_t sa_mask;
	int sa_flags;
};

static __inline int sigemptyset(sigset_t *set) { if (set) *set = 0; return 0; }
static __inline int sigfillset(sigset_t *set) { if (set) *set = ~0; return 0; }
static __inline int sigaddset(sigset_t *set, int signo) { if (set) *set |= (1 << (signo & 31)); return 0; }

static __inline int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact) {
	void (*previous)(int);

	if (act == NULL) return -1;
	previous = signal(signo, act->sa_handler);
	if (previous == SIG_ERR) return -1;
	if (oldact != NULL) {
		oldact->sa_handler = previous;
		oldact->sa_mask = 0;
		oldact->sa_flags = 0;
	}
	return 0;
}
#endif /* MBTRN_W32_HAVE_SIGACTION */

#ifdef __cplusplus
}
#endif

/* Applied after the wrappers so the definitions above are left alone. */
#ifndef fcntl
#define fcntl(fd, cmd, arg) mbtrn_w32_fcntl((fd), (cmd), (int)(arg))
#endif
#define setsockopt mbtrn_w32_setsockopt
#define getsockopt mbtrn_w32_getsockopt

#endif /* _WIN32 */

#endif /* MBTRN_W32_COMPAT_H */
