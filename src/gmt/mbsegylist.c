/*--------------------------------------------------------------------
 *    The MB-system:	mbsegylist.c	5/29/2004
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
 * MBsegylist prints the specified contents of a segy data
 * file to stdout. The form of the output is quite flexible;
 * MBsegylist is tailored to produce ascii files in spreadsheet
 * style with data columns separated by tabs.
 *
 * Author:	D. W. Caress
 * Date:	May 29, 2004
 */
/*
 * GMT-module port of src/utilities/mbsegylist.cc: options from GMT's option list (long options
 * through module_kw, lower-case aliases kept), the listing written as text records through the GMT
 * API (mb_gmt_text.c; the binary listing, -A, as raw doubles on stdout), and every exit() a Return()
 * with a GMT error code.
 */

#define THIS_MODULE_NAME "mbsegylist"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "List table data from a segy file"
/* Primary input is the segy file given with -I; the table goes to stdout. */
#define THIS_MODULE_KEYS "ID{,>D}"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#ifdef _MSC_VER
#include "unistd_w.h"
#else
#include <unistd.h>
#endif

#include "mb_define.h"
#include "mb_format.h"
#include "mb_segy.h"
#include "mb_status.h"
#include "mb_gmt_text.h"

enum { MAX_OPTIONS = 25 };
enum { MBLIST_CHECK_ON = 0 };
enum { MBLIST_CHECK_ON_NULL = 1 };
enum { MBLIST_CHECK_OFF_RAW = 2 };
enum { MBLIST_CHECK_OFF_NAN = 3 };
enum { MBLIST_CHECK_OFF_FLAGNAN = 4 };
enum { MBLIST_SET_OFF = 0 };
enum { MBLIST_SET_ON = 1 };
enum { MBLIST_SET_ALL = 2 };

static const char program_name[] = "MBsegylist";
static const char help_message[] =
    "MBsegylist lists table data from a segy data file.";
static const char usage_message[] =
    "MBsegylist -Ifile\n"
    "\t--binary-output {-A}\n"
    "\t--decimate=value {-Dvalue}\n"
    "\t--delimiter=character {-Gcharacter}\n"
    "\t--help {-H}\n"
    "\t--input=file {-Ifile}\n"
    "\t--longitude-domain=lonflip {-Llonflip}\n"
    "\t--output-format=list {-Olist}\n"
    "\t--segment-tag=tag {-Ztag}\n"
    "\t--use-feet {-W}\n"
    "\t--verbose {-V}\n\n";

