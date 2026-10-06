/*--------------------------------------------------------------------
 *    The MB-system:	mbsegyinfo.c	6/2/2004
 *
 *    Copyright (c) 2004-2025 by
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
 * MBsegyinfo reads a segy data file and outputs some basic statistics.
 *
 * Author:	D. W. Caress
 * Date:	June 2, 2004
 *
 * GMT-module port of src/utilities/mbsegyinfo.cc: options parsed in parse() from GMT's option
 * list, the program's long options kept through module_kw and its lower-case aliases kept; the
 * report printed through the GMT API (mb_gmt_text.c), or to <file>.sinf with -O, as the program
 * does; every Return() a GMT error code.
 */

#define THIS_MODULE_NAME		"mbsegyinfo"
#define THIS_MODULE_LIB			"mbsystem"
#define THIS_MODULE_PURPOSE		"List basic statistics (file/trace headers, navigation, time, ranges) from a SEGY data file"
#define THIS_MODULE_KEYS		">D}"
#define THIS_MODULE_NEEDS		""
#define THIS_MODULE_OPTIONS		"->V"

#include "gmt_dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_define.h"
#include "mb_format.h"
#include "mb_segy.h"
#include "mb_status.h"
#include "mb_gmt_text.h"

static const char program_name[] = "MBsegyinfo";
static const char help_message[] =
    "MBsegyinfo lists table data from a segy data file.";
static const char usage_message[] =
    "MBsegyinfo -Ifile [-Llonflip -O -H -V]";

EXTERN_MSC int GMT_mbsegyinfo(void *API, int mode, void *args);

/* --- Control structure ---------------------------------------------- */

struct MBSEGYINFO_CTRL {
	int verbose;	/* the program's -V/-v count */
	struct mbsi_H { bool active; } H;
	struct mbsi_I { bool active; char file[MB_PATH_MAXLINE]; } I;
	struct mbsi_L { bool active; int lonflip; } L;
	struct mbsi_O { bool active; } O;
};

static void *New_mbsegyinfo_Ctrl(struct GMT_CTRL *GMT) {
	struct MBSEGYINFO_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBSEGYINFO_CTRL);
	return Ctrl;
}

