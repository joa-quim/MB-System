/*--------------------------------------------------------------------
 *    The MB-system:	mbtime.c	6/5/2008
 *
 *    Copyright (c) 2008-2025 by
 *    David W. Caress (caress@mbari.org)
 *      Monterey Bay Aquarium Research Institute
 *      Moss Landing, California, USA
 *    Dale N. Chayes 
 *      Center for Coastal and Ocean Mapping
 *      University of New Hampshire
 *      Durham, New Hampshire, USA
 *    Christian dos Santos Ferreira
 *      MARUM
 *      University of Bremen
 *      Bremen Germany
 *     
 *    MB-System was created by Caress and Chayes in 1992 at the
 *      Lamont-Doherty Earth Observatory
 *      Columbia University
 *      Palisades, NY 10964
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * MBTIME converts time values between epoch seconds (seconds since
 * 1970/01/01 00:00:00.000000) and calendar time (e.g. 2008/006/05/17/24/32/0).
 * The input time is set using the command line arguments -Mtime_d for
 * epoch seconds and -Tyear/month/day/hour/minute/second/microsecond for
 * calendar time. The output time (in the form not specified as input) is
 * written to stdout.
 *
 * Author:	D. W. Caress
 * Date:	June 5, 2008
 *
 * GMT-module port of src/utilities/mbtime.cc: the getopt_long loop is
 * replaced by the GMT option parser (long options are rewritten onto
 * their short forms before GMT sees them) and main() becomes
 * GMT_mbtime(), with every exit() turned into Return().
 */

#define THIS_MODULE_NAME "mbtime"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Convert between epoch seconds and calendar time"
/* No data input; the converted time is written to stdout. */
#define THIS_MODULE_KEYS ">D}"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "unistd_w.h"
#else
#include <unistd.h>
#endif
#include "mb_define.h"
#include "mb_status.h"

typedef enum {
    MBTIME_INPUT_EPOCH = 0,
    MBTIME_INPUT_CALENDAR  = 1
} time_mode_t;

static const char program_name[] = "MBTIME";
static const char help_message[] =
    "MBTIME converts time values between epoch seconds (seconds since\n"
    "1970/01/01 00:00:00.000000) and calendar time (e.g. 2008/006/05/17/24/32/0).\n"
    "The input time is set using the command line arguments -Mtime_d for\n"
    "epoch seconds and -Tyear/month/day/hour/minute/second/microsecond for\n"
    "calendar time. The output time (in the form not specified as input) is\n"
    "written to stdout.";
static const char usage_message[] =
    "mbtime [-Mtime_d -Tyear/month/day/hour/minute/second -V -H]\n"
    "\t--calendar-time=year/month/day/hour/minute/second {-Tyear/month/day/hour/minute/second}\n"
    "\t--epoch-time=time_d {-Mtime_d}\n"
    "\t--help {-H}\n"
    "\t--verbose {-V}\n";

/* --- Control structure ---------------------------------------------- */

struct MBTIME_CTRL {
	struct mbtm_H { bool active; } H;
	struct mbtm_M { bool active; double time_d; } M;
	struct mbtm_T { bool active; int time_i[7]; } T;
	/* -M and -T both set the input mode; as with getopt the last one wins */
	time_mode_t mode;
};

static void *New_mbtime_Ctrl(struct GMT_CTRL *GMT) {
	struct MBTIME_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBTIME_CTRL);
	Ctrl->mode = MBTIME_INPUT_EPOCH;
	return Ctrl;
}

static void Free_mbtime_Ctrl(struct GMT_CTRL *GMT, struct MBTIME_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_PARSE_ERROR;
	GMT_Message(API, GMT_TIME_NONE, "%s\n\n", help_message);
	GMT_Message(API, GMT_TIME_NONE,
	            "\t-M Epoch time in seconds since 1970/01/01 00:00:00.000000.\n"
	            "\t-T Calendar time as year/month/day/hour/minute/second\n"
	            "\t   (use --calendar-time=... through the gmt executable, which takes -T itself).\n"
	            "\t-H Print the help message and exit.\n");
	GMT_Option(API, "V");
	return GMT_PARSE_ERROR;
}

