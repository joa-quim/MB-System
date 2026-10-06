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
};

struct MB_GMT_TEXT *mb_gmt_text_begin(struct GMT_CTRL *GMT, struct GMT_OPTION *options) {
	struct GMTAPI_CTRL *API = GMT->parent;
	GMT_Set_Columns(API, GMT_OUT, 0, GMT_COL_FIX);	/* text only: no numerical columns */
	if (GMT_Init_IO(API, GMT_IS_DATASET, GMT_IS_TEXT, GMT_OUT, GMT_ADD_DEFAULT, 0, options) != GMT_NOERROR)
		return NULL;
	if (GMT_Begin_IO(API, GMT_IS_DATASET, GMT_OUT, GMT_HEADER_OFF) != GMT_NOERROR)
		return NULL;
	struct MB_GMT_TEXT *T = (struct MB_GMT_TEXT *)calloc(1, sizeof (struct MB_GMT_TEXT));
	if (T == NULL) return NULL;
	T->GMT = GMT;
	T->Out = gmt_new_record(GMT, NULL, T->record);
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
	char *text = (char *)malloc((size_t)n + 1);
	if (text == NULL) return;
	va_start(ap, format);
	vsnprintf(text, (size_t)n + 1, format, ap);
	va_end(ap);

	const char *p = text;
	const char *eol;
	while ((eol = strchr(p, '\n')) != NULL) {	/* every '\n' completes a line */
		if (T->n_line) {
			mb_gmt_text_append(T, p, (size_t)(eol - p));
			mb_gmt_text_emit(T, T->line, T->n_line);
			T->n_line = 0;
		}
		else
			mb_gmt_text_emit(T, p, (size_t)(eol - p));
		p = eol + 1;
	}
	if (*p) mb_gmt_text_append(T, p, strlen(p));	/* the start of the next line */
	free(text);
}

int mb_gmt_text_end(struct MB_GMT_TEXT *T) {
	int err = 0;
	if (T == NULL) return 1;
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
