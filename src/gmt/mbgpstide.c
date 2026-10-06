/*--------------------------------------------------------------------
 *    The MB-system:	mbgpstide.c	2018-05-23
 *
 *    Copyright (c) 2020-2025 by
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
 *    This program mbgpstide was created by:
 *    Gordon J. Keith
 *      CSIRO Marine Research
 *      Castray Esplanade
 *      Battery Point TAS 7000
 *      Australia
 *
 *--------------------------------------------------------------------*/
/*
 * mbgpstide generates tide files from the GPS altitude data recorded in
 * the input files.
 *
 * Input (-I) may be a single data file or a datalist. The format of the input file may be specified
 * using the -F option. Default is -Idatalist.mb-1.
 *
 * Output is either to a file specified by -O ("-" for stdout) or to <file>.gps.tde where <file> is
 * the name of the input data file. The -S option specifies that the <file>.gps.tde will not be generated
 * if it already exists. The format of the tide file may be specified with -A where 1 is
 * a two column format, seconds since 1970-01-01 and tide height; and two is a seven column format,
 * year, month, day, hour, minute, second and tide height. Default is -A2.
 *
 * The -Dinterval indicates the time interval in seconds over which the tide values will be averaged to get a value.
 * The default is 300 seconds (5 minutes).
 *
 * -M will cause the program to set tide processing on for the input data file using the output file generated.
 *
 * -Roffset adds a constant offset to the tide value.
 *
 * -Tgrid takes a geoid difference grid and adds the offset for each location in the file.
 * The geoid difference grid is in GMT format. For example
 *		wget  http://earth-info.nga.mil/GandG/wgs84/gravitymod/egm2008/GIS/world_geoid/s45e135.zip
 *		unzip s45e135.zip
 *		grdconvert s45e135/s45e135/ s45e135.grd
 *		mbgpstide -Ts45e135.grd
 *
 * -Usource specifies the source of GPS elipsoid height. 0 = Simrad Height telegram or GSF ping height,
 *		1 = GSF ping height + separator.
 *
 * Author:	G. J. Keith
 * Date:	May 29, 2018
 *
 * GMT-module port of src/utilities/mbgpstide.cc: options parsed in parse() from GMT's option list,
 * the program's long options kept through module_kw and its lower-case aliases kept; every Return()
 * a GMT error code. Its results are the program's own tide files.
 */

#define THIS_MODULE_NAME		"mbgpstide"
#define THIS_MODULE_LIB			"mbsystem"
#define THIS_MODULE_PURPOSE		"Generate tide files from GPS altitude data in swath sonar files"
/* Primary input is the swath file or datalist given with -I; writes tide files, and with -O- the tide
 * table through the GMT API (the >D} key). */
#define THIS_MODULE_KEYS		"ID{,>D}"
#define THIS_MODULE_NEEDS		""
#define THIS_MODULE_OPTIONS		"->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "mb_define.h"
#include "mb_format.h"
#include "mb_io.h"
#include "mb_process.h"
#include "mb_status.h"
#include "mb_gmt_text.h"

#ifdef ENABLE_GSF
#include "mbsys_gsf.h"
#endif
#include "mbsys_simrad2.h"
#include "mbsys_simrad3.h"

EXTERN_MSC int GMT_mbgpstide(void *API, int mode, void *args);

/* --- Control structure ---------------------------------------------- */

struct MBGPSTIDE_CTRL {
	int verbose;	/* the program's -V/-v count */
	struct mgt_H { bool active; } H;
	struct mgt_A { bool active; int tideformat; } A;
	struct mgt_D { bool active; double interval; } D;
	struct mgt_F { bool active; int format; } F;
	struct mgt_I { bool active; char *inputfile; } I;
	struct mgt_M { bool active; } M;                            /* mbprocess_update */
	struct mgt_O { bool active; char tide_file[MB_PATH_MAXLINE+10]; } O;
	struct mgt_R { bool active; double tide_offset; } R;
	struct mgt_S { bool active; } S;                            /* skip_existing */
	struct mgt_T { bool active; char geoidgrid[MB_PATH_MAXLINE]; } T;
	struct mgt_U { bool active; int gps_source; } U;
};

