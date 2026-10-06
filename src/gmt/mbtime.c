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
 * GMT-module port of src/utilities/mbtime.cc: options from GMT's option list (the program's long
 * options are GMT long options through module_kw, its lower-case aliases are kept --
 * GMT_Parse_Common only parses the common options named in THIS_MODULE_OPTIONS), and the converted
 * time is written as a text record through the GMT API (mb_gmt_text.c).
 */

#define THIS_MODULE_NAME "mbtime"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Convert between epoch seconds and calendar time"
/* No data input; the converted time is written as a text record. */
#define THIS_MODULE_KEYS ">D}"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mb_define.h"
#include "mb_status.h"
#include "mb_gmt_text.h"

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

/* Translation table from the program's long options to the module's short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'T', "calendar-time", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'M', "epoch-time",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'v', "verbose",       "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

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
	GMT_Usage(API, 0, "usage: %s [-H] [-M<time_d>] [-T<year>/<month>/<day>/<hour>/<minute>/<second>] [%s]\n",
	          THIS_MODULE_NAME, GMT_V_OPT);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n\n", help_message);
	GMT_Message(API, GMT_TIME_NONE, "  OPTIONAL ARGUMENTS:\n");
	GMT_Usage(API, 1, "\n-H (--help)");
	GMT_Usage(API, -2, "Print this help.");
	GMT_Usage(API, 1, "\n-M<time_d> (--epoch-time=)");
	GMT_Usage(API, -2, "Epoch time in seconds since 1970/01/01 00:00:00.000000 [0].");
	GMT_Usage(API, 1, "\n-T<year>/<month>/<day>/<hour>/<minute>/<second> (--calendar-time=)");
	GMT_Usage(API, -2, "Calendar time.");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse_mbtime(struct GMT_CTRL *GMT, struct MBTIME_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt;
	struct GMTAPI_CTRL *API = GMT->parent;
	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		/* Every option keeps the program's lower-case alias: GMT_Parse_Common only touches the
		   common options named in THIS_MODULE_OPTIONS (-V), so these arrive here untouched. */
		case 'H': case 'h':
			Ctrl->H.active = true;
			break;
		case 'M': case 'm':
			if (sscanf(opt->arg, "%lf", &Ctrl->M.time_d) == 1) {
				Ctrl->M.active = true;
				Ctrl->mode = MBTIME_INPUT_EPOCH;
			}
			else {
				GMT_Report(API, GMT_MSG_ERROR, "Option -M: expected epoch seconds\n");
				n_errors++;
			}
			break;
		case 'T': case 't': {
			double seconds;
			if (sscanf(opt->arg, "%d/%d/%d/%d/%d/%lf", &Ctrl->T.time_i[0], &Ctrl->T.time_i[1],
			           &Ctrl->T.time_i[2], &Ctrl->T.time_i[3], &Ctrl->T.time_i[4], &seconds) == 6) {
				Ctrl->T.time_i[5] = (int)seconds;
				Ctrl->T.time_i[6] = (int)(1000000 * (seconds - Ctrl->T.time_i[5]));
				Ctrl->T.active = true;
				Ctrl->mode = MBTIME_INPUT_CALENDAR;
			}
			else {
				GMT_Report(API, GMT_MSG_ERROR, "Option -T: expected year/month/day/hour/minute/second\n");
				n_errors++;
			}
			break;
		}
		case 'v':	/* the program's -v: verbosity, as -V */
			GMT->current.setting.verbose = GMT_MSG_INFORMATION;
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}
	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code) { Free_mbtime_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbtime(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbtime(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MBTIME_CTRL *Ctrl = NULL;
	struct MB_GMT_TEXT *T = NULL;
	int error;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options at all is a valid run (mbtime converts epoch time 0), not a usage request */
	if ((error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = (struct MBTIME_CTRL *)New_mbtime_Ctrl(GMT);
	if ((error = parse_mbtime(GMT, Ctrl, options)) != GMT_NOERROR) Return(error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	const int verbose = (GMT->current.setting.verbose >= GMT_MSG_DEBUG) ? 2 : 0;
	time_mode_t mode_input = Ctrl->mode;
	int time_i[7] = {0};
	double time_d = 0.0;

	if (Ctrl->M.active)
		time_d = Ctrl->M.time_d;
	if (Ctrl->T.active)
		for (int i = 0; i < 7; i++)
			time_i[i] = Ctrl->T.time_i[i];

	GMT_Report(API, GMT_MSG_INFORMATION, "Program %s\n", program_name);
	GMT_Report(API, GMT_MSG_INFORMATION, "MB-system Version %s\n", MB_VERSION);
	GMT_Report(API, GMT_MSG_DEBUG, "mode: %d  time_i: %d/%d/%d/%d/%d/%d/%d  time_d: %f\n", mode_input,
	           time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6], time_d);

	if ((T = mb_gmt_text_begin(GMT, options)) == NULL)
		Return(API->error);

	/* convert to calendar time and output */
	if (mode_input == MBTIME_INPUT_EPOCH) {
		mb_get_date(verbose, time_d, time_i);
		mb_gmt_text_put(T,"%4.4d/%2.2d/%2.2d/%2.2d/%2.2d/%2.2d.%6.6d\n",
			time_i[0], time_i[1], time_i[2], time_i[3], time_i[4],
		        time_i[5], time_i[6]);
	} else {
		/* convert to epoch time and output */
		mb_get_time(verbose, time_i, &time_d);
		mb_gmt_text_put(T,"%f\n", time_d);
	}

	if (mb_gmt_text_end(T))
		Return(API->error);

	Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
