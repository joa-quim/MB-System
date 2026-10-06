/*--------------------------------------------------------------------
 *    The MB-system:	mbmapscale.c	6/5/2008
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
 * mbmapscale converts time values between epoch seconds (seconds since
 * 1970/01/01 00:00:00.000000) and calendar time (e.g. 2008/006/05/17/24/32/0).
 * The input time is set using the command line arguments -Mtime_d for
 * epoch seconds and -Tyear/month/day/hour/minute/second/microsecond for
 * calendar time. The output time (in the form not specified as input) is
 * written to stdout.
 *
 * Author:	D. W. Caress
 * Date:	June 5, 2008
 *
 * GMT-module port of src/utilities/mbmapscale.cc: options parsed in parse() from GMT's option
 * list, the program's long options kept through module_kw and its lower-case aliases kept; the
 * scaling printed through the GMT API (mb_gmt_text.c); every Return() a GMT error code.
 */

#define THIS_MODULE_NAME		"mbmapscale"
#define THIS_MODULE_LIB			"mbsystem"
#define THIS_MODULE_PURPOSE		"Output scaling between geographic coordinates and local meters east/north at given latitude"
#define THIS_MODULE_KEYS		">D}"
#define THIS_MODULE_NEEDS		""
#define THIS_MODULE_OPTIONS		"->V"

#include "gmt_dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_define.h"
#include "mb_status.h"
#include "mb_gmt_text.h"

typedef enum {
	MBMAPSCALE_MODE_WGS72   = 0,
	MBMAPSCALE_MODE_ALVINXY = 1
} mapscale_mode_t;

static const char program_name[] = "mbmapscale";
static const char help_message[] =
    "mbmapscale outputs the scaling between geographic coordinates (longitude and latitude)"
    "and local meters east and north at a user defined latitude. The map scale is\n"
    "written to stdout in the form of meters per degree longitude and latitude.";
static const char usage_message[] =
    "mbmapscale [-Llatitude -A -V -H]";

EXTERN_MSC int GMT_mbmapscale(void *API, int mode, void *args);

/* --- Control structure ---------------------------------------------- */

struct MBMAPSCALE_CTRL {
	int verbose;	/* the program's -V/-v count */
	struct mms_A { bool active; } A;                       /* AlvinXY mode */
	struct mms_H { bool active; } H;
	struct mms_L { bool active; double latitude; } L;
};

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'v', "verbose",  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",     "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'A', "alvinxy",  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'L', "latitude", "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static void *New_mbmapscale_Ctrl(struct GMT_CTRL *GMT) {
	struct MBMAPSCALE_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBMAPSCALE_CTRL);
	return Ctrl;
}

static void Free_mbmapscale_Ctrl(struct GMT_CTRL *GMT, struct MBMAPSCALE_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "\n%s\n\n", help_message);
	GMT_Message(API, GMT_TIME_NONE,
		"\tEvery option also has the program's lower-case and long forms (--latitude, --alvinxy,\n"
		"\t--verbose, --help).\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse(struct GMT_CTRL *GMT, struct MBMAPSCALE_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (struct GMT_OPTION *opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case 'V':
		case 'v':
			Ctrl->verbose++;
			break;
		case 'H':
		case 'h':
			Ctrl->H.active = true;
			break;
		case 'A':
		case 'a':
			Ctrl->A.active = true;
			break;
		case 'L':
		case 'l':
			if (sscanf(opt->arg, "%lf", &Ctrl->L.latitude) > 0)
				Ctrl->L.active = true;
			else {
				GMT_Report(API, GMT_MSG_ERROR, "Syntax error -%c option: expected latitude\n", opt->option);
				n_errors++;
			}
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}

	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code)  { gmt_M_free_options(mode); return code; }
#define Return(code)   { if (T) mb_gmt_text_end(T); Free_mbmapscale_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }

/*--------------------------------------------------------------------*/
int GMT_mbmapscale(void *V_API, int mode, void *args) {
	struct MBMAPSCALE_CTRL *Ctrl = NULL;
	struct MB_GMT_TEXT     *T = NULL;
	struct GMT_CTRL        *GMT  = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION      *options = NULL;
	struct GMTAPI_CTRL     *API = gmt_get_api_ptr(V_API);
	int gmt_error;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program (the scaling at the equator) */
	if ((gmt_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(gmt_error);

	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	Ctrl = New_mbmapscale_Ctrl(GMT);
	if ((gmt_error = parse(GMT, Ctrl, options)) != GMT_NOERROR) Return(gmt_error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	const int verbose = Ctrl->verbose;
	mapscale_mode_t smode = Ctrl->A.active ? MBMAPSCALE_MODE_ALVINXY : MBMAPSCALE_MODE_WGS72;
	double latitude = Ctrl->L.latitude;

	if (verbose == 1) {
		fprintf(stderr, "\nProgram %s\n", program_name);
		fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
	}

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
		fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
		fprintf(stderr, "dbg2  Control Parameters:\n");
		fprintf(stderr, "dbg2       verbose:    %d\n", verbose);
		fprintf(stderr, "dbg2       latitude:   %f\n", latitude);
		fprintf(stderr, "dbg2       mode:       %d\n", smode);
	}

	/* calculate mtodeglon and mtodeglat */
	double mtodeglon, mtodeglat;
	int status;
	if (smode == MBMAPSCALE_MODE_WGS72) {
		status = mb_coor_scale(verbose, latitude, &mtodeglon, &mtodeglat);
	}
	else {
		status = mb_alvinxy_scale(verbose, latitude, &mtodeglon, &mtodeglat);
	}

	/* the result: what the program prints on stdout, through the GMT API */
	if ((T = mb_gmt_text_begin(GMT, options)) == NULL) Return(API->error);
	mb_gmt_text_put(T, "\nLocal scaling between degrees longitude and latitude and meters east and north:\n");
	if (smode == MBMAPSCALE_MODE_WGS72) {
		mb_gmt_text_put(T, "\tUsing WGS72 ellipsoid\n");
	}
	else {
		mb_gmt_text_put(T, "\tUsing 1866 Clark Spheroid as per AlvinXY coordinates\n");
	}
	mb_gmt_text_put(T, "\tMeters per degree longitude: %.3f\n", 1.0/mtodeglon);
	mb_gmt_text_put(T, "\tMeters per degree latitude:  %.3f\n", 1.0/mtodeglat);
	mb_gmt_text_put(T, "\tMeters to degree longitude:  %.9f\n", mtodeglon);
	mb_gmt_text_put(T, "\tMeters to degree latitude:   %.9f\n", mtodeglat);
	const int output_failed = mb_gmt_text_end(T);
	T = NULL;
	if (output_failed) Return(GMT_RUNTIME_ERROR);

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
		fprintf(stderr, "dbg2  Ending status:\n");
		fprintf(stderr, "dbg2       status:  %d\n", status);
	}

	Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