/*--------------------------------------------------------------------*/
static int printsimplevalue(struct MB_GMT_TEXT *T, int verbose, double value, int width, int precision, bool ascii, bool *invert, bool *flipsign, int *error) {
	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  MBlist function <%s> called\n", __func__);
		fprintf(stderr, "dbg2  Input arguments:\n");
		fprintf(stderr, "dbg2       verbose:         %d\n", verbose);
		fprintf(stderr, "dbg2       value:           %f\n", value);
		fprintf(stderr, "dbg2       width:           %d\n", width);
		fprintf(stderr, "dbg2       precision:       %d\n", precision);
		fprintf(stderr, "dbg2       ascii:           %d\n", ascii);
		fprintf(stderr, "dbg2       invert:          %d\n", *invert);
		fprintf(stderr, "dbg2       flipsign:        %d\n", *flipsign);
	}

	/* make print format */
	char format[24] = "%";
	if (*invert)
		strcpy(format, "%g");
	else if (width > 0)
		snprintf(&format[1], 23, "%d.%df", width, precision);
	else
		snprintf(&format[1], 23, ".%df", precision);

	/* invert value if desired */
	if (*invert) {
		*invert = false;
		if (value != 0.0)
			value = 1.0 / value;
	}

	/* flip sign value if desired */
	if (*flipsign) {
		*flipsign = false;
		value = -value;
	}

	/* print value */
	if (ascii)
		mb_gmt_text_put(T,format, value);
	else
		fwrite(&value, sizeof(double), 1, stdout);

	const int status = MB_SUCCESS;

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  MBlist function <%s> completed\n", __func__);
		fprintf(stderr, "dbg2  Return values:\n");
		fprintf(stderr, "dbg2       invert:          %d\n", *invert);
		fprintf(stderr, "dbg2       error:           %d\n", *error);
		fprintf(stderr, "dbg2  Return status:\n");
		fprintf(stderr, "dbg2       status:          %d\n", status);
	}

	return (status);
}
/*--------------------------------------------------------------------*/
static int printNaN(struct MB_GMT_TEXT *T, int verbose, bool ascii, bool *invert, bool *flipsign, int *error) {
	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  MBlist function <%s> called\n", __func__);
		fprintf(stderr, "dbg2  Input arguments:\n");
		fprintf(stderr, "dbg2       verbose:         %d\n", verbose);
		fprintf(stderr, "dbg2       ascii:           %d\n", ascii);
		fprintf(stderr, "dbg2       invert:          %d\n", *invert);
		fprintf(stderr, "dbg2       flipsign:        %d\n", *flipsign);
	}

	/* reset invert flag */
	if (*invert)
		*invert = false;

	/* reset flipsign flag */
	if (*flipsign)
		*flipsign = false;

	/* print value */
	if (ascii) {
		mb_gmt_text_put(T,"NaN");
	} else {
		const double NaN = NAN;
		fwrite(&NaN, sizeof(double), 1, stdout);
	}

	const int status = MB_SUCCESS;

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  MBlist function <%s> completed\n", __func__);
		fprintf(stderr, "dbg2  Return values:\n");
		fprintf(stderr, "dbg2       invert:          %d\n", *invert);
		fprintf(stderr, "dbg2       error:           %d\n", *error);
		fprintf(stderr, "dbg2  Return status:\n");
		fprintf(stderr, "dbg2       status:          %d\n", status);
	}

	return (status);
}

/*--------------------------------------------------------------------*/


/* --- GMT front end ---------------------------------------------------- */

