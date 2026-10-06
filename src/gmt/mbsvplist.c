/*--------------------------------------------------------------------
 *    The MB-system:  mbsvplist.c  1/3/2001
 *
 *    Copyright (c) 2001-2025 by
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
 * This program, mbsvplist, lists all water sound velocity
 * profiles (SVPs) within swath data files. Swath bathymetry is
 * calculated from raw angles and travel times by raytracing
 * through a model of the speed of sound in water. Many swath
 * data formats allow SVPs to be embedded in the data, and
 * often the SVPs used to calculate the data will be included.
 * By default, all unique SVPs encountered are listed to
 * stdout. The SVPs may instead be written to individual files
 * with names FILE_XXX.svp, where FILE is the swath data
 * filename and XXX is the SVP count within the file.  The -D
 * option causes duplicate SVPs to be output. The -P option
 * implies -O, and also causes the parameter file to be modified
 * so that the first svp output for each file becomes the
 * svp used for recalculating bathymetry for that swath file.
 *
 * Author:  D. W. Caress
 * Date:  January 3,  2001
 *
 * GMT-module port of src/utilities/mbsvplist.cc: the getopt_long loop
 * is replaced by the GMT option parser (long options through module_kw,
 * lower-case aliases kept), the listing goes through the GMT API, and main() becomes
 * GMT_mbsvplist(), with every exit() a Return() with a GMT error code.
 */

#define THIS_MODULE_NAME "mbsvplist"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "List water sound velocity profiles embedded in swath data files"
/* Primary input is the swath file or datalist given with -I; profiles,
 * counts and SSV values are listed to stdout. */
#define THIS_MODULE_KEYS "ID{,>D}"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _MSC_VER
#include "unistd_w.h"
#else
#include <unistd.h>
#endif
#include "mb_status.h"
#include "mb_gmt_opts.h"
#include "mb_gmt_text.h"
#include "mb_format.h"
#include "mb_define.h"
#include "mb_process.h"

enum { MBSVPLIST_SVP_NUM_ALLOC = 24 };
typedef enum {
    MBSVPLIST_PRINTMODE_CHANGE = 0,
    MBSVPLIST_PRINTMODE_UNIQUE = 1,
    MBSVPLIST_PRINTMODE_ALL = 2,
} printmode_t;

struct mbsvplist_svp_struct {
  bool time_set;        /* time stamp known */
  bool position_set;    /* position known */
  bool repeat_in_file;  /* repeats a previous svp in the same file */
  bool match_last;      /* repeats the last svp in the same file or the previous file */
  bool depthzero_reset; /* uppermost SVP value set to zero depth */
  double time_d;
  double longitude;
  double latitude;
  double depthzero;
  int n;
  double depth[MB_SVP_MAX];
  double velocity[MB_SVP_MAX];
};

static const char program_name[] = "mbsvplist";
static const char help_message[] =
    "mbsvplist lists all water sound velocity\n"
    "profiles (SVPs) within swath data files. Swath bathymetry is\n"
    "calculated from raw angles and travel times by raytracing\n"
    "through a model of the speed of sound in water. Many swath\n"
    "data formats allow SVPs to be embedded in the data, and\n"
    "often the SVPs used to calculate the data will be included.\n"
    "By default, all unique SVPs encountered are listed to\n"
    "stdout. The SVPs may instead be written to individual files\n"
    "with names FILE_XXX.svp, where FILE is the swath data\n"
    "filename and XXX is the SVP count within the file. The -D\n"
    "option causes duplicate SVPs to be output.\n"
    "The -T option will output a CSV table of svp#, time, longitude, latitude and number of points for SVPs.\n"
    "When the -Nmin_num_pairs option is used, only svps that have at least min_num_pairs svp values will "
    "be output.(This is particularly useful for .xse data where the svp is entered as a single values svp.)";
static const char usage_message_old[] =
    "mbsvplist [-Asource -C -D -Fformat -H -Ifile -Mmode -O -Nmin_num_pairs -P -T -V -Z]";
