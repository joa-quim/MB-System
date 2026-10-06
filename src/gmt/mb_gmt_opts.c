/*--------------------------------------------------------------------
 *    The MB-system:	mb_gmt_opts.c
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/* Option handling shared by the MB-System GMT modules; see mb_gmt_opts.h. */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "mb_gmt_opts.h"
#include "mb_define.h"
#include "mb_status.h"

/* GMT's shorthand options (gmt_common.h GMT_SHORTHAND_OPTIONS) */
static const char mb_gmt_shorthand[] = "BJRXYp";

/* index of the table entry named by "name[=value]", or -1 */
static int mb_gmt_long_index(const char *arg, const struct MB_GMT_LONGOPT_DEF *table) {
	const char *eq = strchr(arg, '=');
	const size_t len = eq ? (size_t)(eq - arg) : strlen(arg);
	for (int k = 0; table[k].name; k++)
		if (strlen(table[k].name) == len && strncmp(table[k].name, arg, len) == 0) return k;
	return -1;
}

void mb_gmt_mark_long_options(struct GMTAPI_CTRL *API, struct GMT_OPTION **options, const struct MB_GMT_LONGOPT_DEF *table) {
	for (struct GMT_OPTION *opt = *options; opt; opt = opt->next) {
		int k;
		if (opt->option != GMT_OPT_PARAMETER || opt->arg == NULL || (k = mb_gmt_long_index(opt->arg, table)) < 0) continue;
		opt->option = MB_GMT_LONGOPT;
		/* getopt_long's "--name value": GMT made the value a separate (file) option; join it back */
		struct GMT_OPTION *next = opt->next;
		if (table[k].has_arg && strchr(opt->arg, '=') == NULL && next && next->option == GMT_OPT_INFILE && next->arg) {
			char *joined = (char *)malloc(strlen(opt->arg) + strlen(next->arg) + 2);
			if (joined == NULL) continue;
			sprintf(joined, "%s=%s", opt->arg, next->arg);
			free(opt->arg);
			opt->arg = joined;
			GMT_Delete_Option(API, next, options);
		}
	}
}

int mb_gmt_long_option(const struct GMT_OPTION *opt, const struct MB_GMT_LONGOPT_DEF *table, const char **value) {
	const int k = mb_gmt_long_index(opt->arg, table);
	if (k < 0) return -1;
	const char *eq = strchr(opt->arg, '=');
	*value = eq ? eq + 1 : "";
	if (table[k].has_arg && (*value)[0] == '\0') return -2;
	return k;
}

void mb_gmt_join_separated_values(struct GMTAPI_CTRL *API, struct GMT_OPTION **options, const char *letters) {
	for (struct GMT_OPTION *opt = *options; opt; opt = opt->next) {
		if (!strchr(letters, opt->option) || (opt->arg && opt->arg[0])) continue;
		struct GMT_OPTION *next = opt->next;
		if (next == NULL || next->option != GMT_OPT_INFILE || next->arg == NULL) continue;
		char *value = strdup(next->arg);
		if (value == NULL) continue;
		free(opt->arg);
		opt->arg = value;
		GMT_Delete_Option(API, next, options);
	}
}

int mb_gmt_history_args(struct GMTAPI_CTRL *API, struct GMT_OPTION *options, const char *name, char ***argv) {
	char **av = (char **)calloc(3, sizeof (char *));
	if (av == NULL) { *argv = NULL; return 0; }
	char *cmd = GMT_Create_Cmd(API, options);
	av[0] = strdup(name);
	av[1] = strdup(cmd ? cmd : "");
	if (cmd) GMT_Destroy_Cmd(API, &cmd);
	*argv = av;
	return 2;
}

void mb_gmt_history_free(struct GMTAPI_CTRL *API, int argc, char **argv) {
	(void)API;
	if (argv == NULL) return;
	for (int i = 0; i < argc; i++) free(argv[i]);
	free(argv);
}

void mb_gmt_shorthand_guard(struct GMT_OPTION *options, const char *letters) {
	for (struct GMT_OPTION *opt = options; opt; opt = opt->next) {
		if (opt->arg && opt->arg[0]) continue;	/* an argument: GMT leaves it alone */
		if (!strchr(mb_gmt_shorthand, opt->option) || !strchr(letters, opt->option)) continue;
		opt->option = (char)(islower((unsigned char)opt->option) ? toupper((unsigned char)opt->option)
		                                                        : tolower((unsigned char)opt->option));
	}
}

bool mb_gmt_datalist_opens(struct GMTAPI_CTRL *API, int verbose, char *file, int format) {
	int error = MB_ERROR_NO_ERROR;
	void *datalist = NULL;
	if (format == 0) mb_get_format(verbose, file, NULL, &format, &error);
	if (format >= 0) return true;	/* a swath file: the functions open no datalist */
	if (mb_datalist_open(verbose, &datalist, file, MB_DATALIST_LOOK_UNSET, &error) != MB_SUCCESS) {
		GMT_Report(API, GMT_MSG_ERROR, "Unable to open data list file: %s\n", file);
		return false;
	}
	mb_datalist_close(verbose, &datalist, &error);
	return true;
}