/* Translation table from the program's long options to its short ones (each one has a short twin) */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'v', "verbose",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",             "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'A', "binary-output",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'D', "decimate",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'G', "delimiter",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",            "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'L', "longitude-domain", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "output-format",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'Z', "segment-tag",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'W', "use-feet",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n", help_message);
	GMT_Message(API, GMT_TIME_NONE, "Every option also has the program's lower-case and long forms.\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

#define bailout(code) { gmt_M_free_options(mode); return code; }
#define Return(code) { if (T) mb_gmt_text_end(T); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbsegylist(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbsegylist(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MB_GMT_TEXT *T = NULL;	/* the listing: records, or stdout for the binary one (-A) */
	int gmt_error;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program, which reports the missing segy file itself */
	if ((gmt_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(gmt_error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	int verbose = 0;
	int pings;
	int lonflip;
	double bounds[4];
	int btime_i[7];
	int etime_i[7];
	double speedmin;
	double timegap;
	int format;
	int status = mb_defaults(verbose, &format, &pings, &lonflip, bounds, btime_i, etime_i, &speedmin, &timegap);

	int error = MB_ERROR_NO_ERROR;

	int decimate = 1;
	bool ascii = true;
	char delimiter[MB_PATH_MAXLINE] = "\t";
	bool segment = false;
	char segment_tag[MB_PATH_MAXLINE] = "";
	bool bathy_in_feet = false;

	char file[MB_PATH_MAXLINE] = "";

	/* set up the default list controls: TiXYSsCcDINL
	    (time, time interval, lon, lat, shot, shot trace #, cmp, cmp trace #,
	        delay, sample length, number samples, trace length) */
	int n_list = 0;
	char list[MAX_OPTIONS] = "";
	list[n_list] = 'T';
	n_list++;
	list[n_list] = 'i';
	n_list++;
	list[n_list] = 'X';
	n_list++;
	list[n_list] = 'Y';
	n_list++;
	list[n_list] = 'S';
	n_list++;
	list[n_list] = 's';
	n_list++;
	list[n_list] = 'C';
	n_list++;
	list[n_list] = 'c';
	n_list++;
	list[n_list] = 'D';
	n_list++;
	list[n_list] = 'I';
	n_list++;
	list[n_list] = 'N';
	n_list++;
	list[n_list] = 'L';
	n_list++;

	/* process argument list */
	{
		bool errflg = false;
		bool help = false;
		/* the program's options from GMT's option list (long options come in as their short twins
		   through module_kw; lower-case aliases kept) */
		for (struct GMT_OPTION *opt = options; opt; opt = opt->next)
			switch (opt->option) {
			case 'H':
			case 'h':
				help = true;
				break;
			case 'V':
			case 'v':
				verbose++;
				break;
			case 'W':
			case 'w':
				bathy_in_feet = true;
				break;
			case 'A':
			case 'a':
				ascii = false;
				break;
			case 'D':
			case 'd':
				sscanf(opt->arg, "%d", &decimate);
				break;
			case 'G':
			case 'g':
				sscanf(opt->arg, "%1023s", delimiter);
				break;
			case 'I':
			case 'i':
				sscanf(opt->arg, "%1023s", file);
				break;
			case 'L':
			case 'l':
				sscanf(opt->arg, "%d", &lonflip);
				break;
			case 'O':
			case 'o':
        n_list = 0;
				for (int j = 0; j < (int)strlen(opt->arg) && n_list < MAX_OPTIONS; j++) {
					list[n_list] = opt->arg[j];
          n_list++;
        }
				break;
			case 'Z':
			case 'z':
				segment = true;
				sscanf(opt->arg, "%1023s", segment_tag);
				break;
			default:
				errflg |= (gmt_default_option_error(GMT, opt) != 0);
				break;
			}

		if (errflg)
			Return(GMT_PARSE_ERROR);

		if (verbose == 1 || help) {
			fprintf(stderr, "\nProgram %s\n", program_name);
			fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
		}

		if (verbose >= 2) {
			fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
			fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(stderr, "dbg2  Control Parameters:\n");
			fprintf(stderr, "dbg2       verbose:        %d\n", verbose);
			fprintf(stderr, "dbg2       help:           %d\n", help);
			fprintf(stderr, "dbg2       lonflip:        %d\n", lonflip);
			fprintf(stderr, "dbg2       decimate:       %d\n", decimate);
			fprintf(stderr, "dbg2       bounds[0]:      %f\n", bounds[0]);
			fprintf(stderr, "dbg2       bounds[1]:      %f\n", bounds[1]);
			fprintf(stderr, "dbg2       bounds[2]:      %f\n", bounds[2]);
			fprintf(stderr, "dbg2       bounds[3]:      %f\n", bounds[3]);
			fprintf(stderr, "dbg2       btime_i[0]:     %d\n", btime_i[0]);
			fprintf(stderr, "dbg2       btime_i[1]:     %d\n", btime_i[1]);
			fprintf(stderr, "dbg2       btime_i[2]:     %d\n", btime_i[2]);
			fprintf(stderr, "dbg2       btime_i[3]:     %d\n", btime_i[3]);
			fprintf(stderr, "dbg2       btime_i[4]:     %d\n", btime_i[4]);
			fprintf(stderr, "dbg2       btime_i[5]:     %d\n", btime_i[5]);
			fprintf(stderr, "dbg2       btime_i[6]:     %d\n", btime_i[6]);
			fprintf(stderr, "dbg2       etime_i[0]:     %d\n", etime_i[0]);
			fprintf(stderr, "dbg2       etime_i[1]:     %d\n", etime_i[1]);
			fprintf(stderr, "dbg2       etime_i[2]:     %d\n", etime_i[2]);
			fprintf(stderr, "dbg2       etime_i[3]:     %d\n", etime_i[3]);
			fprintf(stderr, "dbg2       etime_i[4]:     %d\n", etime_i[4]);
			fprintf(stderr, "dbg2       etime_i[5]:     %d\n", etime_i[5]);
			fprintf(stderr, "dbg2       etime_i[6]:     %d\n", etime_i[6]);
			fprintf(stderr, "dbg2       speedmin:       %f\n", speedmin);
			fprintf(stderr, "dbg2       timegap:        %f\n", timegap);
			fprintf(stderr, "dbg2       file:           %s\n", file);
			fprintf(stderr, "dbg2       ascii:          %d\n", ascii);
			fprintf(stderr, "dbg2       segment:        %d\n", segment);
			fprintf(stderr, "dbg2       segment_tag:    %s\n", segment_tag);
			fprintf(stderr, "dbg2       delimiter:      %s\n", delimiter);
			fprintf(stderr, "dbg2       n_list:         %d\n", n_list);
			for (int i = 0; i < n_list; i++)
				fprintf(stderr, "dbg2         list[%d]:      %c\n", i, list[i]);
		}

		if (help)
			Return(usage(API, GMT_USAGE));
	}

	/* the listing: text records through the GMT API; the binary one (-A) is raw doubles on stdout */
	if ((T = ascii ? mb_gmt_text_begin(GMT, options) : mb_gmt_text_stdout(GMT)) == NULL)
		Return(API->error);

#ifdef _WIN32
	/* binary output must bypass the C runtime's CRLF translation */
	if (!ascii)
		_setmode(_fileno(stdout), _O_BINARY);
#endif

	/* set depth scaling */
	const double bathy_scale = bathy_in_feet ? 1.0 / 0.3048 : 1.0;

	void *mbsegyioptr;
	struct mb_segyasciiheader_struct asciiheader;
	struct mb_segyfileheader_struct fileheader;

	/* initialize reading the segy file */
	if (mb_segy_read_init(verbose, file, &mbsegyioptr, &asciiheader, &fileheader, &error) != MB_SUCCESS) {
		char *message = NULL;
		mb_error(verbose, error, &message);
		GMT_Report(API, GMT_MSG_ERROR, "MBIO Error returned from function <mb_segy_read_init>: %s\n", message);
		GMT_Report(API, GMT_MSG_ERROR, "SEGY File <%s> not initialized for reading\n", file);
		Return(GMT_RUNTIME_ERROR);
	}

	/* output separator for GMT style segment file output */
	if (segment && ascii) {
		mb_gmt_text_put(T,"%s\n", segment_tag);
	}

	/* segy data */
	float *trace;

	/* output format list controls */
	bool invert_next_value = false;
	bool signflip_next_value = false;  // TODO(schwehr): signflip or flipsign.  Be consistent.

	bool first_m = true;
	double time_d_ref;
	bool first_u = true;
	time_t time_u;
	time_t time_u_ref;
	double time_interval = 0.0;
	double minutes;
	int degrees;
	char hemi;

	int time_i[7], time_j[5];
	double time_d, time_d_old;
	double navlon, navlat;
	double factor, sensordepth, waterdepth;
	double delay, interval;
	double seconds;

	/* read and print data */
	int nread = 0;
	bool first = true;
	while (error <= MB_ERROR_NO_ERROR) {
		/* reset error */
		error = MB_ERROR_NO_ERROR;

		/* read a trace */
		struct mb_segytraceheader_struct traceheader;
		status = mb_segy_read_trace(verbose, mbsegyioptr, &traceheader, &trace, &error);

		/* get needed values */
		if (status == MB_SUCCESS) {
			nread++;
			time_j[0] = traceheader.year;
			time_j[1] = traceheader.day_of_yr;
			time_j[2] = traceheader.min + 60 * traceheader.hour;
			time_j[3] = traceheader.sec;
			time_j[4] = 1000 * traceheader.mils;
			mb_get_itime(verbose, time_j, time_i);
			mb_get_time(verbose, time_i, &time_d);
			if (first) {
				time_d_old = time_d;
			}
			if (traceheader.elev_scalar < 0)
				factor = 1.0 / ((float)(-traceheader.elev_scalar));
			else
				factor = (float)traceheader.elev_scalar;
			if (traceheader.grp_elev != 0)
				sensordepth = -factor * traceheader.grp_elev;
			else if (traceheader.src_elev != 0)
				sensordepth = -factor * traceheader.src_elev;
			else if (traceheader.src_depth != 0)
				sensordepth = factor * traceheader.src_depth;
			else
				sensordepth = 0.0;
			if (traceheader.src_wbd != 0)
				waterdepth = -traceheader.grp_elev;
			else if (traceheader.grp_wbd != 0)
				waterdepth = -traceheader.src_elev;
			else
				waterdepth = 0;
			if (traceheader.coord_scalar < 0)
				factor = 1.0 / ((float)(-traceheader.coord_scalar)) / 3600.0;
			else
				factor = (float)traceheader.coord_scalar / 3600.0;
			if (traceheader.src_long != 0)
				navlon = factor * ((float)traceheader.src_long);
			else
				navlon = factor * ((float)traceheader.grp_long);
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
		}

		/* print out info */
		if (status == MB_SUCCESS && (nread - 1) % decimate == 0) {
			for (int i = 0; i < n_list; i++) {
				switch (list[i]) {
				case '/': /* Inverts next simple value */
					invert_next_value = true;
					break;
				case '-': /* Flip sign on next simple value */
					signflip_next_value = true;
					break;
				case 'C': /* CDP number or CMP number or RP number */
					if (ascii)
						mb_gmt_text_put(T,"%6d", traceheader.rp_num);
					else {
						const double b = traceheader.rp_num;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'c': /* CDP trace or CMP trace or RP trace */
					if (ascii)
						mb_gmt_text_put(T,"%6d", traceheader.rp_tr);
					else {
						const double b = traceheader.rp_tr;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'D': /* trace start delay */
					delay = 0.001 * traceheader.delay_mils;
					printsimplevalue(T, verbose,delay, 0, 3, ascii, &invert_next_value, &signflip_next_value, &error);
					break;
				case 'I': /* sample interval in seconds */
					interval = 0.000001 * traceheader.si_micros;
					printsimplevalue(T, verbose,interval, 0, 6, ascii, &invert_next_value, &signflip_next_value, &error);
					break;
				case 'i': /* time interval since last trace */
					interval = time_d - time_d_old;
					printsimplevalue(T, verbose,interval, 0, 3, ascii, &invert_next_value, &signflip_next_value, &error);
					break;
				case 'J': /* time string */
					mb_get_jtime(verbose, time_i, time_j);
					seconds = time_i[5] + 0.000001 * time_i[6];
					if (ascii) {
						mb_gmt_text_put(T,"%.4d %.3d %.2d %.2d %9.6f", time_j[0], time_j[1], time_i[3], time_i[4], seconds);
					}
					else {
						double b = time_j[0];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_j[1];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[3];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[4];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[5];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[6];
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'j': /* time string */
					mb_get_jtime(verbose, time_i, time_j);
					seconds = time_i[5] + 0.000001 * time_i[6];
					if (ascii) {
						mb_gmt_text_put(T,"%.4d %.3d %.4d %9.6f", time_j[0], time_j[1], time_j[2], seconds);
					}
					else {
						double b = time_j[0];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_j[1];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_j[2];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_j[3];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_j[4];
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'L': /* Trace length in seconds */
					interval = 0.000001 * traceheader.si_micros * traceheader.nsamps;
					printsimplevalue(T, verbose,interval, 0, 6, ascii, &invert_next_value, &signflip_next_value, &error);
					break;
				case 'l': /* Line number from fileheader */
					if (ascii)
						mb_gmt_text_put(T,"%6d", fileheader.line);
					else {
						const double b = fileheader.line;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'M': /* Decimal unix seconds since
				        1/1/70 00:00:00 */
					printsimplevalue(T, verbose,time_d, 0, 6, ascii, &invert_next_value, &signflip_next_value, &error);
					break;
				case 'm': /* time in decimal seconds since first record */
				{
					if (first_m) {
						time_d_ref = time_d;
						first_m = false;
					}
					const double b = time_d - time_d_ref;
					printsimplevalue(T, verbose,b, 0, 6, ascii, &invert_next_value, &signflip_next_value, &error);
					break;
				}
				case 'N': /* number of samples in trace */
					if (ascii)
						mb_gmt_text_put(T,"%6d", traceheader.nsamps);
					else {
						const double b = traceheader.nsamps;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'n': /* trace counter */
					if (ascii)
						mb_gmt_text_put(T,"%6d", nread);
					else {
						const double b = nread;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'R': /* range */
					if (ascii)
						mb_gmt_text_put(T,"%6d", traceheader.range);
					else {
						const double b = traceheader.range;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'S': /* shot number */
					if (ascii)
						mb_gmt_text_put(T,"%6d", traceheader.shot_num);
					else {
						const double b = traceheader.shot_num;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 's': /* shot trace */
					if (ascii)
						mb_gmt_text_put(T,"%6d", traceheader.shot_tr);
					else {
						const double b = traceheader.shot_tr;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'T': /* yyyy/mm/dd/hh/mm/ss time string */
					seconds = time_i[5] + 1e-6 * time_i[6];
					if (ascii)
						mb_gmt_text_put(T,"%.4d/%.2d/%.2d/%.2d/%.2d/%09.6f", time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], seconds);
					else {
						double b = time_i[0];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[1];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[2];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[3];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[4];
						fwrite(&b, sizeof(double), 1, stdout);
						b = seconds;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 't': /* yyyy mm dd hh mm ss time string */
					seconds = time_i[5] + 1e-6 * time_i[6];
					if (ascii)
						mb_gmt_text_put(T,"%.4d %.2d %.2d %.2d %.2d %09.6f", time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], seconds);
					else {
						double b = time_i[0];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[1];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[2];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[3];
						fwrite(&b, sizeof(double), 1, stdout);
						b = time_i[4];
						fwrite(&b, sizeof(double), 1, stdout);
						b = seconds;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'U': /* unix time in seconds since 1/1/70 00:00:00 */
					time_u = (time_t)time_d;
					if (ascii)
						mb_gmt_text_put(T,"%lld", (long long)time_u);
					else {
						const double b = time_u;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'u': /* time in seconds since first record */
					time_u = (time_t)time_d;
					if (first_u) {
						time_u_ref = time_u;
						first_u = false;
					}
					if (ascii)
						mb_gmt_text_put(T,"%lld", (long long)(time_u - time_u_ref));
					else {
						const double b = time_u - time_u_ref;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'V': /* time in seconds since last ping */
				case 'v':
					time_interval = time_d - time_d_old;
					if (ascii) {
						if (fabs(time_interval) > 100.)
							mb_gmt_text_put(T,"%g", time_interval);
						else
							mb_gmt_text_put(T,"%7.3f", time_interval);
					}
					else {
						fwrite(&time_interval, sizeof(double), 1, stdout);
					}
					break;
				case 'X': /* longitude decimal degrees */
					printsimplevalue(T, verbose,navlon, 11, 6, ascii, &invert_next_value, &signflip_next_value, &error);
					break;
				case 'x': /* longitude degress + decimal minutes */
					if (navlon < 0.0) {
						hemi = 'W';
						navlon = -navlon;
					}
					else
						hemi = 'E';
					degrees = (int)navlon;
					minutes = 60.0 * (navlon - degrees);
					if (ascii) {
						mb_gmt_text_put(T,"%3d %8.5f%c", degrees, minutes, hemi);
					}
					else {
						double b = degrees;
						if (hemi == 'W')
							b = -b;
						fwrite(&b, sizeof(double), 1, stdout);
						b = minutes;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'Y': /* latitude decimal degrees */
					printsimplevalue(T, verbose,navlat, 11, 6, ascii, &invert_next_value, &signflip_next_value, &error);
					break;
				case 'y': /* latitude degrees + decimal minutes */
					if (navlat < 0.0) {
						hemi = 'S';
						navlat = -navlat;
					}
					else
						hemi = 'N';
					degrees = (int)navlat;
					minutes = 60.0 * (navlat - degrees);
					if (ascii) {
						mb_gmt_text_put(T,"%3d %8.5f%c", degrees, minutes, hemi);
					}
					else {
						double b = degrees;
						if (hemi == 'S')
							b = -b;
						fwrite(&b, sizeof(double), 1, stdout);
						b = minutes;
						fwrite(&b, sizeof(double), 1, stdout);
					}
					break;
				case 'Z': /* sonar depth (m) */
					if (traceheader.elev_scalar < 0)
						factor = 1.0 / ((float)(-traceheader.elev_scalar));
					else
						factor = (float)traceheader.elev_scalar;
					if (traceheader.grp_elev != 0)
						sensordepth = -factor * traceheader.grp_elev;
					else if (traceheader.src_elev != 0)
						sensordepth = -factor * traceheader.src_elev;
					else if (traceheader.src_depth != 0)
						sensordepth = factor * traceheader.src_depth;
					else
						sensordepth = 0.0;
					sensordepth *= bathy_scale;
					printsimplevalue(T, verbose,sensordepth, 11, 6, ascii, &invert_next_value, &signflip_next_value, &error);
					break;
				case 'z': /* water depth (m) */
					if (traceheader.elev_scalar < 0)
						factor = 1.0 / ((float)(-traceheader.elev_scalar));
					else
						factor = (float)traceheader.elev_scalar;
					if (traceheader.src_wbd != 0)
						waterdepth = -factor * traceheader.src_wbd;
					else if (traceheader.grp_wbd != 0)
						waterdepth = -factor * traceheader.grp_wbd;
					else
						waterdepth = 0.0;
					waterdepth *= bathy_scale;
					printsimplevalue(T, verbose,waterdepth, 11, 6, ascii, &invert_next_value, &signflip_next_value, &error);
					break;
				default:
					if (ascii)
						mb_gmt_text_put(T,"<Invalid Option: %c>", list[i]);
					break;
				}
				if (ascii) {
					if (i < (n_list - 1))
						mb_gmt_text_put(T,"%s", delimiter);
					else
						mb_gmt_text_put(T,"\n");
				}
			}
		}

		/* reset first flag */
		if (error == MB_ERROR_NO_ERROR && first) {
			first = false;
		}

		/* save old values */
		if (error == MB_ERROR_NO_ERROR) {
			time_d_old = time_d;
		}
	}

	/* close the swath file */
	status = mb_segy_close(verbose, &mbsegyioptr, &error);

	/* check memory */
	if (verbose >= 4)
		status = mb_memory_list(verbose, &error);

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
		fprintf(stderr, "dbg2  Ending status:\n");
		fprintf(stderr, "dbg2       status:  %d\n", status);
	}

	const int output_failed = mb_gmt_text_end(T);
	T = NULL;	/* closed: Return() must not close it again */
	if (output_failed) Return(GMT_RUNTIME_ERROR);
	/* The program exits with MBIO's error; as a module that is a GMT error code, never an MBIO one */
	if (error != MB_ERROR_NO_ERROR) {
		char *message;
		mb_error(verbose, error, &message);
		GMT_Report(API, GMT_MSG_ERROR, "%s\n", message);
		Return(GMT_RUNTIME_ERROR);
	}
	Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