static const char usage_message[] =
    "mbsvplist\n"
    "\t--bounds=west/east/south/north {-Rwest/east/south/north}\n"
    "\t--counts {-C}\n"
    "\t--duplicates {-D}\n"
    "\t--format=format_id {-Fformat_id}\n"
    "\t--help {-H}\n"
    "\t--input=file {-Ifile}\n"
    "\t--min-num-pairs=min_num_pairs {-Nmin_num_pairs}\n"
    "\t--mode=mode {-Mmode}\n"
    "\t--output {-O}\n"
    "\t--process {-P}\n"
    "\t--source=source {-Asource}\n"
    "\t--ssv {-S}\n"
    "\t--table {-T}\n"
    "\t--verbose {-V}\n"
    "\t--zero-depth {-Z}\n";

/* --- Control structure ---------------------------------------------- */

struct MBSVPLIST_CTRL {
	int verbose;	/* the program's -V/-v count */
	struct mbsl_A { bool active; int svp_source_use; } A;
	struct mbsl_C { bool active; } C;
	struct mbsl_D { bool active; } D;
	struct mbsl_F { bool active; int format; } F;
	struct mbsl_H { bool active; } H;
	struct mbsl_I { bool active; char file[MB_PATH_MAXLINE]; } I;
	struct mbsl_M { bool active; int mode; } M;
	struct mbsl_N { bool active; int min_num_pairs; } N;
	struct mbsl_O { bool active; } O;
	struct mbsl_P { bool active; } P;
	struct mbsl_R { bool active; double bounds[4]; } R;
	struct mbsl_S { bool active; } S;
	struct mbsl_T { bool active; } T;
	struct mbsl_Z { bool active; } Z;
	/* -C -D -M -O -P -S -T switch the output modes on and off against each
	 * other; getopt applied them in command-line order, and so does
	 * parse_mbsvplist(), into this block */
	struct mbsl_mode {
		printmode_t svp_printmode;
		bool output_counts;
		bool ssv_output;
		bool svp_file_output;
		bool svp_setprocess;
		bool output_as_table;
	} mode;
};

static void *New_mbsvplist_Ctrl(struct GMT_CTRL *GMT) {
	struct MBSVPLIST_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBSVPLIST_CTRL);
	Ctrl->A.svp_source_use = -1;
	Ctrl->mode.svp_printmode = MBSVPLIST_PRINTMODE_CHANGE;
	return Ctrl;
}

