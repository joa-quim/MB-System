/*--------------------------------------------------------------------
 *    The MB-system:  mbtrn_w32_time.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * POSIX time functions the TRN sources use that MSVC does not provide:
 * gettimeofday(), clock_gettime(), nanosleep(), usleep(), gmtime_r() and
 * localtime_r().
 *
 * struct timeval comes from Winsock2 and struct timespec from MSVC's own
 * <time.h> (VS2015 and later), so only the functions are supplied here.
 *
 * The wall-clock sources use GetSystemTimePreciseAsFileTime, which has
 * 100 ns resolution; CLOCK_MONOTONIC uses QueryPerformanceCounter so that it
 * is unaffected by clock adjustments, as the POSIX one is.
 */

#ifndef MBTRN_W32_TIME_H
#define MBTRN_W32_TIME_H

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>   /* struct timeval */
#include <windows.h>
#include <time.h>
#include <errno.h>

/* Number of 100 ns ticks between 1601-01-01 (the FILETIME epoch) and
 * 1970-01-01 (the Unix epoch). */
#define MBTRN_W32_EPOCH_DELTA_100NS 116444736000000000ULL

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME  0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MBTRN_W32_HAVE_CLOCKID_T
#define MBTRN_W32_HAVE_CLOCKID_T
typedef int clockid_t;
#endif

/* The obsolete second argument of gettimeofday(). Still named by some
 * sources; the fields are never filled in, as on Linux. */
#ifndef MBTRN_W32_HAVE_STRUCT_TIMEZONE
#define MBTRN_W32_HAVE_STRUCT_TIMEZONE
struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};
#endif

/* 100 ns ticks since the Unix epoch, from the system wall clock. */
static __inline unsigned long long mbtrn_w32_unix_100ns(void) {
	FILETIME ft;
	ULARGE_INTEGER value;

	GetSystemTimePreciseAsFileTime(&ft);
	value.LowPart = ft.dwLowDateTime;
	value.HighPart = ft.dwHighDateTime;
	return (unsigned long long)value.QuadPart - MBTRN_W32_EPOCH_DELTA_100NS;
}

static __inline int gettimeofday(struct timeval *tv, void *tz) {
	unsigned long long ticks;

	(void)tz; /* the POSIX timezone argument is obsolete and unused here */
	if (tv == NULL) return -1;
	ticks = mbtrn_w32_unix_100ns();
	tv->tv_sec = (long)(ticks / 10000000ULL);
	tv->tv_usec = (long)((ticks % 10000000ULL) / 10ULL);
	return 0;
}

static __inline int clock_gettime(clockid_t clock_id, struct timespec *ts) {
	if (ts == NULL) return -1;

	if (clock_id == CLOCK_MONOTONIC) {
		static LARGE_INTEGER frequency;
		LARGE_INTEGER counter;

		if (frequency.QuadPart == 0 && !QueryPerformanceFrequency(&frequency)) {
			errno = EINVAL;
			return -1;
		}
		QueryPerformanceCounter(&counter);
		ts->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
		ts->tv_nsec = (long)(((counter.QuadPart % frequency.QuadPart) * 1000000000LL) / frequency.QuadPart);
		return 0;
	}

	{
		const unsigned long long ticks = mbtrn_w32_unix_100ns();
		ts->tv_sec = (time_t)(ticks / 10000000ULL);
		ts->tv_nsec = (long)((ticks % 10000000ULL) * 100ULL);
	}
	return 0;
}

/* Sleep() has millisecond granularity, so sub-millisecond requests are
 * rounded up to 1 ms rather than silently becoming no-ops. */
static __inline int nanosleep(const struct timespec *request, struct timespec *remain) {
	DWORD milliseconds;

	if (remain != NULL) {
		remain->tv_sec = 0;
		remain->tv_nsec = 0;
	}
	if (request == NULL) return -1;

	milliseconds = (DWORD)(request->tv_sec * 1000ULL + (unsigned long long)request->tv_nsec / 1000000ULL);
	if (milliseconds == 0 && (request->tv_sec > 0 || request->tv_nsec > 0)) milliseconds = 1;
	Sleep(milliseconds);
	return 0;
}

/* clock_getres/clock_setres. The monotonic clock resolution comes from the
 * performance counter frequency; the realtime clock is the 100 ns system
 * clock. Windows offers no way to set a clock resolution, so clock_setres
 * reports failure rather than pretending to have changed anything. */
static __inline int clock_getres(clockid_t clock_id, struct timespec *res) {
	if (res == NULL) return -1;
	if (clock_id == CLOCK_MONOTONIC) {
		LARGE_INTEGER frequency;
		if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart == 0) {
			errno = EINVAL;
			return -1;
		}
		res->tv_sec = 0;
		res->tv_nsec = (long)(1000000000LL / frequency.QuadPart);
		if (res->tv_nsec == 0) res->tv_nsec = 1;
		return 0;
	}
	res->tv_sec = 0;
	res->tv_nsec = 100;   /* GetSystemTimePreciseAsFileTime ticks */
	return 0;
}

static __inline int clock_setres(clockid_t clock_id, struct timespec *res) {
	(void)clock_id;
	(void)res;
	errno = ENOSYS;   /* Windows clocks have a fixed resolution */
	return -1;
}

static __inline int usleep(unsigned long microseconds) {
	DWORD milliseconds = (DWORD)(microseconds / 1000UL);

	if (milliseconds == 0 && microseconds > 0) milliseconds = 1;
	Sleep(milliseconds);
	return 0;
}

/* MSVC's gmtime_s/localtime_s take their arguments in the opposite order to
 * the POSIX _r functions and return an error code rather than a pointer. */
static __inline struct tm *gmtime_r(const time_t *timep, struct tm *result) {
	if (timep == NULL || result == NULL) return NULL;
	return (gmtime_s(result, timep) == 0) ? result : NULL;
}

static __inline struct tm *localtime_r(const time_t *timep, struct tm *result) {
	if (timep == NULL || result == NULL) return NULL;
	return (localtime_s(result, timep) == 0) ? result : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */

#endif /* MBTRN_W32_TIME_H */