static void *New_mbgpstide_Ctrl(struct GMT_CTRL *GMT) {
	struct MBGPSTIDE_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBGPSTIDE_CTRL);
	Ctrl->A.tideformat = 2;
	Ctrl->D.interval = 300.0;
	Ctrl->R.tide_offset = 0.0;
	Ctrl->U.gps_source = 0;
	return Ctrl;
}

static void Free_mbgpstide_Ctrl(struct GMT_CTRL *GMT, struct MBGPSTIDE_CTRL *Ctrl) {
	if (!Ctrl) return;
	if (Ctrl->I.inputfile) free(Ctrl->I.inputfile);
	gmt_M_free(GMT, Ctrl);
}

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'v', "verbose",       "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'A', "tideformat",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'D', "interval",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'F', "format",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'M', "setparameters", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "output",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'R', "offset",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'S', "skipexisting",  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'T', "geoid",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'U', "use",           "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: mbgpstide [-Atideformat -Dinterval -Fformat -Idatalist -M\n"
	    "\t-Ooutput -Roffset -S -Tgeoid -Usource -V -H]\n\n");
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE,
		"MBgpstide generates tide files from the GPS altitude data in swath sonar files.\n"
		"\tEvery option also has the program's lower-case and long forms (--tideformat, --interval,\n"
		"\t--format, --input, --setparameters, --output, --offset, --skipexisting, --geoid, --use,\n"
		"\t--verbose, --help).\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse(struct GMT_CTRL *GMT, struct MBGPSTIDE_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0, n_files = 0;
	struct GMT_OPTION *opt;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case '<':
			Ctrl->I.active = true;
			if (gmt_check_filearg(GMT, '<', opt->arg, GMT_IN, GMT_IS_DATASET)) {
				Ctrl->I.inputfile = strdup(opt->arg); n_files = 1;
			} else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'A':
		case 'a':
			if (sscanf(opt->arg, "%d", &Ctrl->A.tideformat) > 0) {
				if (Ctrl->A.tideformat != 2 && Ctrl->A.tideformat != 5)
					Ctrl->A.tideformat = 1;
				Ctrl->A.active = true;
			} else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'D':
		case 'd':
			if (sscanf(opt->arg, "%lf", &Ctrl->D.interval) > 0) Ctrl->D.active = true; else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'F':
		case 'f':
			if (sscanf(opt->arg, "%d", &Ctrl->F.format) > 0) Ctrl->F.active = true; else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'I':
		case 'i':
			if (!gmt_access(GMT, opt->arg, R_OK)) {
				Ctrl->I.inputfile = strdup(opt->arg); Ctrl->I.active = true; n_files = 1;
			} else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'M':
		case 'm':
			Ctrl->M.active = true;
			break;
		case 'O':
		case 'o':
			sscanf(opt->arg, "%1033s", Ctrl->O.tide_file);
			Ctrl->O.active = true;
			break;
		case 'R':
		case 'r':
			if (sscanf(opt->arg, "%lf", &Ctrl->R.tide_offset) > 0) Ctrl->R.active = true; else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'S':
		case 's':
			Ctrl->S.active = true;
			break;
		case 'T':
		case 't':
			sscanf(opt->arg, "%1023s", Ctrl->T.geoidgrid);
			Ctrl->T.active = true;
			break;
		case 'U':
		case 'u':
			if (sscanf(opt->arg, "%d", &Ctrl->U.gps_source) > 0) Ctrl->U.active = true; else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'V':
		case 'v':
			Ctrl->verbose++;
			break;
		case 'H':
		case 'h':
			Ctrl->H.active = true;
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}

	(void)n_files;
	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code)  { gmt_M_free_options(mode); return code; }
#define Return(code)   { Free_mbgpstide_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }

