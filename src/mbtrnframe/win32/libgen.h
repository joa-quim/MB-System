/*--------------------------------------------------------------------
 *    The MB-system:  libgen.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * MSVC substitute for POSIX <libgen.h>, on the TRN include path for WIN32 only.
 *
 * Provides basename() and dirname() with the POSIX semantics the TRN sources
 * rely on, including the awkward parts: both may modify the string they are
 * given, both may return a pointer into it, and both return a pointer to
 * static storage for the degenerate cases ("", "/", no separator). Windows
 * path separators are accepted as well as '/'.
 */

#ifndef MBTRN_W32_LIBGEN_H
#define MBTRN_W32_LIBGEN_H

#if defined(_WIN32)

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static __inline int mbtrn_w32_is_sep(char c) { return c == '/' || c == '\\'; }

static __inline char *basename(char *path) {
	static char dot[] = ".";
	char *p;
	size_t len;

	if (path == NULL || path[0] == '\0') return dot;

	/* Drop any trailing separators, as POSIX basename does. */
	len = strlen(path);
	while (len > 1 && mbtrn_w32_is_sep(path[len - 1])) path[--len] = '\0';

	/* A path that is nothing but separators resolves to the separator. */
	if (len == 1 && mbtrn_w32_is_sep(path[0])) return path;

	for (p = path + len; p > path; p--) {
		if (mbtrn_w32_is_sep(p[-1])) return p;
	}
	return path;
}

static __inline char *dirname(char *path) {
	static char dot[] = ".";
	static char root[] = "/";
	char *p;
	size_t len;

	if (path == NULL || path[0] == '\0') return dot;

	len = strlen(path);
	while (len > 1 && mbtrn_w32_is_sep(path[len - 1])) path[--len] = '\0';

	if (len == 1 && mbtrn_w32_is_sep(path[0])) return root;

	for (p = path + len; p > path; p--) {
		if (mbtrn_w32_is_sep(p[-1])) {
			/* Strip the separator(s) that end the directory part. */
			char *end = p - 1;
			while (end > path && mbtrn_w32_is_sep(end[-1])) end--;
			if (end == path) return root; /* the directory is the root itself */
			*end = '\0';
			return path;
		}
	}
	return dot; /* no separator at all: the directory is the current one */
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */

#endif /* MBTRN_W32_LIBGEN_H */