static int parse_mbtime(struct GMT_CTRL *GMT, struct MBTIME_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt;
	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case 'H': case 'h':
			Ctrl->H.active = true;
			break;
		case 'M': case 'm':
			if (opt->arg && sscanf(opt->arg, "%lf", &Ctrl->M.time_d) == 1) {
				Ctrl->M.active = true;
				Ctrl->mode = MBTIME_INPUT_EPOCH;
			}
			else n_errors++;
			break;
		case 'T': case 't': case 'Z': {	/* -T arrives as -Z, see preparse_long_options() */
			double seconds;
			if (opt->arg && sscanf(opt->arg, "%d/%d/%d/%d/%d/%lf", &Ctrl->T.time_i[0], &Ctrl->T.time_i[1],
			                       &Ctrl->T.time_i[2], &Ctrl->T.time_i[3], &Ctrl->T.time_i[4], &seconds) == 6) {
				Ctrl->T.time_i[5] = (int)seconds;
				Ctrl->T.time_i[6] = (int)(1000000 * (seconds - Ctrl->T.time_i[5]));
				Ctrl->T.active = true;
				Ctrl->mode = MBTIME_INPUT_CALENDAR;
			}
			else n_errors++;
			break;
		}
		default:
			n_errors += gmt_default_error(GMT, opt->option);
			break;
		}
	}
	return n_errors ? GMT_PARSE_ERROR : GMT_OK;
}

static char *join_args(int mode, void *args) {
	char **argv = (char **)args, *joined;
	size_t total = 1;
	int i;
	if (mode <= 0 || !args) return NULL;
	for (i = 0; i < mode; i++) total += strlen(argv[i]) + 1;
	joined = (char *)calloc(total, 1);
	if (!joined) return NULL;
	for (i = 0; i < mode; i++) { if (i) strcat(joined, " "); strcat(joined, argv[i]); }
	return joined;
}

/* Rewrite the getopt_long options of mbtime.cc onto short options.
 * GMT reserves -T, so the calendar time is carried as -Z; the rewrite
 * happens before GMT_Create_Options() splits the slash-delimited value. */