/*--------------------------------------------------------------------*/
int GMT_mbgpstide(void *V_API, int mode, void *args) {
	char program_name[] = "mbgpstide";

	struct MBGPSTIDE_CTRL *Ctrl = NULL;
	struct GMT_CTRL       *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION     *options = NULL;
	struct GMTAPI_CTRL    *API = gmt_get_api_ptr(V_API);

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program (on datalist.mb-1) */
	{ int uerr = gmt_report_usage(API, options, 1, usage); if (uerr != GMT_NOERROR) bailout(uerr); }

	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	Ctrl = New_mbgpstide_Ctrl(GMT);
	{ int perr = parse(GMT, Ctrl, options); if (perr) Return(perr); }
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	int verbose = Ctrl->verbose;
	int format, pings, lonflip;
	double bounds[4];
	int btime_i[7], etime_i[7];
	double speedmin, timegap;
	int status = mb_defaults(verbose, &format, &pings, &lonflip, bounds,
	                         btime_i, etime_i, &speedmin, &timegap);

	if (Ctrl->F.active) format = Ctrl->F.format;

	char read_file[MB_PATH_MAXLINE];
	strcpy(read_file, Ctrl->I.active ? Ctrl->I.inputfile : "datalist.mb-1");

	int tideformat = Ctrl->A.tideformat;
	double interval = Ctrl->D.interval;
	bool mbprocess_update = Ctrl->M.active;
	bool file_output = Ctrl->O.active;
	char tide_file[MB_PATH_MAXLINE+10];
	if (file_output)
		strcpy(tide_file, Ctrl->O.tide_file);
	else
		tide_file[0] = '\0';
	double tide_offset = Ctrl->R.tide_offset;
	bool skip_existing = Ctrl->S.active;
	bool geoid_set = Ctrl->T.active;
	char geoidgrid[MB_PATH_MAXLINE];
	strcpy(geoidgrid, Ctrl->T.active ? Ctrl->T.geoidgrid : "");
	int gps_source = Ctrl->U.gps_source;

	if (verbose == 1) {
		fprintf(stderr, "\nProgram %s\n", program_name);
		fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
	}

	if (verbose >= 2) {
		GMT_Report(API, GMT_MSG_DEBUG, "\ndbg2  Program <%s>\n", program_name);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2  MB-system Version %s\n", MB_VERSION);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2  Control Parameters:\n");
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       verbose:              %d\n", verbose);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       format:               %d\n", format);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       pings:                %d\n", pings);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       lonflip:              %d\n", lonflip);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       bounds[0]:            %f\n", bounds[0]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       bounds[1]:            %f\n", bounds[1]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       bounds[2]:            %f\n", bounds[2]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       bounds[3]:            %f\n", bounds[3]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       btime_i[0]:           %d\n", btime_i[0]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       btime_i[1]:           %d\n", btime_i[1]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       btime_i[2]:           %d\n", btime_i[2]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       btime_i[3]:           %d\n", btime_i[3]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       btime_i[4]:           %d\n", btime_i[4]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       btime_i[5]:           %d\n", btime_i[5]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       btime_i[6]:           %d\n", btime_i[6]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       etime_i[0]:           %d\n", etime_i[0]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       etime_i[1]:           %d\n", etime_i[1]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       etime_i[2]:           %d\n", etime_i[2]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       etime_i[3]:           %d\n", etime_i[3]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       etime_i[4]:           %d\n", etime_i[4]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       etime_i[5]:           %d\n", etime_i[5]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       etime_i[6]:           %d\n", etime_i[6]);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       speedmin:             %f\n", speedmin);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       timegap:              %f\n", timegap);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       read_file:            %s\n", read_file);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       tideformat:           %d\n", tideformat);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       interval:             %f\n", interval);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       mbprocess_update:     %d\n", mbprocess_update);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       skip_existing:        %d\n", skip_existing);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       file_output:          %d\n", file_output);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       tide_file:            %s\n", tide_file);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       tide_offset:          %f\n", tide_offset);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       geoid_set:            %d\n", geoid_set);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       geoidgrid:            %s\n", geoidgrid);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       gps_source:           %d\n", gps_source);
	}

	struct MB_GMT_TEXT *ofp = NULL;	/* the tide table: a file, or with -O- the GMT API */
	int error = MB_ERROR_NO_ERROR;

	/* If a single output file is specified, open and initialise it */
	if (file_output) {
		if (strcmp(tide_file, "-") == 0) {
			if ((ofp = mb_gmt_text_begin(GMT, options)) == NULL) Return(API->error);
		} else {
			if ((ofp = mb_gmt_text_file(GMT, tide_file)) == NULL) {
				GMT_Report(API, GMT_MSG_NORMAL, "Unable to open tide output file <%s>\n", tide_file);
				GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
				Return(GMT_ERROR_ON_FOPEN);
			}
		}

		if (tideformat == 5) {
			mb_gmt_text_put(ofp, "--------\n");
		} else {
			mb_gmt_text_put(ofp, "# Tide model generated by program %s\n", program_name);
			mb_gmt_text_put(ofp, "# MB-System Version: %s\n", MB_VERSION);
			mb_gmt_text_put(ofp, "#    mbgpstide");
			if (Ctrl->A.active) mb_gmt_text_put(ofp, " -A%d", tideformat);
			if (Ctrl->D.active) mb_gmt_text_put(ofp, " -D%f", interval);
			if (Ctrl->F.active) mb_gmt_text_put(ofp, " -F%d", format);
			if (Ctrl->I.active) mb_gmt_text_put(ofp, " -I%s", read_file);
			if (Ctrl->M.active) mb_gmt_text_put(ofp, " -M");
			if (Ctrl->O.active) mb_gmt_text_put(ofp, " -O%s", tide_file);
			if (Ctrl->R.active) mb_gmt_text_put(ofp, " -R%f", tide_offset);
			if (Ctrl->S.active) mb_gmt_text_put(ofp, " -S");
			if (Ctrl->T.active) mb_gmt_text_put(ofp, " -T%s", geoidgrid);
			if (Ctrl->U.active) mb_gmt_text_put(ofp, " -U%d", gps_source);
			mb_gmt_text_put(ofp, " \n");
			char user[256], host[256], date[32];
			mb_user_host_date(verbose, user, host, date, &error);
			mb_gmt_text_put(ofp, "# Run by user <%s> on cpu <%s> at <%s>\n", user, host, date);
		}
	}

	/* get format if required */
	if (format == 0)
		mb_get_format(verbose, read_file, NULL, &format, &error);

	/* determine whether to read one file or a list of files */
	const bool read_datalist = format < 0;
	bool read_data;
	void *datalist = NULL;
	mb_path file;
	mb_path dfile;
	double file_weight;

	/* open file list */
	if (read_datalist) {
		const int look_processed = MB_DATALIST_LOOK_UNSET;
		if (mb_datalist_open(verbose, &datalist, read_file, look_processed, &error) != MB_SUCCESS) {
			GMT_Report(API, GMT_MSG_NORMAL, "Unable to open data list file: %s\n", read_file);
			GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
			if (file_output && ofp != NULL) mb_gmt_text_end(ofp);
			Return(GMT_ERROR_ON_FOPEN);
		}
		read_data = mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight, &error) == MB_SUCCESS;
	} else {
		strcpy(file, read_file);
		read_data = true;
	}

	bool read_geoid = false;
	bool have_height = false;
	int count_tide = 0;
	int ngood = 0;
	double sum_tide = 0.0;
	double this_interval = 0.0;

	/* loop over all files to be read */
	while (read_data) {

		/* Figure out if the file needs a tide model - don't generate a new tide
		   model if one was made previously and is up to date AND the
		   appropriate request has been made */
		bool proceed = true;
		if (!file_output) {
			snprintf(tide_file, sizeof(tide_file), "%s.gps.tde", file);
			if (skip_existing) {
				struct stat file_status;
				int fstat = stat(file, &file_status);
				int input_modtime = 0;
				int input_size = 0;
				if (fstat == 0 && (file_status.st_mode & S_IFMT) != S_IFDIR) {
					input_modtime = file_status.st_mtime;
					input_size = file_status.st_size;
				}
				fstat = stat(tide_file, &file_status);
				int output_size = 0;
				int output_modtime = 0;
				if (fstat == 0 && (file_status.st_mode & S_IFMT) != S_IFDIR) {
					output_modtime = file_status.st_mtime;
					output_size = file_status.st_size;
				}
				if (output_modtime > input_modtime && input_size > 0 && output_size > 0) {
					proceed = false;
				}
			}
		}

		/* skip the file */
		if (!proceed) {
			fprintf(stderr, "\n---------------------------------------\n\nProcessing tides for %s\n\n", file);
		}

		/* generate the tide model */
		else {
			/* if one output file per input file then open and initialise it */
			if (!file_output) {
				if ((ofp = mb_gmt_text_file(GMT, tide_file)) == NULL) {
					error = MB_ERROR_OPEN_FAIL;
					GMT_Report(API, GMT_MSG_NORMAL, "Unable to open tide output file <%s>\n", tide_file);
					GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
					if (read_datalist) mb_datalist_close(verbose, &datalist, &error);
					Return(GMT_ERROR_ON_FOPEN);
				}

				if (tideformat == 5) {
					mb_gmt_text_put(ofp, "--------\n");
				} else {
					mb_gmt_text_put(ofp, "# Tide model generated by program %s\n", program_name);
					mb_gmt_text_put(ofp, "# MB-System Version: %s\n", MB_VERSION);
					mb_gmt_text_put(ofp, "#    mbgpstide");
					if (Ctrl->A.active) mb_gmt_text_put(ofp, " -A%d", tideformat);
					if (Ctrl->D.active) mb_gmt_text_put(ofp, " -D%f", interval);
					if (Ctrl->F.active) mb_gmt_text_put(ofp, " -F%d", format);
					if (Ctrl->I.active) mb_gmt_text_put(ofp, " -I%s", read_file);
					if (Ctrl->M.active) mb_gmt_text_put(ofp, " -M");
					if (Ctrl->R.active) mb_gmt_text_put(ofp, " -R%f", tide_offset);
					if (Ctrl->S.active) mb_gmt_text_put(ofp, " -S");
					if (Ctrl->T.active) mb_gmt_text_put(ofp, " -T%s", geoidgrid);
					if (Ctrl->U.active) mb_gmt_text_put(ofp, " -U%d", gps_source);
					mb_gmt_text_put(ofp, " \n");
					char user[256], host[256], date[32];
					status = mb_user_host_date(verbose, user, host, date, &error);
					mb_gmt_text_put(ofp, "# Run by user <%s> on cpu <%s> at <%s>\n", user, host, date);
				}
			}

			fprintf(stderr, "\n---------------------------------------\n\nProcessing tides for %s\n\n", file);

			mb_path swath_file;
			strcpy(swath_file, file);

			void *mbio_ptr = NULL;
			double btime_d;
			double etime_d;
			int beams_bath;
			int beams_amp;
			int pixels_ss;

			if (mb_read_init(verbose, file, format, pings, lonflip, bounds, btime_i, etime_i, speedmin, timegap,
			                 &mbio_ptr, &btime_d, &etime_d, &beams_bath, &beams_amp, &pixels_ss, &error) !=
			    MB_SUCCESS) {
				char *message;
				mb_error(verbose, error, &message);
				GMT_Report(API, GMT_MSG_NORMAL, "MBIO Error returned from function <mb_read_init>:\n%s\n", message);
				GMT_Report(API, GMT_MSG_NORMAL, "Multibeam File <%s> not initialized for reading\n", file);
				GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
				if (!file_output && ofp != NULL) { mb_gmt_text_end(ofp); ofp = NULL; }
				if (read_datalist) mb_datalist_close(verbose, &datalist, &error);
				if (file_output && ofp != NULL) mb_gmt_text_end(ofp);
				Return(GMT_RUNTIME_ERROR);
			}

			/* allocate memory for data arrays */
			char *beamflag = NULL;
			if (error == MB_ERROR_NO_ERROR)
				mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(char), (void **)&beamflag, &error);
			double *bath = NULL;
			if (error == MB_ERROR_NO_ERROR)
				mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bath, &error);
			double *amp = NULL;
			if (error == MB_ERROR_NO_ERROR)
				mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_AMPLITUDE, sizeof(double), (void **)&amp, &error);
			double *bathacrosstrack = NULL;
			if (error == MB_ERROR_NO_ERROR)
				mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathacrosstrack, &error);
			double *bathalongtrack = NULL;
			if (error == MB_ERROR_NO_ERROR)
				mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathalongtrack, &error);
			double *ss = NULL;
			if (error == MB_ERROR_NO_ERROR)
				mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ss, &error);
			double *ssacrosstrack = NULL;
			if (error == MB_ERROR_NO_ERROR)
				mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssacrosstrack, &error);
			double *ssalongtrack = NULL;
			if (error == MB_ERROR_NO_ERROR)
				mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssalongtrack, &error);

			/* if error initializing memory then quit */
			if (error != MB_ERROR_NO_ERROR) {
				char *message;
				mb_error(verbose, error, &message);
				GMT_Report(API, GMT_MSG_NORMAL, "MBIO Error allocating data arrays:\n%s\n", message);
				GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
				mb_close(verbose, &mbio_ptr, &error);
				if (!file_output && ofp != NULL) { mb_gmt_text_end(ofp); ofp = NULL; }
				if (read_datalist) mb_datalist_close(verbose, &datalist, &error);
				if (file_output && ofp != NULL) mb_gmt_text_end(ofp);
				Return(GMT_MEMORY_ERROR);
			}

			FILE *tfp = NULL;
			double tidelon;
			double tidelat;
			double geoid_time = 0.0;
			double geoid_offset = 0.0;

			/* get geoid corrections */
			if (geoid_set) {
				char nav_file[MB_PATH_MAXLINE+10];
				snprintf(nav_file, sizeof(nav_file), "%s.fnv", swath_file);
				struct stat file_status;
				const int fstat = stat(nav_file, &file_status);
				char line[2*MB_PATH_MAXLINE+100] = "";
				if (fstat == 0 && (file_status.st_mode & S_IFMT) != S_IFDIR) {
					snprintf(line, sizeof(line), "awk '{ print $8 \" \" $9 \" \" $7 }' %s | grdtrack -G%s", nav_file, geoidgrid);
				} else {
					snprintf(line, sizeof(line), "mblist -F%d -I%s -OXYU | grdtrack -G%s", format, file, geoidgrid);
				}
				if ((tfp = popen(line, "r")) != NULL) {
					read_geoid = true;
					if (EOF == fscanf(tfp, "%lf %lf %lf %lf\n", &tidelat, &tidelon, &geoid_time, &geoid_offset)) {
						pclose(tfp);
						GMT_Report(API, GMT_MSG_NORMAL, "Error - Geoid model returned no data\n");
						mb_close(verbose, &mbio_ptr, &error);
						if (!file_output && ofp != NULL) { mb_gmt_text_end(ofp); ofp = NULL; }
						if (read_datalist) mb_datalist_close(verbose, &datalist, &error);
						if (file_output && ofp != NULL) mb_gmt_text_end(ofp);
						Return(GMT_RUNTIME_ERROR);
					}
				} else {
					GMT_Report(API, GMT_MSG_NORMAL, "Unable to read geoid model\n");
					mb_close(verbose, &mbio_ptr, &error);
					if (!file_output && ofp != NULL) { mb_gmt_text_end(ofp); ofp = NULL; }
					if (read_datalist) mb_datalist_close(verbose, &datalist, &error);
					if (file_output && ofp != NULL) mb_gmt_text_end(ofp);
					Return(GMT_RUNTIME_ERROR);
				}
			}

			/* read and use data */
			int nread = 0;
			void *store_ptr = NULL;
			int kind;
			double time_d;
			double navlon;
			double navlat;
			double speed;
			double heading;
			double next_interval = 0.0;
			int time_i[7];

			while (error <= MB_ERROR_NO_ERROR) {
				/* reset error */
				error = MB_ERROR_NO_ERROR;

				/* read next data record */
				double distance;
				double altitude;
				double sensordepth;
				char comment[MB_COMMENT_MAXLINE];
				status = mb_get_all(verbose, mbio_ptr, &store_ptr, &kind, time_i, &time_d, &navlon, &navlat, &speed, &heading,
				                    &distance, &altitude, &sensordepth, &beams_bath, &beams_amp, &pixels_ss, beamflag, bath, amp,
				                    bathacrosstrack, bathalongtrack, ss, ssacrosstrack, ssalongtrack, comment, &error);

				if (verbose >= 2) {
					GMT_Report(API, GMT_MSG_DEBUG, "\ndbg2  Ping read in program <%s>\n", program_name);
					GMT_Report(API, GMT_MSG_DEBUG, "dbg2       kind:           %d\n", kind);
					GMT_Report(API, GMT_MSG_DEBUG, "dbg2       error:          %d\n", error);
					GMT_Report(API, GMT_MSG_DEBUG, "dbg2       status:         %d\n", status);
				}

				if (error <= MB_ERROR_NO_ERROR && kind == MB_DATA_START) {
					if (verbose >= 2)
						GMT_Report(API, GMT_MSG_DEBUG, "dbg2       Have Installation telegram\n");
				}

				struct mb_io_struct *mb_io_ptr = (struct mb_io_struct *)mbio_ptr;

				double ttime_d = 0.0;
				double height = 0.0;
#ifdef ENABLE_GSF
				if (mb_io_ptr->format == MBF_GSFGENMB) {
					if (error <= MB_ERROR_NO_ERROR && kind == MB_DATA_DATA) {
						ttime_d = time_d;
						struct mbsys_gsf_struct *gsf_ptr = (struct mbsys_gsf_struct *)mb_io_ptr->store_data;
						height = gsf_ptr->records.mb_ping.height;
						if (gps_source == 1) {
							height += gsf_ptr->records.mb_ping.sep;
						}
						have_height = true;
						nread++;
					}
				}
#endif
				/* Read sounder height from height telegram */
				if (error <= MB_ERROR_NO_ERROR && kind == MB_DATA_HEIGHT && gps_source == 0) {

					if (mb_io_ptr->format == MBF_EM300MBA || mb_io_ptr->format == MBF_EM300RAW) {
						struct mbsys_simrad2_struct *simrad2_ptr = (struct mbsys_simrad2_struct *)mb_io_ptr->store_data;
						height = simrad2_ptr->hgt_height * 0.01;
						time_i[0] = simrad2_ptr->hgt_date / 10000;
						time_i[1] = (simrad2_ptr->hgt_date % 10000) / 100;
						time_i[2] = simrad2_ptr->hgt_date % 100;
						time_i[3] = simrad2_ptr->hgt_msec / 3600000;
						time_i[4] = (simrad2_ptr->hgt_msec % 3600000) / 60000;
						time_i[5] = (simrad2_ptr->hgt_msec % 60000) / 1000;
						time_i[6] = (simrad2_ptr->hgt_msec % 1000) * 1000;
						mb_get_time(verbose, time_i, &ttime_d);
						have_height = true;

					} else if (mb_io_ptr->format == MBF_EM710MBA || mb_io_ptr->format == MBF_EM710RAW) {
						struct mbsys_simrad3_struct *simrad3_ptr = (struct mbsys_simrad3_struct *)mb_io_ptr->store_data;
						height = simrad3_ptr->hgt_height * 0.01;
						time_i[0] = simrad3_ptr->hgt_date / 10000;
						time_i[1] = (simrad3_ptr->hgt_date % 10000) / 100;
						time_i[2] = simrad3_ptr->hgt_date % 100;
						time_i[3] = simrad3_ptr->hgt_msec / 3600000;
						time_i[4] = (simrad3_ptr->hgt_msec % 3600000) / 60000;
						time_i[5] = (simrad3_ptr->hgt_msec % 60000) / 1000;
						time_i[6] = (simrad3_ptr->hgt_msec % 1000) * 1000;
						mb_get_time(verbose, time_i, &ttime_d);
						have_height = true;
					}

					/* increment counter */
					nread++;
				}

				if (have_height) {

					if (ttime_d > next_interval || (!file_output && error == MB_ERROR_EOF)) {
						if (count_tide > 0) {
							ngood++;
							const double atide = sum_tide / count_tide;
							if (tideformat == 1) {
								mb_gmt_text_put(ofp, "%.3f %9.4f\n", this_interval, atide);

							} else if (tideformat == 5) { /* CARIS */
								mb_get_date(verbose, this_interval, time_i);
								mb_gmt_text_put(ofp, "%4.4d/%2.2d/%2.2d %2.2d:%2.2d:%.3f  %.6f\n",
								        time_i[0], time_i[1], time_i[2], time_i[3], time_i[4],
								        time_i[5] + time_i[6] * 0.000001, atide);

							} else {
								mb_get_date(verbose, this_interval, time_i);
								mb_gmt_text_put(ofp, "%4.4d %2.2d %2.2d %2.2d %2.2d %2.2d %9.4f\n",
								        time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5],
								        atide);
							}
						}
						count_tide = 0;
						sum_tide = 0.0;
						if (interval == 0) {
							this_interval = ttime_d;
						} else {
							this_interval = (int)(ttime_d / interval + 0.5) * interval;
							next_interval = this_interval + interval / 2;
						}
					}

					/* find the first geoid offset along the track after the height time */
					while (read_geoid && geoid_time < ttime_d) {
						if (EOF == fscanf(tfp, "%lf %lf %lf %lf\n", &tidelat, &tidelon, &geoid_time, &geoid_offset)) {
							pclose(tfp);
							read_geoid = false;
						}
						if (verbose >= 2)
							GMT_Report(API, GMT_MSG_DEBUG, "tide %.0f, geoid %.0f, goff %.3f, %.4f %.4f\n",
							        ttime_d, geoid_time, geoid_offset, tidelat, tidelon);
					}

					count_tide++;
					sum_tide += height + tide_offset - geoid_offset;
					have_height = false;
					if (verbose >= 1)
						fprintf(stderr, "time %f, interval %f, count %d, sum %.2f, tide %.2f, offset %.2f, geoid %.2f\n",
						        ttime_d, next_interval, count_tide, sum_tide, height, tide_offset, geoid_offset);
				}
			}

			/* close the swath file */
			status &= mb_close(verbose, &mbio_ptr, &error);

			if (read_geoid) {
				pclose(tfp);
				read_geoid = false;
			}

			if (!file_output) {
				mb_gmt_text_end(ofp);
				ofp = NULL;
			}

			fprintf(stderr, "%d records read from %s\n", nread, file);

			/* set mbprocess usage of tide file */
			if (mbprocess_update && ngood > 0) {
				status &= mb_pr_update_tide(verbose, swath_file, MBP_TIDE_ON, tide_file, tideformat, &error);
				fprintf(stderr, "MBprocess set to apply tide correction to %s\n", swath_file);
			}
		}

		/* figure out whether and what to read next */
		if (read_datalist) {
			read_data = mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight, &error) == MB_SUCCESS;
		} else {
			read_data = false;
		}
	}  /* end loop over files in list */

	if (read_datalist) {
		mb_datalist_close(verbose, &datalist, &error);
	}

	/* if single output file specified, then finalise and close it */
	if (file_output) {
		if (count_tide > 0) {
			const double atide = sum_tide / count_tide;
			int time_i[7];
			if (tideformat == 1) {
				mb_gmt_text_put(ofp, "%.3f %9.4f\n", this_interval, atide);

			} else if (tideformat == 5) { /* CARIS */
				mb_get_date(verbose, this_interval, time_i);
				mb_gmt_text_put(ofp, "%4.4d/%2.2d/%2.2d %2.2d:%2.2d:%.3f  %.6f\n",
				        time_i[0], time_i[1], time_i[2], time_i[3], time_i[4],
				        time_i[5] + time_i[6] * 0.000001, atide);

			} else {
				mb_get_date(verbose, this_interval, time_i);
				mb_gmt_text_put(ofp, "%4.4d %2.2d %2.2d %2.2d %2.2d %2.2d %9.4f\n",
				        time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], atide);
			}
		}
		if (ofp != NULL) {
			mb_gmt_text_end(ofp);
		}
	}

	if (verbose >= 4)
		status &= mb_memory_list(verbose, &error);

	if (verbose >= 2) {
		GMT_Report(API, GMT_MSG_DEBUG, "\ndbg2  Program <%s> completed\n", program_name);
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2  Ending status:\n");
		GMT_Report(API, GMT_MSG_DEBUG, "dbg2       status:  %d\n", status);
	}

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
