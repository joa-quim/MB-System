/*--------------------------------------------------------------------
 *    The MB-system:	mb_gmt_text.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Text output of the MB-System GMT modules. What the programs print with printf, the modules write
 * through this one writer, which has printf's semantics (a line may be built by several calls; it
 * is complete at its '\n') and two destinations:
 *   - mb_gmt_text_begin(): text records through the GMT API (the sequence GMT's own grdinfo and
 *     gmtinfo use), so the command line still gets them on stdout and a GMT.jl/Python/MATLAB caller
 *     gets them back as a dataset;
 *   - mb_gmt_text_file(): a file the PROGRAM itself writes (mbinfo -O's .inf, ...), unchanged.
 * ONE implementation for every module.
 *
 *   struct MB_GMT_TEXT *T = mb_gmt_text_begin(GMT, options);
 *   if (T == NULL) Return(API->error);
 *   mb_gmt_text_put(T, "lonflip:    %d\n", lonflip);	  -- printf's format, unchanged
 *   ...
 *   if (mb_gmt_text_end(T)) Return(API->error);
 */

#ifndef MB_GMT_TEXT_H
#define MB_GMT_TEXT_H

#include <stdio.h>
#include "gmt_dev.h"

struct MB_GMT_TEXT;

/* Records through the GMT API; NULL on failure (API->error says why). */
struct MB_GMT_TEXT *mb_gmt_text_begin(struct GMT_CTRL *GMT, struct GMT_OPTION *options);

/* The file `path`, written as the program writes it; NULL when it cannot be opened. */
struct MB_GMT_TEXT *mb_gmt_text_file(struct GMT_CTRL *GMT, const char *path);

/* The process's stdout, written as the program writes it (never closed): for a listing the program
   writes in BINARY (raw doubles with fwrite), which text records cannot carry. */
struct MB_GMT_TEXT *mb_gmt_text_stdout(struct GMT_CTRL *GMT);

/* printf to the writer. Records are emitted per complete line ('\n'). */
void mb_gmt_text_put(struct MB_GMT_TEXT *T, const char *format, ...);

/* Emit any unterminated last line, close the destination, free the writer; non-zero on failure. */
int mb_gmt_text_end(struct MB_GMT_TEXT *T);

#endif