static void Free_mbsvplist_Ctrl(struct GMT_CTRL *GMT, struct MBSVPLIST_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'v', "verbose",       "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'R', "bounds",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'C', "counts",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'D', "duplicates",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'F', "format",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'N', "min-num-pairs", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'M', "mode",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "output",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'P', "process",       "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'A', "source",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'S', "ssv",           "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'T', "table",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'Z', "zero-depth",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message_old);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n\n", help_message);
	GMT_Message(API, GMT_TIME_NONE,
	            "\t-A Select the SVP source by record kind, or C (CTD) / S (SVP).\n"
	            "\t-C Output the count of unique profiles.\n"
	            "\t-D Include duplicate profiles.\n"
	            "\t-F Input MBIO format.\n"
	            "\t-I Input swath file or datalist [datalist.mb-1].\n"
	            "\t-M Profile printing mode (0 change, 1 unique, 2 all).\n"
	            "\t-N Minimum number of depth/velocity pairs.\n"
	            "\t-O Write profiles to individual FILE_XXX.svp files.\n"
	            "\t-P As -O, and set the first profile for recalculating bathymetry.\n"
	            "\t-R Restrict surface sound velocity output to w/e/s/n.\n"
	            "\t-S Output surface sound velocity values.\n"
	            "\t-T Output a CSV table of the profiles.\n"
	            "\t-Z Force the uppermost profile depth to zero.\n"
	            "\t-H Print help and exit.\n"
	            "\tEvery option also has the program's lower-case and long forms.\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse_mbsvplist(struct GMT_CTRL *GMT, struct MBSVPLIST_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt;
	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case 'V': case 'v':
			Ctrl->verbose++;
			break;
		case 'A': case 'a':
			if (!opt->arg || !opt->arg[0]) { n_errors++; break; }
			Ctrl->A.active = true;
			if (opt->arg[0] == 'C' || opt->arg[0] == 'c') {
				Ctrl->A.svp_source_use = MB_DATA_CTD;
			} else if (opt->arg[0] == 'S' || opt->arg[0] == 's') {
				Ctrl->A.svp_source_use = MB_DATA_VELOCITY_PROFILE;
			} else {
				sscanf(opt->arg, "%d", &Ctrl->A.svp_source_use);
			}
			break;
		case 'C': case 'c':
			Ctrl->C.active = true;
			Ctrl->mode.output_counts = true;
			Ctrl->mode.ssv_output = false;
			break;
		case 'D': case 'd':
			Ctrl->D.active = true;
			Ctrl->mode.svp_printmode = MBSVPLIST_PRINTMODE_ALL;
			break;
		case 'F': case 'f':
			if (opt->arg && sscanf(opt->arg, "%d", &Ctrl->F.format) == 1) Ctrl->F.active = true;
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
		case 'M': case 'm':
			if (opt->arg && sscanf(opt->arg, "%d", &Ctrl->M.mode) == 1) {
				Ctrl->M.active = true;
				Ctrl->mode.svp_printmode = (printmode_t)Ctrl->M.mode;
			}
			else n_errors++;
			break;
		case 'N': case 'n':
			if (opt->arg && sscanf(opt->arg, "%d", &Ctrl->N.min_num_pairs) == 1) Ctrl->N.active = true;
			else n_errors++;
			break;
		case 'O': case 'o':
			Ctrl->O.active = true;
			Ctrl->mode.svp_file_output = true;
			Ctrl->mode.ssv_output = false;
			break;
		case 'P': case 'p':
			Ctrl->P.active = true;
			Ctrl->mode.svp_file_output = true;
			Ctrl->mode.svp_setprocess = true;
			Ctrl->mode.ssv_output = false;
			break;
		case 'R': case 'r':
			if (opt->arg && opt->arg[0]) {
				mb_get_bounds(opt->arg, Ctrl->R.bounds);
				Ctrl->R.active = true;
			}
			else n_errors++;
			break;
		case 'S': case 's':
			Ctrl->S.active = true;
			Ctrl->mode.ssv_output = true;
			Ctrl->mode.svp_file_output = false;
			Ctrl->mode.svp_setprocess = false;
			break;
		case 'T': case 't':
			Ctrl->T.active = true;
			Ctrl->mode.output_as_table = true;
			Ctrl->mode.ssv_output = false;
			break;
		case 'Z': case 'z':
			Ctrl->Z.active = true;
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}
	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code) { if (T) mb_gmt_text_end(T); Free_mbsvplist_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbsvplist(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbsvplist(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MBSVPLIST_CTRL *Ctrl = NULL;
	struct MB_GMT_TEXT *T = NULL;	/* the listing, through the GMT API */
	int parse_error;

	if (!API) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no arguments is a valid run: mbsvplist reads datalist.mb-1 */
	if ((parse_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(parse_error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	/* -p (process) takes no argument: keep GMT from completing it as its -p from history */
	mb_gmt_shorthand_guard(options, "p");
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = (struct MBSVPLIST_CTRL *)New_mbsvplist_Ctrl(GMT);
	if ((parse_error = parse_mbsvplist(GMT, Ctrl, options)) != GMT_NOERROR) Return(parse_error);
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

  printmode_t svp_printmode = MBSVPLIST_PRINTMODE_CHANGE;
  bool output_counts = false;
  bool ssv_output = false;
  char read_file[MB_PATH_MAXLINE] = "datalist.mb-1";
  int min_num_pairs = 0;
  bool svp_file_output = false;
  bool svp_setprocess = false;
  double ssv_bounds[4] = {-360.0, 360.0, -90.0, 90.0};
  bool ssv_bounds_set = false;
  bool output_as_table = false;
  bool svp_force_zero = false;
  int svp_source_use = -1;


  {

    if (Ctrl->A.active)
      svp_source_use = Ctrl->A.svp_source_use;
    if (Ctrl->F.active)
      format = Ctrl->F.format;
    if (Ctrl->I.active)
      sscanf(Ctrl->I.file, "%1023s", read_file);
    if (Ctrl->N.active)
      min_num_pairs = Ctrl->N.min_num_pairs;
    if (Ctrl->R.active) {
      for (int i = 0; i < 4; i++)
        ssv_bounds[i] = Ctrl->R.bounds[i];
      ssv_bounds_set = true;
    }
    if (Ctrl->Z.active)
      svp_force_zero = true;
    svp_printmode = Ctrl->mode.svp_printmode;
    output_counts = Ctrl->mode.output_counts;
    ssv_output = Ctrl->mode.ssv_output;
    svp_file_output = Ctrl->mode.svp_file_output;
    svp_setprocess = Ctrl->mode.svp_setprocess;
    output_as_table = Ctrl->mode.output_as_table;

    if (verbose == 1) {
      fprintf(stderr, "\nProgram %s\n", program_name);
      fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
    }

    if (verbose >= 2) {
      fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
      fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
      fprintf(stderr, "dbg2  Control Parameters:\n");
      fprintf(stderr, "dbg2       verbose:           %d\n", verbose);
      fprintf(stderr, "dbg2       format:            %d\n", format);
      fprintf(stderr, "dbg2       pings:             %d\n", pings);
      fprintf(stderr, "dbg2       lonflip:           %d\n", lonflip);
      fprintf(stderr, "dbg2       bounds[0]:         %f\n", bounds[0]);
      fprintf(stderr, "dbg2       bounds[1]:         %f\n", bounds[1]);
      fprintf(stderr, "dbg2       bounds[2]:         %f\n", bounds[2]);
      fprintf(stderr, "dbg2       bounds[3]:         %f\n", bounds[3]);
      fprintf(stderr, "dbg2       btime_i[0]:        %d\n", btime_i[0]);
      fprintf(stderr, "dbg2       btime_i[1]:        %d\n", btime_i[1]);
      fprintf(stderr, "dbg2       btime_i[2]:        %d\n", btime_i[2]);
      fprintf(stderr, "dbg2       btime_i[3]:        %d\n", btime_i[3]);
      fprintf(stderr, "dbg2       btime_i[4]:        %d\n", btime_i[4]);
      fprintf(stderr, "dbg2       btime_i[5]:        %d\n", btime_i[5]);
      fprintf(stderr, "dbg2       btime_i[6]:        %d\n", btime_i[6]);
      fprintf(stderr, "dbg2       etime_i[0]:        %d\n", etime_i[0]);
      fprintf(stderr, "dbg2       etime_i[1]:        %d\n", etime_i[1]);
      fprintf(stderr, "dbg2       etime_i[2]:        %d\n", etime_i[2]);
      fprintf(stderr, "dbg2       etime_i[3]:        %d\n", etime_i[3]);
      fprintf(stderr, "dbg2       etime_i[4]:        %d\n", etime_i[4]);
      fprintf(stderr, "dbg2       etime_i[5]:        %d\n", etime_i[5]);
      fprintf(stderr, "dbg2       etime_i[6]:        %d\n", etime_i[6]);
      fprintf(stderr, "dbg2       speedmin:          %f\n", speedmin);
      fprintf(stderr, "dbg2       timegap:           %f\n", timegap);
      fprintf(stderr, "dbg2       read_file:         %s\n", read_file);
      fprintf(stderr, "dbg2       svp_source_use:    %d\n", svp_source_use);
      fprintf(stderr, "dbg2       svp_printmode:     %d\n", svp_printmode);
      fprintf(stderr, "dbg2       svp_file_output:   %d\n", svp_file_output);
      fprintf(stderr, "dbg2       svp_setprocess:    %d\n", svp_setprocess);
      fprintf(stderr, "dbg2       svp_force_zero:    %d\n", svp_force_zero);
      fprintf(stderr, "dbg2       ssv_output:        %d\n", ssv_output);
      fprintf(stderr, "dbg2       ssv_bounds_set:    %d\n", ssv_bounds_set);
      fprintf(stderr, "dbg2       ssv_bounds[0]:     %f\n", ssv_bounds[0]);
      fprintf(stderr, "dbg2       ssv_bounds[1]:     %f\n", ssv_bounds[1]);
      fprintf(stderr, "dbg2       ssv_bounds[2]:     %f\n", ssv_bounds[2]);
      fprintf(stderr, "dbg2       ssv_bounds[3]:     %f\n", ssv_bounds[3]);
    }

  }

  /* the listing (profiles, counts, ssv, table): text records through the GMT API */
  if ((T = mb_gmt_text_begin(GMT, options)) == NULL)
    Return(API->error);

  int error = MB_ERROR_NO_ERROR;

  if (format == 0)
    mb_get_format(verbose, read_file, NULL, &format, &error);

  /* determine whether to read one file or a list of files */
  const bool read_datalist = format < 0;
  bool read_data;
  void *datalist;
  char file[MB_PATH_MAXLINE];
  char dfile[MB_PATH_MAXLINE];
  double file_weight;

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
    /* else copy single filename to be read */
    strcpy(file, read_file);
    read_data = true;
  }

  /* MBIO read control parameters */
  double btime_d;
  double etime_d;
  int beams_bath;
  int beams_amp;
  int pixels_ss;

  /* MBIO read values */
  void *mbio_ptr = NULL;
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
  char *beamflag = NULL;
  double *bath = NULL;
  double *bathacrosstrack = NULL;
  double *bathalongtrack = NULL;
  double *amp = NULL;
  double *ss = NULL;
  double *ssacrosstrack = NULL;
  double *ssalongtrack = NULL;
  char comment[MB_COMMENT_MAXLINE];

  /* save time stamp and position of last survey data */
  double last_time_d = 0.0;
  double last_navlon = 0.0;
  double last_navlat = 0.0;

  /* data record source types */
  int platform_source;
  int nav_source;
  int sensordepth_source;
  int heading_source;
  int attitude_source;
  int svp_source;

  /* output mode settings */

  /* SVP values */
  struct mbsvplist_svp_struct svp;
  struct mbsvplist_svp_struct svp_last;
  svp_last.n = 0;
  int svp_save_alloc = 0;
  struct mbsvplist_svp_struct *svp_save = NULL;
  mb_pathplus svp_file;
  int svp_read_tot = 0;
  int svp_written_tot = 0;
  int svp_repeat_in_file;
  int out_cnt = 0;
  int svp_time_i[7];

  /* ttimes values */
  int nbeams;
  double *ttimes = NULL;
  double *angles = NULL;
  double *angles_forward = NULL;
  double *angles_null = NULL;
  double *heave = NULL;
  double *alongtrack_offset = NULL;
  double ssv;

  bool svp_match_last = false;
  int svp_unique_tot = 0;

  /* loop over all files to be read */
  while (read_data) {
    /* check format and get data sources */
    if ((status = mb_format_source(verbose, &format, &platform_source, &nav_source, &sensordepth_source, &heading_source,
                                   &attitude_source, &svp_source, &error)) == MB_FAILURE) {
      char *message;
      mb_error(verbose, error, &message);
      fprintf(stderr, "\nMBIO Error returned from function <mb_format_source>:\n%s\n", message);
      fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
      Return(GMT_RUNTIME_ERROR);
    }

    /* if svp source record type has been specified, override the default svp_source for this format */
    if (svp_source_use >= 0) {
      svp_source = svp_source_use;
    }

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
    if (error == MB_ERROR_NO_ERROR)
      status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&ttimes, &error);
    if (error == MB_ERROR_NO_ERROR)
      status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&angles, &error);
    if (error == MB_ERROR_NO_ERROR)
      status =
          mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&angles_forward, &error);
    if (error == MB_ERROR_NO_ERROR)
      status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&angles_null, &error);
    if (error == MB_ERROR_NO_ERROR)
      status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&heave, &error);
    if (error == MB_ERROR_NO_ERROR)
      status =
          mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&alongtrack_offset, &error);

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
      if (ssv_output)
        fprintf(stderr, "\nSearching %s for SSV records\n", file);
      else
        fprintf(stderr, "\nSearching %s for SVP records\n", file);
    }

    /* read and print data */
    bool svp_loaded = false;
    svp.n = 0;
    int svp_save_count = 0;
    int svp_read = 0;
    int svp_written = 0;
    int svp_unique = 0;
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

      /* if svp then extract data */
      if (error <= MB_ERROR_NO_ERROR && kind == svp_source && svp_source != MB_DATA_NONE) {
        /* extract svp */
        status = mb_extract_svp(verbose, mbio_ptr, store_ptr, &kind, &svp.n, svp.depth, svp.velocity, &error);
        if (status == MB_SUCCESS) {
          svp_read++;
          svp_loaded = true;
          svp.match_last = false;
          svp.repeat_in_file = false;
          if (last_time_d != 0.0) {
            svp.time_set = true;
            svp.time_d = last_time_d;
          }
          else {
            svp.time_set = false;
            svp.time_d = 0.0;
          }
          if (navlon != 0.0 || navlat != 0.0) {
            svp.position_set = true;
            svp.longitude = navlon;
            svp.latitude = navlat;
          }
          else if (last_navlon != 0.0 || last_navlat != 0.0) {
            svp.position_set = true;
            svp.longitude = last_navlon;
            svp.latitude = last_navlat;
          }
          else {
            svp.position_set = false;
            svp.longitude = 0.0;
            svp.latitude = 0.0;
          }
          svp.depthzero_reset = false;
          svp.depthzero = 0.0;
        }
        else {
          svp_loaded = false;
        }

        /* force zero depth if requested */
        if (svp_loaded && svp.n > 0 && svp_force_zero && svp.depth[0] != 0.0) {
          svp.depthzero = svp.depth[0];
          svp.depth[0] = 0.0;
          svp.depthzero_reset = true;
        }

        /* check if the svp is a duplicate to a previous svp
            in the same file */
        if (svp_loaded) {
          svp_match_last = false;
          for (int j = 0; j < svp_save_count && svp_match_last; j++) {
            if (svp.n == svp_save[j].n && memcmp(svp.depth, svp_save[j].depth, svp.n) == 0 &&
                memcmp(svp.velocity, svp_save[j].velocity, svp.n) == 0) {
              svp_match_last = true;
            }
          }
          svp.match_last = svp_match_last;
        // }

        /* check if the svp is a duplicate to the previous svp
            whether from the same file or a previous file */
        // if (svp_loaded) {
          /* check if svp is the same as the previous */
          if (svp.n == svp_last.n && memcmp(svp.depth, svp_last.depth, svp.n) == 0 &&
              memcmp(svp.velocity, svp_last.velocity, svp.n) == 0) {
            svp_repeat_in_file = true;
          }
          else {
            svp_repeat_in_file = false;
          }
          svp.repeat_in_file = svp_repeat_in_file;

          /* save the svp */
          svp_last.time_set = false;
          svp_last.position_set = false;
          svp_last.n = svp.n;
          for (int i = 0; i < svp.n; i++) {
            svp_last.depth[i] = svp.depth[i];
            svp_last.velocity[i] = svp.velocity[i];
          }
        }

        /* if the svp is unique so far, save it in memory */
        if (svp_loaded && !svp_match_last && svp.n >= min_num_pairs) {
          /* allocate memory as needed */
          if (svp_save_count >= svp_save_alloc) {
            svp_save_alloc += MBSVPLIST_SVP_NUM_ALLOC;
            status = mb_reallocd(verbose, __FILE__, __LINE__, svp_save_alloc * sizeof(struct mbsvplist_svp_struct),
                                 (void **)&svp_save, &error);
            if (status != MB_SUCCESS) {
              fprintf(stderr, "\nUnable to allocate SVP save array\n");
              fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
              Return(GMT_MEMORY_ERROR);
            }
          }

          /* save the svp */
          svp_save[svp_save_count].time_set = svp.time_set;
          svp_save[svp_save_count].position_set = svp.position_set;
          svp_save[svp_save_count].repeat_in_file = svp.repeat_in_file;
          svp_save[svp_save_count].match_last = svp.match_last;
          svp_save[svp_save_count].time_d = svp.time_d;
          svp_save[svp_save_count].longitude = svp.longitude;
          svp_save[svp_save_count].latitude = svp.latitude;
          svp_save[svp_save_count].n = svp.n;
          for (int i = 0; i < svp.n; i++) {
            svp_save[svp_save_count].depth[i] = svp.depth[i];
            svp_save[svp_save_count].velocity[i] = svp.velocity[i];
          }
          svp_save_count++;
          svp_unique++;
        }
      }

      /* else if survey data save most recent ping time
          and if ssv output desired call mb_ttimes() and output ssv */
      else if (error <= MB_ERROR_NO_ERROR && kind == MB_DATA_DATA) {
        /* save most recent survey time stamp and position */
        last_time_d = time_d;
        last_navlon = navlon;
        last_navlat = navlat;

        /* check if any saved svps need time tags and position */
        if (time_d != 0.0 && (navlon != 0.0 || navlat != 0.0)) {
          for (int isvp = 0; isvp < svp_save_count; isvp++) {
            if (!svp_save[isvp].time_set) {
              svp_save[isvp].time_set = true;
              svp_save[isvp].time_d = time_d;
            }
            if (!svp_save[isvp].position_set) {
              svp_save[isvp].position_set = true;
              svp_save[isvp].longitude = navlon;
              svp_save[isvp].latitude = navlat;
            }
          }
        }

        /* if desired output ssv_output */
        if (ssv_output) {
          /* extract ttimes */
          status = mb_ttimes(verbose, mbio_ptr, store_ptr, &kind, &nbeams, ttimes, angles, angles_forward, angles_null,
                             heave, alongtrack_offset, &sensordepth, &ssv, &error);

          /* output ssv */
          if (status == MB_SUCCESS) {
            if (!ssv_bounds_set || (navlon >= ssv_bounds[0] && navlon <= ssv_bounds[1] &&
                                            navlat >= ssv_bounds[2] && navlat <= ssv_bounds[3]))
              mb_gmt_text_put(T, "%f %f\n", sensordepth, ssv);
          }
        }
      }
    }

    status &= mb_close(verbose, &mbio_ptr, &error);

    /* output svps from this file if there are any and ssv_output and output_counts are false */
    if (svp_save_count > 0 && !ssv_output && !output_counts) {
      for (int isvp = 0; isvp < svp_save_count; isvp++) {
        if (svp_save[isvp].n >= min_num_pairs &&
            ((svp_printmode == MBSVPLIST_PRINTMODE_CHANGE &&
              (svp_written == 0 || !svp_save[isvp].repeat_in_file)) ||
             (svp_printmode == MBSVPLIST_PRINTMODE_UNIQUE && !svp_save[isvp].match_last) ||
             (svp_printmode == MBSVPLIST_PRINTMODE_ALL))) {
          /* set the output */
          struct MB_GMT_TEXT *svp_fp = T;	/* the listing, or the profile's own .svp file */
          if (svp_file_output) {
            snprintf(svp_file, sizeof(svp_file), "%s_%3.3d.svp", file, isvp);
            svp_fp = mb_gmt_text_file(GMT, svp_file);
          }

          /* get time as date */
          mb_get_date(verbose, svp_save[isvp].time_d, svp_time_i);

          /* print out the svp */
          if (output_as_table) /* output csv table to stdout */
          {
            if (out_cnt == 0) /* output header records */
            {
              mb_gmt_text_put(T, "#mbsvplist CSV table output\n#navigation information is "
                     "approximate\n#SVP_cnt,date_time,longitude,latitude,num_data_points\n");
            }
            out_cnt++;
            mb_gmt_text_put(T, "%d,%4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d,%.6f,%.6f,%d\n", out_cnt, svp_time_i[0],
                   svp_time_i[1], svp_time_i[2], svp_time_i[3], svp_time_i[4], svp_time_i[5], svp_time_i[6],
                   svp_save[isvp].longitude, svp_save[isvp].latitude, svp_save[isvp].n);
          }
          else if (svp_fp != NULL) {
            /* output info */
            if (verbose >= 1) {
              fprintf(stderr, "Outputting SVP to file: %s (# svp pairs=%d)\n", svp_file, svp_save[isvp].n);
            }

            /* write it out */
            mb_gmt_text_put(svp_fp, "## MB-SVP %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d %.9f %.9f\n", svp_time_i[0],
                    svp_time_i[1], svp_time_i[2], svp_time_i[3], svp_time_i[4], svp_time_i[5], svp_time_i[6],
                    svp_save[isvp].longitude, svp_save[isvp].latitude);
            mb_gmt_text_put(svp_fp, "## Water Sound Velocity Profile (SVP)\n");
            mb_gmt_text_put(svp_fp, "## Output by Program %s\n", program_name);
            mb_gmt_text_put(svp_fp, "## MB-System Version %s\n", MB_VERSION);
            char user[256], host[256], date[32];
            status = mb_user_host_date(verbose, user, host, date, &error);
            mb_gmt_text_put(svp_fp, "## Run by user <%s> on cpu <%s> at <%s>\n", user, host, date);
            mb_gmt_text_put(svp_fp, "## Swath File: %s\n", file);
            mb_gmt_text_put(svp_fp, "## Start Time: %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n", svp_time_i[0],
                    svp_time_i[1], svp_time_i[2], svp_time_i[3], svp_time_i[4], svp_time_i[5], svp_time_i[6]);
            mb_gmt_text_put(svp_fp, "## SVP Longitude: %f\n", svp_save[isvp].longitude);
            mb_gmt_text_put(svp_fp, "## SVP Latitude:  %f\n", svp_save[isvp].latitude);
            mb_gmt_text_put(svp_fp, "## SVP Count: %d\n", svp_save_count);
            if (svp_save[isvp].depthzero_reset) {
              mb_gmt_text_put(svp_fp, "## Initial depth reset from %f to 0.0 meters\n", svp_save[isvp].depthzero);
            }
            if (verbose >= 1 && svp_save[isvp].depthzero_reset) {
              fprintf(stderr, "Initial depth reset from %f to 0.0 meters\n", svp_save[isvp].depthzero);
            }
            mb_gmt_text_put(svp_fp, "## Number of SVP Points: %d\n", svp_save[isvp].n);
            for (int i = 0; i < svp_save[isvp].n; i++)
              mb_gmt_text_put(svp_fp, "%8.2f\t%7.2f\n", svp_save[isvp].depth[i], svp_save[isvp].velocity[i]);
            if (!svp_file_output) {
              mb_gmt_text_put(svp_fp, "## \n");
              mb_gmt_text_put(svp_fp, "## \n");
            }
            svp_written++;
          }

          /* close the svp file */
          if (svp_file_output && svp_fp != NULL) {
            mb_gmt_text_end(svp_fp);

            /* if desired, set first svp output to be used for recalculating
                bathymetry */
            if (svp_setprocess && svp_save_count >= 1) {
              status = mb_pr_update_svp(verbose, file, true, svp_file, MBP_ANGLES_OK, true, &error);
            }
          }
        }
      }
    }

    /* update total counts */
    svp_read_tot += svp_read;
    svp_unique_tot += svp_unique;
    svp_written_tot += svp_written;

    /* output info */
    if (verbose >= 1) {
      fprintf(stderr, "%d SVP records read\n", svp_read);
      fprintf(stderr, "%d SVP unique records read\n", svp_unique);
      fprintf(stderr, "%d SVP records written\n", svp_written);
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
    fprintf(stderr, "\nTotal %d SVP records read\n", svp_read_tot);
    fprintf(stderr, "Total %d SVP unique records found\n", svp_unique_tot);
    fprintf(stderr, "Total %d SVP records written\n", svp_written_tot);
  }
  if (output_counts)
    mb_gmt_text_put(T, "%d\n", svp_unique_tot);

  /* deallocate memory */
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&svp_save, &error);

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
