/*--------------------------------------------------------------------
 *    The MB-system:	mb_gmt_text.c
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/* Text output of the MB-System GMT modules; see mb_gmt_text.h. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_gmt_text.h"

struct MB_GMT_TEXT {
	struct GMT_CTRL *GMT;
	FILE *fp;			/* the program's own file (or stdout), or NULL for GMT records */
	bool keep_open;			/* fp is stdout: never closed here */
	struct GMT_RECORD *Out;
	char record[GMT_BUFSIZ];	/* the record's text */
	char *line;			/* the line being built, up to its '\n' */
	size_t n_line, n_alloc;
	struct GMT_OPTION *options;	/* deferred: the module's options, for the output at the end */
	char *kept;			/* deferred: all the text put so far */
	size_t n_kept, a_kept;
};

static int mb_gmt_text_open(struct GMT_CTRL *GMT, struct GMT_OPTION *options) {
	struct GMTAPI_CTRL *API = GMT->parent;
	GMT_Set_Columns(API, GMT_OUT, 0, GMT_COL_FIX);	/* text only: no numerical columns */
	if (GMT_Init_IO(API, GMT_IS_DATASET, GMT_IS_TEXT, GMT_OUT, GMT_ADD_DEFAULT, 0, options) != GMT_NOERROR)
		return 1;
	if (GMT_Begin_IO(API, GMT_IS_DATASET, GMT_OUT, GMT_HEADER_OFF) != GMT_NOERROR)
		return 1;
	return 0;
}

struct MB_GMT_TEXT *mb_gmt_text_begin(struct GMT_CTRL *GMT, struct GMT_OPTION *options) {
	if (mb_gmt_text_open(GMT, options))
		return NULL;
	struct MB_GMT_TEXT *T = (struct MB_GMT_TEXT *)calloc(1, sizeof (struct MB_GMT_TEXT));
	if (T == NULL) return NULL;
	T->GMT = GMT;
	T->Out = gmt_new_record(GMT, NULL, T->record);
	return T;
}

struct MB_GMT_TEXT *mb_gmt_text_begin_deferred(struct GMT_CTRL *GMT, struct GMT_OPTION *options) {
	struct MB_GMT_TEXT *T = (struct MB_GMT_TEXT *)calloc(1, sizeof (struct MB_GMT_TEXT));
	if (T == NULL) return NULL;
	T->GMT = GMT;
	T->options = options;
	T->kept = (char *)malloc(1);
	if (T->kept == NULL) { free(T); return NULL; }
	T->kept[0] = '\0';
	T->a_kept = 1;
	return T;
}

struct MB_GMT_TEXT *mb_gmt_text_file(struct GMT_CTRL *GMT, const char *path) {
	FILE *fp = fopen(path, "w");
	if (fp == NULL) return NULL;
	struct MB_GMT_TEXT *T = (struct MB_GMT_TEXT *)calloc(1, sizeof (struct MB_GMT_TEXT));
	if (T == NULL) { fclose(fp); return NULL; }
	T->GMT = GMT;
	T->fp = fp;
	return T;
}

struct MB_GMT_TEXT *mb_gmt_text_stdout(struct GMT_CTRL *GMT) {
	struct MB_GMT_TEXT *T = (struct MB_GMT_TEXT *)calloc(1, sizeof (struct MB_GMT_TEXT));
	if (T == NULL) return NULL;
	T->GMT = GMT;
	T->fp = stdout;
	T->keep_open = true;
	return T;
}

/* One complete line (without its '\n') as one record */
static void mb_gmt_text_emit(struct MB_GMT_TEXT *T, const char *text, size_t len) {
	const size_t keep = (len < GMT_BUFSIZ - 1) ? len : GMT_BUFSIZ - 1;
	memcpy(T->record, text, keep);
	T->record[keep] = '\0';
	GMT_Put_Record(T->GMT->parent, GMT_WRITE_DATA, T->Out);
}

static void mb_gmt_text_append(struct MB_GMT_TEXT *T, const char *text, size_t len) {
	if (T->n_line + len + 1 > T->n_alloc) {
		size_t n = T->n_alloc ? T->n_alloc : 256;
		while (n < T->n_line + len + 1) n *= 2;
		char *grown = (char *)realloc(T->line, n);
		if (grown == NULL) return;
		T->line = grown;
		T->n_alloc = n;
	}
	memcpy(T->line + T->n_line, text, len);
	T->n_line += len;
}

/* Text as records: every '\n' completes a line, the rest starts the next one */
static void mb_gmt_text_lines(struct MB_GMT_TEXT *T, const char *text) {
	const char *p = text;
	const char *eol;
	while ((eol = strchr(p, '\n')) != NULL) {
		if (T->n_line) {
			mb_gmt_text_append(T, p, (size_t)(eol - p));
			mb_gmt_text_emit(T, T->line, T->n_line);
			T->n_line = 0;
		}
		else
			mb_gmt_text_emit(T, p, (size_t)(eol - p));
		p = eol + 1;
	}
	if (*p) mb_gmt_text_append(T, p, strlen(p));
}

void mb_gmt_text_put(struct MB_GMT_TEXT *T, const char *format, ...) {
	va_list ap;
	if (T == NULL) return;
	if (T->fp) {	/* the program's own file: exactly as it writes it */
		va_start(ap, format);
		vfprintf(T->fp, format, ap);
		va_end(ap);
		return;
	}
	va_start(ap, format);
	const int n = vsnprintf(NULL, 0, format, ap);
	va_end(ap);
	if (n <= 0) return;
	if (T->kept) {	/* deferred: kept as it is until mb_gmt_text_end() */
		if (T->n_kept + (size_t)n + 1 > T->a_kept) {
			size_t a = T->a_kept;
			while (a < T->n_kept + (size_t)n + 1) a *= 2;
			char *grown = (char *)realloc(T->kept, a);
			if (grown == NULL) return;
			T->kept = grown;
			T->a_kept = a;
		}
		va_start(ap, format);
		vsnprintf(T->kept + T->n_kept, T->a_kept - T->n_kept, format, ap);
		va_end(ap);
		T->n_kept += (size_t)n;
		return;
	}
	char *text = (char *)malloc((size_t)n + 1);
	if (text == NULL) return;
	va_start(ap, format);
	vsnprintf(text, (size_t)n + 1, format, ap);
	va_end(ap);
	mb_gmt_text_lines(T, text);
	free(text);
}

int mb_gmt_text_end(struct MB_GMT_TEXT *T) {
	int err = 0;
	if (T == NULL) return 1;
	if (T->kept) {	/* deferred: the output is opened now, after the modules this one ran */
		char *kept = T->kept;
		T->kept = NULL;
		if (mb_gmt_text_open(T->GMT, T->options)) {
			free(kept);
			free(T->line);
			free(T);
			return 1;
		}
		T->Out = gmt_new_record(T->GMT, NULL, T->record);
		mb_gmt_text_lines(T, kept);
		free(kept);
	}
	if (T->fp)
		err = T->keep_open ? (fflush(T->fp) != 0) : (fclose(T->fp) != 0);
	else {
		if (T->n_line) mb_gmt_text_emit(T, T->line, T->n_line);	/* printf never wrote its '\n' */
		gmt_M_free(T->GMT, T->Out);
		err = (GMT_End_IO(T->GMT->parent, GMT_OUT, 0) != GMT_NOERROR);
	}
	free(T->line);
	free(T);
	return err;
}
