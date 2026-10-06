/*--------------------------------------------------------------------
 *    The MB-system:	mb_gmt_opts.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Option handling shared by the MB-System GMT modules.
 *
 * GMT completes an argument-less -B, -J, -R, -X, -Y or -p from its history
 * (GMT_Parse_Common -> gmtparse_complete_options) and fails when there is none
 * ("Found no history for option -Y"), whatever the module's own option list
 * says. An MB-System program option with one of those letters that takes no
 * argument (mbdatalist -Y "unlock", -p "processed", ...) never reaches the
 * module that way. mb_gmt_shorthand_guard(), called BEFORE GMT_Parse_Common,
 * renames each such option to its other-case twin, which the program accepts
 * as the very same option, so it arrives at the module's parse() unchanged in
 * meaning.
 */

#ifndef MB_GMT_OPTS_H
#define MB_GMT_OPTS_H

#include "gmt_dev.h"

/* `letters`: the module's argument-less options among B, J, R, X, Y, p (e.g. "Yp") */
void mb_gmt_shorthand_guard(struct GMT_OPTION *options, const char *letters);

/* A program whose long options outnumber the letters a module_kw table could give them
 * (mbmakeplatform has ~200 long options and no short ones). GMT leaves a --name[=value] it has no
 * keyword for as a "--" parameter option, which GMT_Parse_Common then hands to its own defaults
 * parser (--PAR=value), where the program's names are errors. mb_gmt_mark_long_options(), called
 * BEFORE GMT_Parse_Common, re-codes each --name[=value] whose name is one of the PROGRAM's as option
 * MB_GMT_LONGOPT (arg kept: "name[=value]"), so GMT's own --PAR=value settings still work, and the
 * module's parse() reads them in command-line order with mb_gmt_long_option(). */
#define MB_GMT_LONGOPT '\001'

struct MB_GMT_LONGOPT_DEF {
	const char *name;	/* NULL ends the table */
	bool has_arg;		/* the program's required_argument */
};

void mb_gmt_mark_long_options(struct GMTAPI_CTRL *API, struct GMT_OPTION **options, const struct MB_GMT_LONGOPT_DEF *table);

/* For an option marked by mb_gmt_mark_long_options(): its index in `table` and (in *value) its
 * argument, "" when it takes none. -1 when the name is not in the table, -2 when an option that
 * takes an argument has none (getopt_long's "requires an argument"). */
int mb_gmt_long_option(const struct GMT_OPTION *opt, const struct MB_GMT_LONGOPT_DEF *table, const char **value);

/* getopt also takes a short option's value as the next word ("-I dir"); GMT reads that word as a
 * separate input file. For each option letter in `letters` given with no value and followed by such
 * a word, join the word back on as its value. Call before GMT_Parse_Common. */
void mb_gmt_join_separated_values(struct GMTAPI_CTRL *API, struct GMT_OPTION **options, const char *letters);

/* The command line a module was given, as the argc/argv pair the programs hand to
 * mb_write_gmt_grd() (and the like) for a file's history: argv[0] the program name, argv[1] the
 * options as GMT has them. Returns argc; free with mb_gmt_history_free(). */
int mb_gmt_history_args(struct GMTAPI_CTRL *API, struct GMT_OPTION *options, const char *name, char ***argv);
void mb_gmt_history_free(struct GMTAPI_CTRL *API, int argc, char **argv);

/* mb_make_info_datalist() and mb_get_info_datalist() call exit() when they are handed a datalist
 * that cannot be opened ("Unable to open data list file"), which ends the whole process a module
 * runs in (a Julia, Python or MATLAB session) instead of the program. A module asks this first:
 * false, with the program's own message reported, when `file` (of MBIO format `format`; 0 to
 * resolve it, < 0 for a datalist) is a datalist that cannot be opened. */
bool mb_gmt_datalist_opens(struct GMTAPI_CTRL *API, int verbose, char *file, int format);

#endif