static void Free_mbsegyinfo_Ctrl(struct GMT_CTRL *GMT, struct MBSEGYINFO_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'H', "help",             "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",            "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'L', "longitude-domain", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "output-file",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'v', "verbose",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "\n%s\n\n", help_message);
	GMT_Message(API, GMT_TIME_NONE,
		"\t-I Input SEGY data file.\n"
		"\t-L Longitude flip control (-1=use 0..-360, 0=use -180..180, 1=use 0..360).\n"
		"\t-O Write info to <file>.sinf instead of stdout.\n"
		"\t-H Print description and exit.\n"
		"\tEvery option also has the program's lower-case and long forms (--input, --longitude-domain,\n"
		"\t--output-file, --help, --verbose).\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse(struct GMT_CTRL *GMT, struct MBSEGYINFO_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case 'V':
		case 'v':
			Ctrl->verbose++;
			break;
		case 'H':
		case 'h':
			Ctrl->H.active = true;
			break;
		case 'I':
		case 'i':
			if (opt->arg && opt->arg[0]) {
				strncpy(Ctrl->I.file, opt->arg, MB_PATH_MAXLINE - 1);
				Ctrl->I.file[MB_PATH_MAXLINE - 1] = '\0';
				Ctrl->I.active = true;
			} else { GMT_Report(API, GMT_MSG_ERROR, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'L':
		case 'l':
			if (opt->arg && opt->arg[0] && sscanf(opt->arg, "%d", &Ctrl->L.lonflip) == 1)
				Ctrl->L.active = true;
			else {
				GMT_Report(API, GMT_MSG_ERROR, "Syntax error -%c option: expected integer lonflip\n", opt->option);
				n_errors++;
			}
			break;
		case 'O':
		case 'o':
			Ctrl->O.active = true;
			break;
		case 'W':	/* in the program's option string, without any effect there either */
		case 'w':
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}

	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code)  { gmt_M_free_options(mode); return code; }
#define Return(code)   { if (T) mb_gmt_text_end(T); Free_mbsegyinfo_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }

/*--------------------------------------------------------------------*/
int GMT_mbsegyinfo(void *V_API, int mode, void *args) {
	struct MBSEGYINFO_CTRL *Ctrl = NULL;
	struct MB_GMT_TEXT     *T = NULL;	/* the report: GMT API records, or the -O .sinf file */
	struct GMT_CTRL        *GMT  = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION      *options = NULL;
	struct GMTAPI_CTRL     *API = gmt_get_api_ptr(V_API);
	int gmt_error;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program, which reports the missing input itself */
	if ((gmt_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(gmt_error);

	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	Ctrl = New_mbsegyinfo_Ctrl(GMT);
	if ((gmt_error = parse(GMT, Ctrl, options)) != GMT_NOERROR) Return(gmt_error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	const int verbose = Ctrl->verbose;

	int format;
	int pings;
	int lonflip;
	double bounds[4];
	int btime_i[7];
	int etime_i[7];
	double speedmin;
	double timegap;

	int status = mb_defaults(verbose, &format, &pings, &lonflip, bounds, btime_i, etime_i, &speedmin, &timegap);

	char read_file[MB_PATH_MAXLINE] = "";
	if (Ctrl->I.active) {
		strncpy(read_file, Ctrl->I.file, MB_PATH_MAXLINE - 1);
		read_file[MB_PATH_MAXLINE - 1] = '\0';
	}
	if (Ctrl->L.active) lonflip = Ctrl->L.lonflip;
	const bool output_usefile = Ctrl->O.active;

	if (verbose == 1) {
		fprintf(stderr, "\nProgram %s\n", program_name);
		fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
	}

	if (verbose >= 2) {
		GMT_Report(API, GMT_MSG_NORMAL, "\ndbg2  Program <%s>\n", program_name);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2  MB-system Version %s\n", MB_VERSION);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2  Control Parameters:\n");
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       verbose:        %d\n", verbose);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       lonflip:        %d\n", lonflip);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       bounds[0]:      %f\n", bounds[0]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       bounds[1]:      %f\n", bounds[1]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       bounds[2]:      %f\n", bounds[2]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       bounds[3]:      %f\n", bounds[3]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       btime_i[0]:     %d\n", btime_i[0]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       btime_i[1]:     %d\n", btime_i[1]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       btime_i[2]:     %d\n", btime_i[2]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       btime_i[3]:     %d\n", btime_i[3]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       btime_i[4]:     %d\n", btime_i[4]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       btime_i[5]:     %d\n", btime_i[5]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       btime_i[6]:     %d\n", btime_i[6]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       etime_i[0]:     %d\n", etime_i[0]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       etime_i[1]:     %d\n", etime_i[1]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       etime_i[2]:     %d\n", etime_i[2]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       etime_i[3]:     %d\n", etime_i[3]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       etime_i[4]:     %d\n", etime_i[4]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       etime_i[5]:     %d\n", etime_i[5]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       etime_i[6]:     %d\n", etime_i[6]);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       speedmin:       %f\n", speedmin);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       timegap:        %f\n", timegap);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       read_file:      %s\n", read_file);
	}

	int error = MB_ERROR_NO_ERROR;

	/* initialize reading the segy file */
	void *mbsegyioptr;
	struct mb_segyfileheader_struct fileheader;
	{
		struct mb_segyasciiheader_struct asciiheader;
		if (mb_segy_read_init(verbose, read_file, &mbsegyioptr, &asciiheader, &fileheader, &error) != MB_SUCCESS) {
			char *message;
			mb_error(verbose, error, &message);
			GMT_Report(API, GMT_MSG_ERROR, "MBIO Error returned from function <mb_segy_read_init>: %s\n", message);
			GMT_Report(API, GMT_MSG_ERROR, "SEGY File <%s> not initialized for reading\n", read_file);
			Return(GMT_RUNTIME_ERROR);
		}
	}

	/* the report: to <file>.sinf with -O, as the program writes it, else through the GMT API */
	if (output_usefile) {
		char output_file[MB_PATH_MAXLINE + 5];
		snprintf(output_file, sizeof(output_file), "%s.sinf", read_file);
		T = mb_gmt_text_file(GMT, output_file);
	}
	if (T == NULL && (T = mb_gmt_text_begin(GMT, options)) == NULL)
		Return(API->error);

	double delaymin = 0.0;
	double delaymax = 0.0;

	double lonmin = 0.0;
	double lonmax = 0.0;
	double latmin = 0.0;
	double latmax = 0.0;
	double lonbeg = 0.0;
	double latbeg = 0.0;
	double lonend = 0.0;
	double latend = 0.0;

	double rangemin = 0.0;
	double rangemax = 0.0;

	double receiverelevationmin = 0.0;
	double receiverelevationmax = 0.0;
	double receiverwaterdepthmin = 0.0;
	double receiverwaterdepthmax = 0.0;

	int rpmin = 0;
	int rpmax = 0;
	int rptracemin = 0;
	int rptracemax = 0;

	int shotmin = 0;
	int shotmax = 0;
	int shottracemin = 0;
	int shottracemax = 0;

	double sourcedepthmin = 0.0;
	double sourcedepthmax = 0.0;
	double sourceelevationmin = 0.0;
	double sourceelevationmax = 0.0;
	double sourcewaterdepthmin = 0.0;
	double sourcewaterdepthmax = 0.0;

	int timbeg_i[7];
	int timend_i[7];
	int timbeg_j[5];
	int timend_j[5];

	int nread = 0;
	bool first = true;
	while (error <= MB_ERROR_NO_ERROR) {
		error = MB_ERROR_NO_ERROR;

		/* read a trace */
		struct mb_segytraceheader_struct traceheader;
		float *trace;
		status = mb_segy_read_trace(verbose, mbsegyioptr, &traceheader, &trace, &error);

		/* deal with success */
		if (status == MB_SUCCESS) {
			nread++;

			/* get needed values */
			int time_j[5];
			time_j[0] = traceheader.year;
			time_j[1] = traceheader.day_of_yr;
			time_j[2] = traceheader.min + 60 * traceheader.hour;
			time_j[3] = traceheader.sec;
			time_j[4] = 1000 * traceheader.mils;
			int time_i[7];
			mb_get_itime(verbose, time_j, time_i);
			double time_d;
			mb_get_time(verbose, time_i, &time_d);
			double factor;
			if (traceheader.coord_scalar < 0)
				factor = 1.0 / ((float)(-traceheader.coord_scalar)) / 3600.0;
			else
				factor = (float)traceheader.coord_scalar / 3600.0;
			double navlon;
			if (traceheader.src_long != 0)
				navlon = factor * ((float)traceheader.src_long);
			else
				navlon = factor * ((float)traceheader.grp_long);
			double navlat;
			if (traceheader.src_lat != 0)
				navlat = factor * ((float)traceheader.src_lat);
			else
				navlat = factor * ((float)traceheader.grp_lat);
			if (lonflip < 0) {
				if (navlon > 0.)
					navlon = navlon - 360.;
				else if (navlon < -360.)
					navlon = navlon + 360.;
			}
			else if (lonflip == 0) {
				if (navlon > 180.)
					navlon = navlon - 360.;
				else if (navlon < -180.)
					navlon = navlon + 360.;
			}
			else {
				if (navlon > 360.)
					navlon = navlon - 360.;
				else if (navlon < 0.)
					navlon = navlon + 360.;
			}
			if (traceheader.elev_scalar < 0)
				factor = 1.0 / ((float)(-traceheader.elev_scalar));
			else
				factor = (float)traceheader.elev_scalar;
			const double range = (double)traceheader.range;
			const double receiverelevation = factor * ((double)traceheader.grp_elev);
			const double sourceelevation = factor * ((double)traceheader.src_elev);
			const double sourcedepth = factor * ((double)traceheader.src_depth);
			const double sourcewaterdepth = factor * ((double)traceheader.src_wbd);
			const double receiverwaterdepth = factor * ((double)traceheader.grp_wbd);
			const double delay = 0.001 * ((double)traceheader.delay_mils);

			/* get initial values */
			if (first) {
				first = false;
				shotmin = traceheader.shot_num;
				shotmax = traceheader.shot_num;
				shottracemin = traceheader.shot_tr;
				shottracemax = traceheader.shot_tr;
				rpmin = traceheader.rp_num;
				rpmax = traceheader.rp_num;
				rptracemin = traceheader.rp_tr;
				rptracemax = traceheader.rp_tr;
				delaymin = delay;
				delaymax = delay;
				lonmin = navlon;
				lonmax = navlon;
				latmin = navlat;
				latmax = navlat;
				rangemin = range;
				rangemax = range;
				receiverelevationmin = receiverelevation;
				receiverelevationmax = receiverelevation;
				sourceelevationmin = sourceelevation;
				sourceelevationmax = sourceelevation;
				sourcedepthmin = sourcedepth;
				sourcedepthmax = sourcedepth;
				sourcewaterdepthmin = sourcewaterdepth;
				sourcewaterdepthmax = sourcewaterdepth;
				receiverwaterdepthmin = receiverwaterdepth;
				receiverwaterdepthmax = receiverwaterdepth;

				lonbeg = navlon;
				latbeg = navlat;
				lonend = navlon;
				latend = navlat;
				for (int i = 0; i < 7; i++) {
					timbeg_i[i] = time_i[i];
					timend_i[i] = time_i[i];
				}
				for (int i = 0; i < 5; i++) {
					timbeg_j[i] = time_j[i];
					timend_j[i] = time_j[i];
				}
			}

			/* get min max values */
			else {
				shotmin = MIN(shotmin, traceheader.shot_num);
				shotmax = MAX(shotmax, traceheader.shot_num);
				shottracemin = MIN(shottracemin, traceheader.shot_tr);
				shottracemax = MAX(shottracemax, traceheader.shot_tr);
				rpmin = MIN(rpmin, traceheader.rp_num);
				rpmax = MAX(rpmax, traceheader.rp_num);
				rptracemin = MIN(rptracemin, traceheader.rp_tr);
				rptracemax = MAX(rptracemax, traceheader.rp_tr);
				delaymin = MIN(delaymin, delay);
				delaymax = MAX(delaymax, delay);
				if (navlon != 0.0 && navlat != 0.0) {
					lonmin = MIN(lonmin, navlon);
					lonmax = MAX(lonmax, navlon);
					latmin = MIN(latmin, navlat);
					latmax = MAX(latmax, navlat);
				}
				lonend = navlon;
				latend = navlat;
				for (int i = 0; i < 7; i++) {
					timend_i[i] = time_i[i];
				}
				for (int i = 0; i < 5; i++) {
					timend_j[i] = time_j[i];
				}
				rangemin = MIN(rangemin, range);
				rangemax = MAX(rangemax, range);
				receiverelevationmin = MIN(receiverelevationmin, receiverelevation);
				receiverelevationmax = MAX(receiverelevationmax, receiverelevation);
				sourceelevationmin = MIN(sourceelevationmin, sourceelevation);
				sourceelevationmax = MAX(sourceelevationmax, sourceelevation);
				sourcedepthmin = MIN(sourcedepthmin, sourcedepth);
				sourcedepthmax = MAX(sourcedepthmax, sourcedepth);
				sourcewaterdepthmin = MIN(sourcewaterdepthmin, sourcewaterdepth);
				sourcewaterdepthmax = MAX(sourcewaterdepthmax, sourcewaterdepth);
				receiverwaterdepthmin = MIN(receiverwaterdepthmin, receiverwaterdepth);
				receiverwaterdepthmax = MAX(receiverwaterdepthmax, receiverwaterdepth);
			}
		}

		if (error == MB_ERROR_NO_ERROR && first) {
			first = false;
		}
	}

	status = mb_segy_close(verbose, &mbsegyioptr, &error);

	const double tracelength = 0.000001 * (double)(fileheader.sample_interval * fileheader.number_samples);
	mb_gmt_text_put(T, "\nSEGY Data File:      %s\n", read_file);
	mb_gmt_text_put(T, "\nFile Header Info:\n");
	mb_gmt_text_put(T, "  Channels:                   %8d\n", fileheader.channels);
	mb_gmt_text_put(T, "  Auxiliary Channels:         %8d\n", fileheader.aux_channels);
	mb_gmt_text_put(T, "  Sample Interval (usec):     %8d\n", fileheader.sample_interval);
	mb_gmt_text_put(T, "  Number of Samples in Trace: %8d\n", fileheader.number_samples);
	mb_gmt_text_put(T, "  Trace length (sec):         %8f\n", tracelength);
	if (fileheader.format == 1)
		mb_gmt_text_put(T, "  Data Format:                IBM 32 bit floating point\n");
	else if (fileheader.format == 2)
		mb_gmt_text_put(T, "  Data Format:                32 bit integer\n");
	else if (fileheader.format == 3)
		mb_gmt_text_put(T, "  Data Format:                16 bit integer\n");
	else if (fileheader.format == 5)
		mb_gmt_text_put(T, "  Data Format:                IEEE 32 bit integer\n");
	else if (fileheader.format == 6)
		mb_gmt_text_put(T, "  Data Format:                IEEE 32 bit integer\n");
	else if (fileheader.format == 8)
		mb_gmt_text_put(T, "  Data Format:                8 bit integer\n");
	else if (fileheader.format == 11)
		mb_gmt_text_put(T, "  Data Format:                Little-endian IEEE 32 bit floating point\n");
	else
		mb_gmt_text_put(T, "  Data Format:                Unknown\n");
	mb_gmt_text_put(T, "  CDP Fold:                   %8d\n", fileheader.cdp_fold);
	mb_gmt_text_put(T, "\nData Totals:\n");
	mb_gmt_text_put(T, "  Number of Traces:           %8d\n", nread);
	mb_gmt_text_put(T, "  Min Max Delta:\n");
	mb_gmt_text_put(T, "    Shot number:              %8d %8d %8d\n", shotmin, shotmax, shotmax - shotmin + 1);
	mb_gmt_text_put(T, "    Shot trace:               %8d %8d %8d\n", shottracemin, shottracemax, shottracemax - shottracemin + 1);
	mb_gmt_text_put(T, "    RP number:                %8d %8d %8d\n", rpmin, rpmax, rpmax - rpmin + 1);
	mb_gmt_text_put(T, "    RP trace:                 %8d %8d %8d\n", rptracemin, rptracemax, rptracemax - rptracemin + 1);
	mb_gmt_text_put(T, "    Delay (sec):              %8f %8f %8f\n", delaymin, delaymax, delaymax - delaymin);
	mb_gmt_text_put(T, "    Range (m):                %8f %8f %8f\n", rangemin, rangemax, rangemax - rangemin);
	mb_gmt_text_put(T, "    Receiver Elevation (m):   %8f %8f %8f\n", receiverelevationmin, receiverelevationmax,
	        receiverelevationmax - receiverelevationmin);
	mb_gmt_text_put(T, "    Source Elevation (m):     %8f %8f %8f\n", sourceelevationmin, sourceelevationmax,
	        sourceelevationmax - sourceelevationmin);
	mb_gmt_text_put(T, "    Source Depth (m):         %8f %8f %8f\n", sourcedepthmin, sourcedepthmax,
	        sourcedepthmax - sourcedepthmin);
	mb_gmt_text_put(T, "    Receiver Water Depth (m): %8f %8f %8f\n", receiverwaterdepthmin, receiverwaterdepthmax,
	        receiverwaterdepthmax - receiverwaterdepthmin);
	mb_gmt_text_put(T, "    Source Water Depth (m):   %8f %8f %8f\n", sourcewaterdepthmin, sourcewaterdepthmax,
	        sourcewaterdepthmax - sourcewaterdepthmin);
	mb_gmt_text_put(T, "\nNavigation Totals:\n");
	mb_gmt_text_put(T, "\n  Start of Data:\n");
	mb_gmt_text_put(T, "    Start Time:  %2.2d %2.2d %4.4d %2.2d:%2.2d:%2.2d.%6.6d  JD%d\n", timbeg_i[1], timbeg_i[2], timbeg_i[0],
	        timbeg_i[3], timbeg_i[4], timbeg_i[5], timbeg_i[6], timbeg_j[1]);
	mb_gmt_text_put(T, "    Start Position: Lon: %14.9f     Lat: %14.9f\n", lonbeg, latbeg);
	mb_gmt_text_put(T, "\n  End of Data:\n");
	mb_gmt_text_put(T, "    End Time:    %2.2d %2.2d %4.4d %2.2d:%2.2d:%2.2d.%6.6d  JD%d\n", timend_i[1], timend_i[2], timend_i[0],
	        timend_i[3], timend_i[4], timend_i[5], timend_i[6], timend_j[1]);
	mb_gmt_text_put(T, "    End Position:   Lon: %14.9f     Lat: %14.9f \n", lonend, latend);
	mb_gmt_text_put(T, "\nLimits:\n");
	mb_gmt_text_put(T, "  Minimum Longitude:   %14.9f   Maximum Longitude:   %14.9f\n", lonmin, lonmax);
	mb_gmt_text_put(T, "  Minimum Latitude:    %14.9f   Maximum Latitude:    %14.9f\n", latmin, latmax);

	/* check memory */
	if (verbose >= 4)
		status = mb_memory_list(verbose, &error);

	if (verbose >= 2) {
		GMT_Report(API, GMT_MSG_NORMAL, "\ndbg2  Program <%s> completed\n", program_name);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2  Ending status:\n");
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       status:  %d\n", status);
	}

	const int output_failed = mb_gmt_text_end(T);
	T = NULL;
	if (output_failed) Return(GMT_RUNTIME_ERROR);

	/* The program exits with MBIO's error; as a module that is a GMT error code, never an MBIO one.
	   The end of the data (EOF) is how every read finishes, not an error. */
	if (error > MB_ERROR_NO_ERROR && error != MB_ERROR_EOF) {
		char *message;
		mb_error(verbose, error, &message);
		GMT_Report(API, GMT_MSG_ERROR, "%s\n", message);
		Return(GMT_RUNTIME_ERROR);
	}
	Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
