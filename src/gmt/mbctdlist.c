/*--------------------------------------------------------------------
 *    The MB-system:	mbctdlist.c	9/14/2008
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
 * This program, mbctdlist, lists all ctd data records within swath data files.
 * The -O option specifies how the values are output in an mblist-like
 * fashion. The basic available values are
 *     conductivity
 *     temperature
 *     depth
 *     salinity
 *     sound speed
 *     longitude
 *     latittude
 *
 * Author:	D. W. Caress
 * Date:	September 14,  2008
 *
 * GMT-module port of src/utilities/mbctdlist.cc: the getopt_long loop
 * is replaced by the GMT option parser (long options through module_kw,
 * lower-case aliases kept), the listing goes through the GMT API (-A: raw doubles on stdout)
 * and main() becomes GMT_mbctdlist(), every exit() a Return() with a GMT error code.
 */

#define THIS_MODULE_NAME "mbctdlist"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "List CTD records embedded in swath data files"
/* Primary input is the swath file or datalist given with -I; the CTD
 * records are listed to stdout. */
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
#include "mb_aux.h"
#include "mb_define.h"
#include "mb_format.h"
#include "mb_process.h"
#include "mb_status.h"
#include "mb_gmt_opts.h"
#include "mb_gmt_text.h"

enum { MAX_OPTIONS = 25 };
enum { MBCTDLIST_ALLOC_CHUNK = 1024 };

static const char program_name[] = "mbctdlist";
static const char help_message[] =
    "mbctdlist lists all CTD records within swath data files\n"
    "The -O option specifies how the values are output\n"
    "in an mblist-likefashion.\n";
static const char usage_message[] =
    "mbctdlist\n"
    "\t--binary {-A}\n"
    "\t--decimate=decimate {-Ddecimate}\n"
    "\t--delimiter=delimiter {-Gdelimeter}\n"
    "\t--format=format {-Fformat}\n"
    "\t--help {-H}\n"
    "\t--input=file {-Ifile}\n"
    "\t--longitude-domain=lonflip {-Llonflip}\n"
    "\t--output-format=output_format {-Ooutput_format}\n"
    "\t--segment=segment {-Zsegment}\n"
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
	char format[24];
	format[0] = '%';
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
		mb_gmt_text_put(T, format, value);
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

/* --- Control structure ---------------------------------------------- */

struct MBCTDLIST_CTRL {
	int verbose;	/* the program's -V/-v count */
	struct mbcl_A { bool active; } A;
	struct mbcl_D { bool active; int decimate; } D;
	struct mbcl_F { bool active; int format; } F;
	struct mbcl_G { bool active; char delimiter[MB_PATH_MAXLINE]; } G;
	struct mbcl_H { bool active; } H;
	struct mbcl_I { bool active; char file[MB_PATH_MAXLINE]; } I;
	struct mbcl_L { bool active; int lonflip; } L;
	struct mbcl_O { bool active; char list[MB_PATH_MAXLINE]; } O;
	struct mbcl_Z { bool active; char segment_tag[MB_PATH_MAXLINE]; } Z;
};

static void *New_mbctdlist_Ctrl(struct GMT_CTRL *GMT) {
	struct MBCTDLIST_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBCTDLIST_CTRL);
	return Ctrl;
}