static char *preparse_long_options(bool *help, const char *args) {
	size_t length = args ? strlen(args) : 0, out = 0;
	char *copy = (char *)calloc(length + 2, 1), *result = (char *)calloc(2 * length + 8, 1);
	char *token, *saveptr = NULL, pending = '\0';
	if (!copy || !result) { free(copy); free(result); return NULL; }
	memcpy(copy, args, length);
	for (token = strtok_r(copy, " \t", &saveptr); token; token = strtok_r(NULL, " \t", &saveptr)) {
		char emit = '\0', *equals;
		const char *value = NULL;
		if (pending) {
			if (out) result[out++] = ' ';
			result[out++] = '-'; result[out++] = pending;
			memcpy(result + out, token, strlen(token)); out += strlen(token); pending = '\0'; continue;
		}
		if (strncmp(token, "--", 2) != 0) {
			if (token[0] == '-' && (token[1] == 'T' || token[1] == 't')) token[1] = 'Z';
			if (out) result[out++] = ' ';
			memcpy(result + out, token, strlen(token)); out += strlen(token); continue;
		}
		equals = strchr(token + 2, '=');
		if (equals) { *equals = '\0'; value = equals + 1; }
		if (!strcmp(token + 2, "help")) { *help = true; continue; }
		if (!strcmp(token + 2, "verbose")) emit = 'V';
		else if (!strcmp(token + 2, "epoch-time")) emit = 'M';
		else if (!strcmp(token + 2, "calendar-time")) emit = 'Z';
		if (!emit) {
			if (equals) *equals = '=';
			if (out) result[out++] = ' ';
			memcpy(result + out, token, strlen(token)); out += strlen(token);
		} else if (emit == 'V') {
			if (out) result[out++] = ' ';
			result[out++] = '-'; result[out++] = emit;
		} else if (value) {
			if (out) result[out++] = ' ';
			result[out++] = '-'; result[out++] = emit;
			memcpy(result + out, value, strlen(value)); out += strlen(value);
		} else pending = emit;
	}
	free(copy);
	return result;
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code) { free(remaining_args); Free_mbtime_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbtime(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbtime(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MBTIME_CTRL *Ctrl = NULL;
	char *remaining_args = NULL;
	bool staged_help = false;
	int parse_error;

	if (!API) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	{
		char *joined = join_args(mode, args);
		const char *text = joined ? joined : (mode == GMT_MODULE_CMD ? (const char *)args : NULL);
		if (text) remaining_args = preparse_long_options(&staged_help, text);
		free(joined);
	}
	options = GMT_Create_Options(API, remaining_args ? GMT_MODULE_CMD : mode, remaining_args ? (void *)remaining_args : args);
	if (API->error) { free(remaining_args); return API->error; }
	/* no arguments is a valid run: mbtime converts epoch time 0 */
	if (options && options->option == GMT_OPT_USAGE) { free(remaining_args); bailout(usage(API, GMT_USAGE)); }
	if (options && options->option == GMT_OPT_SYNOPSIS) { free(remaining_args); bailout(usage(API, GMT_SYNOPSIS)); }
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, NULL, &options, &GMT_cpy)) == NULL) { free(remaining_args); bailout(API->error); }
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = (struct MBTIME_CTRL *)New_mbtime_Ctrl(GMT);
	Ctrl->H.active = staged_help;
	if ((parse_error = parse_mbtime(GMT, Ctrl, options)) != GMT_OK) Return(parse_error);

	int verbose = GMT->common.V.active;
	time_mode_t mode_input = MBTIME_INPUT_EPOCH;
	int time_i[7] = {0};
	double time_d = 0.0;

	/* process argument list */
	{
		bool help = Ctrl->H.active;

		if (Ctrl->M.active)
			time_d = Ctrl->M.time_d;
		if (Ctrl->T.active)
			for (int i = 0; i < 7; i++)
				time_i[i] = Ctrl->T.time_i[i];
		mode_input = Ctrl->mode;

		if (verbose == 1 || help) {
			fprintf(stdout, "\nProgram %s\n", program_name);
			fprintf(stdout, "MB-system Version %s\n", MB_VERSION);
		}

		if (verbose >= 2) {
			fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
			fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(stderr, "dbg2  Control Parameters:\n");
			fprintf(stderr, "dbg2       verbose:    %d\n", verbose);
			fprintf(stderr, "dbg2       help:       %d\n", help);
			fprintf(stderr, "dbg2       mode:       %d\n", mode_input);
			fprintf(stderr, "dbg2       time_i[0]:  %d\n", time_i[0]);
			fprintf(stderr, "dbg2       time_i[1]:  %d\n", time_i[1]);
			fprintf(stderr, "dbg2       time_i[2]:  %d\n", time_i[2]);
			fprintf(stderr, "dbg2       time_i[3]:  %d\n", time_i[3]);
			fprintf(stderr, "dbg2       time_i[4]:  %d\n", time_i[4]);
			fprintf(stderr, "dbg2       time_i[5]:  %d\n", time_i[5]);
			fprintf(stderr, "dbg2       time_i[6]:  %d\n", time_i[6]);
			fprintf(stderr, "dbg2       time_d:     %f\n", time_d);
		}

		if (help) {
			fprintf(stderr, "\n%s\n", help_message);
			fprintf(stderr, "\nusage: %s\n", usage_message);
			Return(MB_ERROR_NO_ERROR);
		}

	}


	/* convert to calendar time and output */
	if (mode_input == MBTIME_INPUT_EPOCH) {
		mb_get_date(verbose, time_d, time_i);
		fprintf(stdout, "%4.4d/%2.2d/%2.2d/%2.2d/%2.2d/%2.2d.%6.6d\n",
			time_i[0], time_i[1], time_i[2], time_i[3], time_i[4],
		        time_i[5], time_i[6]);
	} else {
		/* convert to epoch time and output */
		mb_get_time(verbose, time_i, &time_d);
		fprintf(stdout, "%f\n", time_d);
	}

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
		fprintf(stderr, "dbg2  Ending status:\n");
		fprintf(stderr, "dbg2       status:  %d\n", MB_SUCCESS);
	}

	Return(MB_ERROR_NO_ERROR);
}
/*--------------------------------------------------------------------*/
