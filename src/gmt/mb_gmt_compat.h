/*--------------------------------------------------------------------
 *    The MB-system:	mb_gmt_compat.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Build the MB-System GMT modules against GMT 6.3 / 6.4.
 *
 * The modules are written for GMT 6.5. An older GMT (Ubuntu 22.04's apt GMT is 6.3) lacks:
 *   - GMT_KEYWORD_DICTIONARY's last field, transproc_mask, and its GMT_TP_* flags (6.5). Every
 *     keyword row ends with GMT_TP_STANDARD, defined EMPTY here: the row's last initializer is then
 *     a trailing comma -- legal C -- and the row fits the older 7-field struct.
 *   - gmt_default_option_error (6.4) and gmt_get_required_string / gmt_get_required_double (6.4):
 *     reproduced below with GMT 6.5's behaviour, under mb_ names and reached through macros, so a
 *     GMT that already declares them (6.4) never sees a second declaration.
 * The older struct's long_option holds 30 characters (6.5: 254); MB_GMT_COMPAT_PRE65 lets a row
 * that needs more give a shorter form there.
 *
 * Include it right after gmt_dev.h. With GMT >= 6.5 it defines nothing.
 */

#ifndef MB_GMT_COMPAT_H
#define MB_GMT_COMPAT_H

#ifndef GMT_TP_STANDARD

#define MB_GMT_COMPAT_PRE65 1
#define GMT_TP_STANDARD

/* GMT 6.5 gmt_default_option_error(): gmt_default_error() plus a hint for a stray input file */
static inline int mb_gmt_default_option_error(struct GMT_CTRL *GMT, struct GMT_OPTION *opt) {
	int error = gmt_default_error(GMT, opt->option);
	if (error && opt->option == GMT_OPT_INFILE) {
		if (opt->arg[0] && strchr(opt->arg, '+'))
			GMT_Report(GMT->parent, GMT_MSG_ERROR, "%s was seen as an input file but looks like an option with modifiers; did you forget a leading hyphen?\n", opt->arg);
		else
			GMT_Report(GMT->parent, GMT_MSG_ERROR, "%s was seen as an input file which is not expected by this module\n", opt->arg);
	}
	return error;
}

/* GMT 6.5 gmtsupport_print_and_err(): fuss when a required argument is missing */
static inline unsigned int mb_gmt_required_err(struct GMT_CTRL *GMT, char *text, char option, char modifier) {
	if (text && text[0]) return GMT_NOERROR;
	if (modifier)
		GMT_Report(GMT->parent, GMT_MSG_ERROR, "Option -%c: No argument provided for modifier +%c\n", option, modifier);
	else
		GMT_Report(GMT->parent, GMT_MSG_ERROR, "Option -%c: No argument provided\n", option);
	return GMT_PARSE_ERROR;
}

static inline unsigned int mb_gmt_get_required_string(struct GMT_CTRL *GMT, char *text, char option, char modifier, char **string) {
	unsigned int err;
	if (!(err = mb_gmt_required_err(GMT, text, option, modifier)))
		*string = strdup(text);
	return err;
}

static inline unsigned int mb_gmt_get_required_double(struct GMT_CTRL *GMT, char *text, char option, char modifier, double *value) {
	unsigned int err;
	if (!(err = mb_gmt_required_err(GMT, text, option, modifier)))
		*value = atof(text);
	return err;
}

#define gmt_default_option_error(GMT, opt) mb_gmt_default_option_error(GMT, opt)
#define gmt_get_required_string(GMT, text, option, modifier, string) mb_gmt_get_required_string(GMT, text, option, modifier, string)
#define gmt_get_required_double(GMT, text, option, modifier, value) mb_gmt_get_required_double(GMT, text, option, modifier, value)

#endif /* GMT_TP_STANDARD */

#endif /* MB_GMT_COMPAT_H */