static void Free_mbctdlist_Ctrl(struct GMT_CTRL *GMT, struct MBCTDLIST_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'v', "verbose",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",             "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'A', "binary",           "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'D', "decimate",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'F', "format",           "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'G', "delimiter",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",            "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'L', "longitude-domain", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "output-format",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'Z', "segment",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n", help_message);
	GMT_Message(API, GMT_TIME_NONE,
	            "\t-A Write binary doubles instead of ASCII text.\n"
	            "\t-D Output every decimate-th record.\n"
	            "\t-F Input MBIO format.\n"
	            "\t-G Delimiter between output columns [tab].\n"
	            "\t-I Input swath file or datalist [datalist.mb-1].\n"
	            "\t-L Longitude domain (lonflip).\n"
	            "\t-O Output value list (mblist-like) [TXYHCc^cSs].\n"
	            "\t-Z Write GMT segment headers using this tag.\n"
	            "\t-H Print help and exit.\n"
	            "\tEvery option also has the program's lower-case and long forms.\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse_mbctdlist(struct GMT_CTRL *GMT, struct MBCTDLIST_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt;
	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case 'V': case 'v':
			Ctrl->verbose++;
			break;
		case 'A': case 'a':
			Ctrl->A.active = true;
			break;
		case 'D': case 'd':
			if (opt->arg && sscanf(opt->arg, "%d", &Ctrl->D.decimate) == 1) Ctrl->D.active = true;
			else n_errors++;
			break;
		case 'F': case 'f':
			if (opt->arg && sscanf(opt->arg, "%d", &Ctrl->F.format) == 1) Ctrl->F.active = true;
			else n_errors++;
			break;
		case 'G': case 'g':
			if (opt->arg && opt->arg[0]) {
				snprintf(Ctrl->G.delimiter, sizeof(Ctrl->G.delimiter), "%s", opt->arg);
				Ctrl->G.active = true;
			}
			else n_errors++;
			break;
		case 'H': case 'h':
			Ctrl->H.active = true;
			break;
		case 'I': case 'i':
			if (opt->arg && opt->arg[0]) {
				snprintf(Ctrl->I.file, sizeof(Ctrl->I.file), "%s", opt->arg);
				Ctrl->I.active = true;
			}
			else n_errors++;
			break;
		case 'L': case 'l':
			if (opt->arg && sscanf(opt->arg, "%d", &Ctrl->L.lonflip) == 1) Ctrl->L.active = true;
			else n_errors++;
			break;
		case 'O': case 'o':
			if (opt->arg && opt->arg[0]) {
				snprintf(Ctrl->O.list, sizeof(Ctrl->O.list), "%s", opt->arg);
				Ctrl->O.active = true;
			}
			else n_errors++;
			break;
		case 'Z': case 'z':
			if (opt->arg && opt->arg[0]) {
				snprintf(Ctrl->Z.segment_tag, sizeof(Ctrl->Z.segment_tag), "%s", opt->arg);
				Ctrl->Z.active = true;
			}
			else n_errors++;
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}
	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code) { if (T) mb_gmt_text_end(T); Free_mbctdlist_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbctdlist(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbctdlist(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MBCTDLIST_CTRL *Ctrl = NULL;
	struct MB_GMT_TEXT *T = NULL;	/* the listing: text records through the GMT API, or raw stdout for -A */
	int parse_error;

	if (!API) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no arguments is a valid run: mbctdlist reads datalist.mb-1 */
	if ((parse_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(parse_error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = (struct MBCTDLIST_CTRL *)New_mbctdlist_Ctrl(GMT);
	if ((parse_error = parse_mbctdlist(GMT, Ctrl, options)) != GMT_NOERROR) Return(parse_error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	int verbose = Ctrl->verbose;
	int format;
	int pings;
	int lonflip;
	double bounds[4];
	int btime_i[7];
	int etime_i[7];
	double speedmin;
	double timegap;
	int status = mb_defaults(verbose, &format, &pings, &lonflip, bounds, btime_i, etime_i, &speedmin, &timegap);
	pings = 1;
	bounds[0] = -360.0;
	bounds[1] = 360.0;
	bounds[2] = -90.0;
	bounds[3] = 90.0;

	/* set up the default list controls
	    (Time, lon, lat, conductivity, temperature, depth, salinity, sound speed) */
	char list[MAX_OPTIONS];
	list[0] = 'T';
	list[1] = 'X';
	list[2] = 'Y';
	list[3] = 'H';
	list[4] = 'C';
	list[5] = 'c';
	list[6] = '^';
	list[7] = 'c';
	list[8] = 'S';
	list[9] = 's';
	int n_list = 10;

	bool ascii = true;
	char delimiter[MB_PATH_MAXLINE] = "\t";
	int decimate = 1;
	char read_file[MB_PATH_MAXLINE] = "datalist.mb-1";
	bool segment = false;
	char segment_tag[MB_PATH_MAXLINE] = "";

	{

		if (Ctrl->A.active)
			ascii = false;
		if (Ctrl->D.active)
			decimate = Ctrl->D.decimate;
		if (Ctrl->F.active)
			format = Ctrl->F.format;
		if (Ctrl->G.active)
			sscanf(Ctrl->G.delimiter, "%1023s", delimiter);
		if (Ctrl->I.active)
			sscanf(Ctrl->I.file, "%1023s", read_file);
		if (Ctrl->L.active)
			lonflip = Ctrl->L.lonflip;
		if (Ctrl->O.active) {
			n_list = 0;
			for (int j = 0; j < (int)strlen(Ctrl->O.list) && n_list < MAX_OPTIONS; j++)
				list[n_list++] = Ctrl->O.list[j];
		}
		if (Ctrl->Z.active) {
			segment = true;
			sscanf(Ctrl->Z.segment_tag, "%1023s", segment_tag);
		}

		if (verbose == 1) {
			fprintf(stderr, "\nProgram %s\n", program_name);
			fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
		}

		if (verbose >= 2) {
			fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
			fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(stderr, "dbg2  Control Parameters:\n");
			fprintf(stderr, "dbg2       verbose:        %d\n", verbose);
			fprintf(stderr, "dbg2       format:         %d\n", format);
			fprintf(stderr, "dbg2       pings:          %d\n", pings);
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
			fprintf(stderr, "dbg2       ascii:          %d\n", ascii);
			fprintf(stderr, "dbg2       segment:        %d\n", segment);
			fprintf(stderr, "dbg2       segment_tag:    %s\n", segment_tag);
			fprintf(stderr, "dbg2       delimiter:      %s\n", delimiter);
			fprintf(stderr, "dbg2       n_list:         %d\n", n_list);
			for (int i = 0; i < n_list; i++)
				fprintf(stderr, "dbg2         list[%d]:      %c\n", i, list[i]);
		}

	}

#ifdef _WIN32
	/* Binary output must bypass the C runtime's CRLF translation. */
	if (!ascii)
		_setmode(_fileno(stdout), _O_BINARY);
#endif

	/* the listing: text records through the GMT API; -A's binary doubles raw on stdout */
	if ((T = ascii ? mb_gmt_text_begin(GMT, options) : mb_gmt_text_stdout(GMT)) == NULL)
		Return(API->error ? API->error : GMT_RUNTIME_ERROR);

	int error = MB_ERROR_NO_ERROR;

	if (format == 0)
		mb_get_format(verbose, read_file, NULL, &format, &error);

	/**************************************************************************************/
	/* section 1 - read all data and save nav etc for interpolation onto ctd data */

	/* determine whether to read one file or a list of files */
	const bool read_datalist = format < 0;
	void *datalist = NULL;
	char file[MB_PATH_MAXLINE];
	char dfile[MB_PATH_MAXLINE];
	double file_weight;
	bool read_data;

	/* open file list */
	if (read_datalist) {
		const int look_processed = MB_DATALIST_LOOK_UNSET;
		if (mb_datalist_open(verbose, &datalist, read_file, look_processed, &error) != MB_SUCCESS) {
			fprintf(stderr, "\nUnable to open data list file: %s\n", read_file);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			Return(GMT_ERROR_ON_FOPEN);
		}
		read_data = mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight, &error) == MB_SUCCESS;
	} else {
		// else copy single filename to be read
		strcpy(file, read_file);
		read_data = true;
	}

	void *mbio_ptr = NULL;
	double btime_d;
	double etime_d;
	int beams_bath;
	int beams_amp;
	int pixels_ss;
	char *beamflag = NULL;
	double *bath = NULL;
	double *bathacrosstrack = NULL;
	double *bathalongtrack = NULL;
	double *amp = NULL;
	double *ss = NULL;
	double *ssacrosstrack = NULL;
	double *ssalongtrack = NULL;

	void *store_ptr;

	int kind;
	int time_i[7];
	double time_d;
	double navlon;
	double navlat;
	double speed;
	double heading;
	double distance;
	double altitude;
	double sensordepth;
	char comment[MB_COMMENT_MAXLINE];

	int nnav = 0;
	int nnav_alloc = 0;
	double *nav_time_d = NULL;
	double *nav_lon = NULL;
	double *nav_lat = NULL;
	double *nav_sensordepth = NULL;
	double *nav_heading = NULL;
	double *nav_speed = NULL;
	double *nav_altitude = NULL;
	int survey_count_tot = 0;

	/* loop over all files to be read */
	while (read_data) {
		/* initialize reading the swath file */
		if (mb_read_init(verbose, file, format, pings, lonflip, bounds, btime_i, etime_i, speedmin, timegap, &mbio_ptr,
		                           &btime_d, &etime_d, &beams_bath, &beams_amp, &pixels_ss, &error) != MB_SUCCESS) {
			char *message;
			mb_error(verbose, error, &message);
			fprintf(stderr, "\nMBIO Error returned from function <mb_read_init>:\n%s\n", message);
			fprintf(stderr, "\nMultibeam File <%s> not initialized for reading\n", file);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			Return(GMT_RUNTIME_ERROR);
		}

		/* allocate memory for data arrays */
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(char), (void **)&beamflag, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bath, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_AMPLITUDE, sizeof(double), (void **)&amp, &error);
		if (error == MB_ERROR_NO_ERROR)
			status =
			    mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathacrosstrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status =
			    mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathalongtrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ss, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssacrosstrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssalongtrack, &error);

		/* if error initializing memory then quit */
		if (error != MB_ERROR_NO_ERROR) {
			char *message;
			mb_error(verbose, error, &message);
			fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			Return(GMT_MEMORY_ERROR);
		}

		/* output separator for GMT style segment file output */
		if (segment && ascii) {
			mb_gmt_text_put(T, "%s\n", segment_tag);
		}

		/* output info */
		if (verbose >= 1) {
			fprintf(stderr, "\nSearching %s for survey records\n", file);
		}

		/* read and print data */
		int survey_count = 0;
		// first = true;  // TODO(schwehr): Should first be used in this while loop?
		while (error <= MB_ERROR_NO_ERROR) {
			/* read a data record */
			status = mb_get_all(verbose, mbio_ptr, &store_ptr, &kind, time_i, &time_d, &navlon, &navlat, &speed, &heading,
			                    &distance, &altitude, &sensordepth, &beams_bath, &beams_amp, &pixels_ss, beamflag, bath, amp,
			                    bathacrosstrack, bathalongtrack, ss, ssacrosstrack, ssalongtrack, comment, &error);

			if (verbose >= 2) {
				fprintf(stderr, "\ndbg2  Ping read in program <%s>\n", program_name);
				fprintf(stderr, "dbg2       kind:           %d\n", kind);
				fprintf(stderr, "dbg2       error:          %d\n", error);
				fprintf(stderr, "dbg2       status:         %d\n", status);
			}

			/* if survey data save the nav etc */
			if (error <= MB_ERROR_NO_ERROR && kind == MB_DATA_DATA) {
				/* allocate memory for navigation/attitude arrays if needed */
				if (nnav + 1 >= nnav_alloc) {
					nnav_alloc += MBCTDLIST_ALLOC_CHUNK;
					/* status &= */ mb_reallocd(verbose, __FILE__, __LINE__, nnav_alloc * sizeof(double), (void **)&nav_time_d, &error);
					/* status &= */ mb_reallocd(verbose, __FILE__, __LINE__, nnav_alloc * sizeof(double), (void **)&nav_lon, &error);
					/* status &= */ mb_reallocd(verbose, __FILE__, __LINE__, nnav_alloc * sizeof(double), (void **)&nav_lat, &error);
					/* status &= */ mb_reallocd(verbose, __FILE__, __LINE__, nnav_alloc * sizeof(double), (void **)&nav_speed, &error);
					/* status &= */
					    mb_reallocd(verbose, __FILE__, __LINE__, nnav_alloc * sizeof(double), (void **)&nav_sensordepth, &error);
					/* status &= */ mb_reallocd(verbose, __FILE__, __LINE__, nnav_alloc * sizeof(double), (void **)&nav_heading, &error);
					/* status &= */
					    mb_reallocd(verbose, __FILE__, __LINE__, nnav_alloc * sizeof(double), (void **)&nav_altitude, &error);
					if (error != MB_ERROR_NO_ERROR) {
						char *message;
						mb_error(verbose, error, &message);
						fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
						fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
						Return(GMT_MEMORY_ERROR);
					}
				}

				/* save the nav etc */
				if (nnav == 0 || time_d > nav_time_d[nnav - 1]) {
					nav_time_d[nnav] = time_d;
					nav_lon[nnav] = navlon;
					nav_lat[nnav] = navlat;
					nav_speed[nnav] = speed;
					nav_sensordepth[nnav] = sensordepth;
					nav_heading[nnav] = heading;
					nav_altitude[nnav] = altitude;
					nnav++;
				}
				survey_count++;
				survey_count_tot++;
			}
		}

		status &= mb_close(verbose, &mbio_ptr, &error);

		/* output info */
		if (verbose >= 1) {
			fprintf(stderr, "nav extracted from %d survey records\n", survey_count);
		}

		/* figure out whether and what to read next */
		if (read_datalist) {
			read_data = mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight, &error) == MB_SUCCESS;
		} else {
			read_data = false;
		}

		/* end loop over files in list */
	}
	if (read_datalist)
		mb_datalist_close(verbose, &datalist, &error);

	/* output info */
	if (verbose >= 1) {
		fprintf(stderr, "\nTotal %d survey records\n", survey_count_tot);
	}

	/**************************************************************************************/

	/* section 2 - read data and output ctd data with time interpolation of nav etc */

	/* open file list */
	if (read_datalist) {
		const int look_processed = MB_DATALIST_LOOK_UNSET;
		if (mb_datalist_open(verbose, &datalist, read_file, look_processed, &error) != MB_SUCCESS) {
			fprintf(stderr, "\nUnable to open data list file: %s\n", read_file);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			Return(GMT_ERROR_ON_FOPEN);
		}
		read_data = mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight, &error) == MB_SUCCESS;
	} else {
		// else copy single filename to be read
		strcpy(file, read_file);
		read_data = true;
	}

	int nctd;
	double ctd_time_d[MB_CTD_MAX];
	double ctd_conductivity[MB_CTD_MAX];
	double ctd_temperature[MB_CTD_MAX];
	double ctd_depth[MB_CTD_MAX];
	double ctd_salinity[MB_CTD_MAX];
	double ctd_soundspeed[MB_CTD_MAX];
	int nsensor;
	double sensor_time_d[MB_CTD_MAX];
	double sensor1[MB_CTD_MAX];
	double sensor2[MB_CTD_MAX];
	double sensor3[MB_CTD_MAX];
	double sensor4[MB_CTD_MAX];
	double sensor5[MB_CTD_MAX];
	double sensor6[MB_CTD_MAX];
	double sensor7[MB_CTD_MAX];
	double sensor8[MB_CTD_MAX];
	// int ictd;
	double conductivity;
	double temperature;
	double potentialtemperature;
	double salinity;
	double soundspeed;
	double mtodeglon;
	double mtodeglat;
	double course;
	double time_interval;
	double speed_made_good;
	double course_old;  // TODO(schwehr): cpplint says reassigned a value before the old one has been used.
	double speed_made_good_old;  // TODO(schwehr): cpplint says reassigned a value before the old one has been used.
	double time_d_old;
	double navlon_old;
	double navlat_old;
	double distance_total = 0.0;
	int ctd_count_tot = 0;
	bool invert_next_value = false;
	bool signflip_next_value = false;
	bool mblist_next_value = false;
	int time_j[5];
	bool first_m = true;
	double time_d_ref;
	time_t time_u;  // TODO(schwehr): Localize
	bool first_u = true;
	time_t time_u_ref;
	double dlon;
	double dlat;
	double minutes;
	int degrees;
	char hemi;

	/* loop over all files to be read */
	while (read_data) {
		/* initialize reading the swath file */
		if (mb_read_init(verbose, file, format, pings, lonflip, bounds, btime_i, etime_i, speedmin, timegap, &mbio_ptr,
		                           &btime_d, &etime_d, &beams_bath, &beams_amp, &pixels_ss, &error) != MB_SUCCESS) {
			char *message;
			mb_error(verbose, error, &message);
			fprintf(stderr, "\nMBIO Error returned from function <mb_read_init>:\n%s\n", message);
			fprintf(stderr, "\nMultibeam File <%s> not initialized for reading\n", file);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			Return(GMT_RUNTIME_ERROR);
		}

		/* allocate memory for data arrays */
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(char), (void **)&beamflag, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bath, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_AMPLITUDE, sizeof(double), (void **)&amp, &error);
		if (error == MB_ERROR_NO_ERROR)
			status =
			    mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathacrosstrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status =
			    mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathalongtrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ss, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssacrosstrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssalongtrack, &error);

		/* if error initializing memory then quit */
		if (error != MB_ERROR_NO_ERROR) {
			char *message;
			mb_error(verbose, error, &message);
			fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			Return(GMT_MEMORY_ERROR);
		}

		/* output info */
		if (verbose >= 1) {
			fprintf(stderr, "\nSearching %s for CTD records\n", file);
		}

		/* read and print data */
		int ctd_count = 0;
		bool first = true;
		while (error <= MB_ERROR_NO_ERROR) {
			/* read a data record */
			status = mb_get_all(verbose, mbio_ptr, &store_ptr, &kind, time_i, &time_d, &navlon, &navlat, &speed, &heading,
			                    &distance, &altitude, &sensordepth, &beams_bath, &beams_amp, &pixels_ss, beamflag, bath, amp,
			                    bathacrosstrack, bathalongtrack, ss, ssacrosstrack, ssalongtrack, comment, &error);

			if (verbose >= 2) {
				fprintf(stderr, "\ndbg2  Ping read in program <%s>\n", program_name);
				fprintf(stderr, "dbg2       kind:           %d\n", kind);
				fprintf(stderr, "dbg2       error:          %d\n", error);
				fprintf(stderr, "dbg2       status:         %d\n", status);
			}

			/* if ctd then extract data */
			if (error <= MB_ERROR_NO_ERROR && (kind == MB_DATA_CTD || kind == MB_DATA_SSV)) {
				/* extract ctd */
				status &= mb_ctd(verbose, mbio_ptr, store_ptr, &kind, &nctd, ctd_time_d, ctd_conductivity, ctd_temperature,
				                ctd_depth, ctd_salinity, ctd_soundspeed, &error);

				/* extract ancillary sensor data */
				status &= mb_ancilliarysensor(verbose, mbio_ptr, store_ptr, &kind, &nsensor, sensor_time_d, sensor1, sensor2,
				                             sensor3, sensor4, sensor5, sensor6, sensor7, sensor8, &error);

				/* loop over the nctd ctd points, outputting each one */
				if (error == MB_ERROR_NO_ERROR && nctd > 0) {
					for (int ictd = 0; ictd < nctd; ictd++) {
						/* get data */
						time_d = ctd_time_d[ictd];
						mb_get_date(verbose, time_d, time_i);
						conductivity = ctd_conductivity[ictd];
						temperature = ctd_temperature[ictd];
						// const double depth = ctd_depth[ictd];
						salinity = ctd_salinity[ictd];
						soundspeed = ctd_soundspeed[ictd];

						/* get navigation */
						int j = 0;
						speed = 0.0;
						int interp_status =
						    mb_linear_interp_longitude(verbose, nav_time_d - 1, nav_lon - 1, nnav, time_d, &navlon, &j, &error);
						if (interp_status == MB_SUCCESS)
							interp_status &= mb_linear_interp_latitude(verbose, nav_time_d - 1, nav_lat - 1, nnav, time_d, &navlat,
							                                          &j, &error);
						if (interp_status == MB_SUCCESS)
							interp_status &= mb_linear_interp_heading(verbose, nav_time_d - 1, nav_heading - 1, nnav, time_d,
							                                         &heading, &j, &error);
						if (interp_status == MB_SUCCESS)
							interp_status &= mb_linear_interp(verbose, nav_time_d - 1, nav_sensordepth - 1, nnav, time_d,
							                                 &sensordepth, &j, &error);
						if (interp_status == MB_SUCCESS)
							interp_status &=
							    mb_linear_interp(verbose, nav_time_d - 1, nav_altitude - 1, nnav, time_d, &altitude, &j, &error);
						if (interp_status == MB_SUCCESS)
							interp_status &=
							    mb_linear_interp(verbose, nav_time_d - 1, nav_speed - 1, nnav, time_d, &speed, &j, &error);

						/* only output if interpolation of nav etc has worked */
						if (interp_status == MB_SUCCESS) {

							/* calculate course made good and distance */
							mb_coor_scale(verbose, navlat, &mtodeglon, &mtodeglat);
							// const double headingx = sin(DTR * heading);
							// const double headingy = cos(DTR * heading);
							if (first) {
								time_interval = 0.0;
								course = heading;
								speed_made_good = 0.0;
								// course_old = heading;
								// speed_made_good_old = speed;
								distance = 0.0;
							}
							else {
								time_interval = time_d - time_d_old;
								const double dx = (navlon - navlon_old) / mtodeglon;
								const double dy = (navlat - navlat_old) / mtodeglat;
								distance = sqrt(dx * dx + dy * dy);
								if (distance > 0.0)
									course = RTD * atan2(dx / distance, dy / distance);
								else
									course = course_old;
								if (course < 0.0)
									course = course + 360.0;
								if (time_interval > 0.0)
									speed_made_good = 3.6 * distance / time_interval;
								else
									speed_made_good = speed_made_good_old;
							}
							distance_total += 0.001 * distance;

							/* reset old values */
							navlon_old = navlon;
							navlat_old = navlat;
							course_old = course;
							speed_made_good_old = speed_made_good;
							time_d_old = time_d;

							/* now loop over list of output parameters */
							ctd_count++;
							ctd_count_tot++;
							if (nctd % decimate == 0)
								for (int i = 0; i < n_list; i++) {
									switch (list[i]) {
									case '/': /* Inverts next simple value */
										invert_next_value = true;
										break;
									case '-': /* Flip sign on next simple value */
										signflip_next_value = true;
										break;
									case '^': /* use mblist definitions of CcSsTt */
										mblist_next_value = true;
										break;
									case '1': /* Sensor 1 - volts */
										printsimplevalue(T, verbose, sensor1[ictd], 0, 3, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case '2': /* Sensor 2 - volts */
										printsimplevalue(T, verbose, sensor2[ictd], 0, 3, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case '3': /* Sensor 3 - volts */
										printsimplevalue(T, verbose, sensor3[ictd], 0, 3, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case '4': /* Sensor 4 - volts */
										printsimplevalue(T, verbose, sensor4[ictd], 0, 3, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case '5': /* Sensor 5 - volts */
										printsimplevalue(T, verbose, sensor5[ictd], 0, 3, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case '6': /* Sensor 6 - volts */
										printsimplevalue(T, verbose, sensor6[ictd], 0, 3, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case '7': /* Sensor 7 - volts */
										printsimplevalue(T, verbose, sensor7[ictd], 0, 3, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case '8': /* Sensor 8 - volts */
										printsimplevalue(T, verbose, sensor8[ictd], 0, 3, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case 'C': /* Conductivity or Sonar altitude (m) */
										if (!mblist_next_value)
											printsimplevalue(T, verbose, conductivity, 0, 5, ascii, &invert_next_value,
											                 &signflip_next_value, &error);
										else {
											printsimplevalue(T, verbose, altitude, 0, 3, ascii, &invert_next_value,
											                 &signflip_next_value, &error);
											mblist_next_value = false;
										}
										break;
									case 'c': /* Temperature or sonar transducer depth (m) */
										if (!mblist_next_value)
											printsimplevalue(T, verbose, temperature, 0, 5, ascii, &invert_next_value,
											                 &signflip_next_value, &error);
										else {
											printsimplevalue(T, verbose, sensordepth, 0, 3, ascii, &invert_next_value,
											                 &signflip_next_value, &error);
											mblist_next_value = false;
										}
										break;
									case 'H': /* heading */
										printsimplevalue(T, verbose, heading, 6, 2, ascii, &invert_next_value, &signflip_next_value,
										                 &error);
										break;
									case 'h': /* course */
										printsimplevalue(T, verbose, course, 6, 2, ascii, &invert_next_value, &signflip_next_value,
										                 &error);
										break;
									case 'J': /* time string */
									{
										mb_get_jtime(verbose, time_i, time_j);
										const double seconds = time_i[5] + 0.000001 * time_i[6];
										if (ascii) {
											mb_gmt_text_put(T, "%.4d %.3d %.2d %.2d %9.6f", time_j[0], time_j[1], time_i[3], time_i[4],
											       seconds);
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
									}
									case 'j': /* time string */
									{
										mb_get_jtime(verbose, time_i, time_j);
										const double seconds = time_i[5] + 0.000001 * time_i[6];
										if (ascii) {
											mb_gmt_text_put(T, "%.4d %.3d %.4d %9.6f", time_j[0], time_j[1], time_j[2], seconds);
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
									}
									case 'L': /* along-track distance (km) */
										printsimplevalue(T, verbose, distance_total, 7, 3, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case 'l': /* along-track distance (m) */
										printsimplevalue(T, verbose, 1000.0 * distance_total, 7, 3, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case 'M': /* Decimal unix seconds since
									        1/1/70 00:00:00 */
										printsimplevalue(T, verbose, time_d, 0, 6, ascii, &invert_next_value, &signflip_next_value,
										                 &error);
										break;
									case 'm': /* time in decimal seconds since
									        first record */
									{
										if (first_m) {
											time_d_ref = time_d;
											first_m = false;
										}
										const double b = time_d - time_d_ref;
										printsimplevalue(T, verbose, b, 0, 6, ascii, &invert_next_value, &signflip_next_value,
										                 &error);
										break;
									}
									case 'P': /* potential temperature (degrees) */
										/* approximation taken from http://mason.gmu.edu/~bklinger/seawater.pdf
										  on 4/25/2012 - to be replaced by a better calculation at some point */
										potentialtemperature =
										    temperature -
										    0.04 * (1.0 + 0.185 * temperature + 0.35 * (salinity - 35.0)) *
										        (sensordepth / 1000.0) -
										    0.0075 * (1.0 - temperature / 30.0) * (sensordepth * sensordepth / 1000000.0);
										printsimplevalue(T, verbose, potentialtemperature, 0, 5, ascii, &invert_next_value,
										                 &signflip_next_value, &error);
										break;
									case 'S': /* salinity or speed */
										if (!mblist_next_value)
											printsimplevalue(T, verbose, salinity, 0, 5, ascii, &invert_next_value,
											                 &signflip_next_value, &error);
										else {
											printsimplevalue(T, verbose, speed, 5, 2, ascii, &invert_next_value,
											                 &signflip_next_value, &error);
											mblist_next_value = false;
										}
										break;
									case 's': /* speed made good */
										if (!mblist_next_value)
											printsimplevalue(T, verbose, soundspeed, 0, 3, ascii, &invert_next_value,
											                 &signflip_next_value, &error);
										else {
											printsimplevalue(T, verbose, speed_made_good, 5, 2, ascii, &invert_next_value,
											                 &signflip_next_value, &error);
											mblist_next_value = false;
										}
										break;
									case 'T': /* yyyy/mm/dd/hh/mm/ss time string */
									{
										const double seconds = time_i[5] + 1e-6 * time_i[6];
										if (ascii)
											mb_gmt_text_put(T, "%.4d/%.2d/%.2d/%.2d/%.2d/%9.6f", time_i[0], time_i[1], time_i[2], time_i[3],
											       time_i[4], seconds);
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
									}
									case 't': /* yyyy mm dd hh mm ss time string */
									{
										const double seconds = time_i[5] + 1e-6 * time_i[6];
										if (ascii)
											mb_gmt_text_put(T, "%.4d %.2d %.2d %.2d %.2d %9.6f", time_i[0], time_i[1], time_i[2], time_i[3],
											       time_i[4], seconds);
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
									}
									case 'U': /* unix time in seconds since 1/1/70 00:00:00 */
										time_u = (time_t)time_d;
										if (ascii)
											mb_gmt_text_put(T, "%lld", (long long)time_u);
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
											mb_gmt_text_put(T, "%lld", (long long)(time_u - time_u_ref));
										else {
											const double b = time_u - time_u_ref;
											fwrite(&b, sizeof(double), 1, stdout);
										}
										break;
									case 'V': /* time in seconds since last value */
									case 'v':
										if (ascii) {
											if (fabs(time_interval) > 100.)
												mb_gmt_text_put(T, "%g", time_interval);
											else
												mb_gmt_text_put(T, "%7.3f", time_interval);
										}
										else {
											fwrite(&time_interval, sizeof(double), 1, stdout);
										}
										break;
									case 'X': /* longitude decimal degrees */
										dlon = navlon;
										printsimplevalue(T, verbose, dlon, 11, 6, ascii, &invert_next_value, &signflip_next_value,
										                 &error);
										break;
									case 'x': /* longitude degress + decimal minutes */
										dlon = navlon;
										if (dlon < 0.0) {
											hemi = 'W';
											dlon = -dlon;
										}
										else
											hemi = 'E';
										degrees = (int)dlon;
										minutes = 60.0 * (dlon - degrees);
										if (ascii) {
											mb_gmt_text_put(T, "%3d %8.5f%c", degrees, minutes, hemi);
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
										dlat = navlat;
										printsimplevalue(T, verbose, dlat, 11, 6, ascii, &invert_next_value, &signflip_next_value,
										                 &error);
										break;
									case 'y': /* latitude degrees + decimal minutes */
										dlat = navlat;
										if (dlat < 0.0) {
											hemi = 'S';
											dlat = -dlat;
										}
										else
											hemi = 'N';
										degrees = (int)dlat;
										minutes = 60.0 * (dlat - degrees);
										if (ascii) {
											mb_gmt_text_put(T, "%3d %8.5f%c", degrees, minutes, hemi);
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
									default:
										if (ascii)
											mb_gmt_text_put(T, "<Invalid Option: %c>", list[i]);
										break;
									}
									if (ascii) {
										if (i < (n_list - 1))
											mb_gmt_text_put(T, "%s", delimiter);
										else
											mb_gmt_text_put(T, "\n");
									}
								}
							first = false;
						}
					}
				}
			}

			/* else if survey data ignore */
			else if (error <= MB_ERROR_NO_ERROR && kind == MB_DATA_DATA) {
			}
		}

		/* close the swath file */
		status &= mb_close(verbose, &mbio_ptr, &error);

		/* output info */
		if (verbose >= 1) {
			fprintf(stderr, "%d CTD records\n", ctd_count);
		}

		/* figure out whether and what to read next */
		if (read_datalist) {
			if (mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight, &error) == MB_SUCCESS)
				read_data = true;
			else
				read_data = false;
		}
		else {
			read_data = false;
		}
	}  // end loop over files in list

	if (read_datalist)
		mb_datalist_close(verbose, &datalist, &error);

	/* output info */
	if (verbose >= 1) {
		fprintf(stderr, "\nTotal %d CTD records\n", ctd_count_tot);
	}

	if (nnav > 0) {
		status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&nav_time_d, &error);
		status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&nav_lon, &error);
		status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&nav_lat, &error);
		status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&nav_speed, &error);
		status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&nav_sensordepth, &error);
		status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&nav_heading, &error);
		status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&nav_altitude, &error);
	}

	/* check memory */
	if (verbose >= 4)
		status &= mb_memory_list(verbose, &error);

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
		fprintf(stderr, "dbg2  Ending status:\n");
		fprintf(stderr, "dbg2       status:  %d\n", status);
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
