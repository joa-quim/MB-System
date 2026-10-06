/*--------------------------------------------------------------------
 *    The MB-system:  mbotps.c  7/30/2009
 *
 *    Copyright (c) 2009-2025 by
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
 * MBotps predicts tides using tidal prediction methods and data derived
 * from the OSU Tidal Prediction Software (OTPS) distributions available
 * from Gary Egbert & Lana Erofeeva, Oregon State University, at:
 *      https://www.tpxo.net/
 *
 * Unlike the original mbotps (retained as mbotpsold), this program does
 * NOT invoke an external OTPS program (predict_tide) to compute tidal
 * predictions. Instead it implements the same harmonic tidal prediction
 * methods natively, in mb_otps_predict.c/.h, reading an OTPS "atlas"
 * format tide model's binary data files directly. Users must still
 * separately obtain OTPS tidal model data files (e.g. TPXO9_atlas or
 * TPXO10_atlas) from OSU and install them at the location given by the
 * --otps-path option (default /usr/local/src/otps/DATA/), exactly as
 * before - only the separate predict_tide executable is no longer
 * required. See mb_otps_predict.h for attribution of the numerical
 * methods used here to OTPS and to Richard Ray (NASA/GSFC), and for
 * license information.
 *
 * The mbotps usage is:
 *       mbotps [-Atideformat -Byear/month/day/hour/minute/second
 *              -Ctidestationformat -Dinterval
 *              -Eyear/month/day/hour/minute/second -Fformat -Idatalist
 *              -Lopts_path -Ntidestationfile -Ooutput -Potps_location
 *             -Rlon/lat -S -Tmodel -Utidestationlon/tidestationlat -V]
 *
 * This program can be used in two modes. In the first, the user
 * specifies a location (-Rlon/lat), start and end times (-B and -E),
 * and a tidal sampling interval (-D). The program then writes a two
 * column tide time series consisting of epoch time values in seconds followed
 * by tide values in meters for the specified location and times. The
 * output is to a file specified with -Otide_file.
 *
 * In the second mode, the user specifies one or more swath data files using
 * -Idatalist.mb-1. A tide file is generated for each swath file by
 * outputing the time and tide value for the sonar navigation sampled
 * according to -Dinterval. MBotps also sets the parameter file for each
 * swath file so that mbprocess applies the tide model during processing.
 *
 * The -Ctidestationformat, -Ntidestationfile, and  -Utidestationlon/tidestationlat
 * commands together allow users to input observations from a tide station;
 * these observations can be used to calculate corrections to tidal model values
 * in the vicinity of the tide station. If tide station data are specified,
 * then MBotps calculates the difference between the observed and modeled tide
 * at that station for each data point in the input tide station data. This
 * difference time series is then used as a correction to the output tide models,
 * whether at a location specified with the -Rlon/lat option or for swath data
 * specified with the -Idatalist option.

 * Author:  D. W. Caress
 * Date:  July 30,  2009
 * Date:  April 5,  2018
 * Date:  September 2, 2026 (native tidal prediction engine)
 */

#define THIS_MODULE_NAME    "mbotps"
#define THIS_MODULE_LIB     "mbsystem"
#define THIS_MODULE_PURPOSE "Predict tides using the OSU Tidal Prediction Software models"
/* -I carries the input datalist, the module's one primary resource, so an
 * external API binds its input argument to -I. No output key: mbotps writes
 * the tide file named by -O itself and hands nothing back through the API. */
#define THIS_MODULE_KEYS    "ID{"
#define THIS_MODULE_NEEDS   ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#ifdef _MSC_VER
#include "dirent_w.h"
#else
#include <dirent.h>
#endif
/* getopt is not used here: GMT parses the command line */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _MSC_VER
#include "unistd_w.h"
#else
#include <unistd.h>
#endif

#include "mb_define.h"

#include "mb_format.h"
#include "mb_process.h"
#include "mb_status.h"

/* OTPS installation location */
#include "otps.h"

/* native OTPS-derived tidal prediction engine (no predict_tide needed) */
#include "mb_otps_predict.h"

#define MBOTPS_MODE_POSITION            0x00
#define MBOTPS_MODE_NAVIGATION          0x01
#define MBOTPS_MODE_TIDESTATION         0x02
#define MBOTPS_MODE_NAV_WRT_STATION     0x03
#define MBOTPS_DEFAULT_MODEL "tpxo10_atlas"

static char program_name[] = "mbotps";
static char help_message[] =
    "MBotps predicts tides using methods and data derived from the "
    "OSU Tidal Prediction Software (OTPS) distributions.";
static char usage_message[] =
    "mbotps\n\t[\n"
    "\t--input=datalist {-Idatalist}\n"
    "\t--format=format_id {-Fformat_id}\n"
    "\t--use-mbprocess {-M}\n"
    "\t--skip-existing {-S}\n"
    "\t--tide-output=file {-Ooutput}\n"
    "\t--tide-position=lon/lat {-Rlon/lat}\n"
    "\t--tide-format=format_id {-Atideformat}\n"
    "\t--interval=interval {-Dinterval}\n"
    "\t--start-time=year/month/day/hour/minute/second {-Byear/month/day/hour/minute/second}\n"
    "\t--end-time=year/month/day/hour/minute/second {-Eyear/month/day/hour/minute/second}\n"
    "\t--tide-station-file=file {-Ntidestationfile}\n"
    "\t--tide-station-position=lon/lat {-Ulon/lat}\n"
    "\t--tide-station-format=format_id {-Ctidestationformat}\n"
    "\t--otps-model=model_name {-Tmodel}\n"
    "\t--otps-path=path {-Ppath}\n"
    "\t--help {-H}\n"
    "\t--verbose {-V}\n"
    "\t]\n";
/*--------------------------------------------------------------------*/
/* build a space-separated list of the tidal constituent names actually
   available in the currently opened model, for use in output headers */
static void mbotps_constituent_list(struct mbotps_model *model, char *buf, size_t bufsize) {
  buf[0] = '\0';
  size_t used = 0;
  for (int i = 0; i < model->ncon && used < bufsize - 1; i++) {
    const int n = snprintf(buf + used, bufsize - used, "%-4s", model->con[i].name);
    if (n < 0)
      break;
    used += (size_t)n;
  }
}
/*--------------------------------------------------------------------*/
/* write the standard comment header used at the top of every tide file
   this program generates */
static void mbotps_write_header(FILE *ofp, const char *otps_model,
                                 struct mbotps_model *model, int tideformat) {
  fprintf(ofp, "# Tide model generated by program %s\n", program_name);
  fprintf(ofp, "# MB-System Version: %s\n", MB_VERSION);
  fprintf(ofp, "# Tide model generated by program %s\n", program_name);
  fprintf(ofp, "# using tidal prediction methods and data derived from the\n");
  fprintf(ofp, "# OSU Tidal Prediction Software (OTPS) distribution, see:\n");
  fprintf(ofp, "#     https://www.tpxo.net/\n");
  fprintf(ofp, "#\n");
  fprintf(ofp, "# OTPS tide model:\n");
  fprintf(ofp, "#      %s\n", otps_model);
  mb_pathplus constituents = "";
  mbotps_constituent_list(model, constituents, sizeof(constituents));
  fprintf(ofp, "# Constituents included: %s\n", constituents);
  if (tideformat == 2) {
    fprintf(ofp, "# Output format:\n");
    fprintf(ofp, "#      year month day hour minute second tide\n");
    fprintf(ofp, "# where tide is in meters\n");
  } else {
    fprintf(ofp, "# Output format:\n");
    fprintf(ofp, "#      time_d tide\n");
    fprintf(ofp, "# where time_d is in seconds since January 1, 1970\n");
    fprintf(ofp, "# and tide is in meters\n");
  }
  char user[256], host[256], date[32];
  int error = MB_ERROR_NO_ERROR;
  mb_user_host_date(0, user, host, date, &error);
  fprintf(ofp, "# Run by user <%s> on cpu <%s> at <%s>\n", user, host, date);
}
/*--------------------------------------------------------------------*/

/*--------------------------------------------------------------------*/
/* GMT module scaffolding. Everything below the parser is the original
   main() body with the getopt_long loop removed. */
struct MBOTPS_CTRL {
	int verbose;	/* the program's -V/-v count */
	struct otps_A { bool active; int tideformat; } A;
	struct otps_B { bool active; int time_i[7]; } B;
	struct otps_C { bool active; int tidestation_format; } C;
	struct otps_D { bool active; double interval; } D;
	struct otps_E { bool active; int time_i[7]; } E;
	struct otps_F { bool active; int format; } F;
	struct otps_I { bool active; mb_path file; } I;
	struct otps_M { bool active; } M;
	struct otps_N { bool active; mb_path file; } N;
	struct otps_O { bool active; mb_path file; } O;
	struct otps_P { bool active; mb_path path; } P;
	struct otps_R { bool active; double lon, lat; } R;
	struct otps_S { bool active; } S;
	struct otps_T { bool active; mb_path model; } T;
	struct otps_U { bool active; double lon, lat; } U;
	struct otps_H { bool active; } H;
};

static void *New_mbotps_Ctrl(struct GMT_CTRL *GMT) {
	struct MBOTPS_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBOTPS_CTRL);
	return Ctrl;
}

static void Free_mbotps_Ctrl(struct GMT_CTRL *GMT, struct MBOTPS_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'I', "input",                 "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'F', "format",                "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'M', "use-mbprocess",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'S', "skip-existing",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "tide-output",           "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'R', "tide-position",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'A', "tide-format",           "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'D', "interval",              "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'B', "start-time",            "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'E', "end-time",              "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'N', "tide-station-file",     "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'U', "tide-station-position", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'C', "tide-station-format",   "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'T', "otps-model",            "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'P', "otps-path",             "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",                  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'v', "verbose",               "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "\nusage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "\n%s\n", help_message);
	GMT_Message(API, GMT_TIME_NONE, "\tEvery option also has the program's lower-case form.\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

/* Port of the option switch: the long forms come in through module_kw, the lower-case aliases are
   the program's own. */
static int parse_mbotps(struct GMT_CTRL *GMT, struct MBOTPS_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt = NULL;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case '<':
		case 'I':
		case 'i':
			sscanf(opt->arg, "%1023s", Ctrl->I.file);
			Ctrl->I.active = true;
			break;
		case 'A':
		case 'a':
			if (sscanf(opt->arg, "%d", &Ctrl->A.tideformat) == 1) Ctrl->A.active = true;
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'B':
		case 'b':
			if (sscanf(opt->arg, "%d/%d/%d/%d/%d/%d", &Ctrl->B.time_i[0], &Ctrl->B.time_i[1],
			           &Ctrl->B.time_i[2], &Ctrl->B.time_i[3], &Ctrl->B.time_i[4],
			           &Ctrl->B.time_i[5]) == 6) {
				Ctrl->B.time_i[6] = 0;
				Ctrl->B.active = true;
			}
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'C':
		case 'c':
			if (sscanf(opt->arg, "%d", &Ctrl->C.tidestation_format) == 1) Ctrl->C.active = true;
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'D':
		case 'd':
			if (sscanf(opt->arg, "%lf", &Ctrl->D.interval) == 1) Ctrl->D.active = true;
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'E':
		case 'e':
			if (sscanf(opt->arg, "%d/%d/%d/%d/%d/%d", &Ctrl->E.time_i[0], &Ctrl->E.time_i[1],
			           &Ctrl->E.time_i[2], &Ctrl->E.time_i[3], &Ctrl->E.time_i[4],
			           &Ctrl->E.time_i[5]) == 6) {
				Ctrl->E.time_i[6] = 0;
				Ctrl->E.active = true;
			}
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'F':
		case 'f':
			if (sscanf(opt->arg, "%d", &Ctrl->F.format) == 1) Ctrl->F.active = true;
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'M':
		case 'm':
			Ctrl->M.active = true;
			break;
		case 'N':
		case 'n':
			sscanf(opt->arg, "%1023s", Ctrl->N.file);
			Ctrl->N.active = true;
			break;
		case 'O':
		case 'o':
			sscanf(opt->arg, "%1023s", Ctrl->O.file);
			Ctrl->O.active = true;
			break;
		case 'P':
		case 'p':
			sscanf(opt->arg, "%1023s", Ctrl->P.path);
			Ctrl->P.active = true;
			break;
		case 'R':
		case 'r':
			if (sscanf(opt->arg, "%lf/%lf", &Ctrl->R.lon, &Ctrl->R.lat) == 2) Ctrl->R.active = true;
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			break;
		case 'S':
		case 's':
			Ctrl->S.active = true;
			break;
		case 'T':
		case 't':
			sscanf(opt->arg, "%1023s", Ctrl->T.model);
			Ctrl->T.active = true;
			break;
		case 'U':
		case 'u':
			if (sscanf(opt->arg, "%lf/%lf", &Ctrl->U.lon, &Ctrl->U.lat) == 2) Ctrl->U.active = true;
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
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

	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code)  { Free_mbotps_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }

EXTERN_MSC int GMT_mbotps(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/
/* Port of main(). */
int GMT_mbotps(void *V_API, int mode, void *args) {
	struct MBOTPS_CTRL *Ctrl = NULL;
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	int parse_status;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program (it lists the tide models and predicts at 0/0) */
	if ((parse_status = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(parse_status);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	Ctrl = (struct MBOTPS_CTRL *)New_mbotps_Ctrl(GMT);
	if ((parse_status = parse_mbotps(GMT, Ctrl, options)) != 0) Return(parse_status);

  int verbose = 0;
  int format;
  int pings;
  int lonflip;
  double bounds[4];
  int btime_i[7];
  int etime_i[7];
  double speedmin;
  double timegap;

  int status = mb_defaults(
      verbose, &format, &pings, &lonflip, bounds, btime_i, etime_i,
      &speedmin, &timegap);
  btime_i[0] = 2009;
  btime_i[1] = 7;
  btime_i[2] = 31;
  btime_i[3] = 0;
  btime_i[4] = 0;
  btime_i[5] = 0;
  btime_i[6] = 0;
  etime_i[0] = 2009;
  etime_i[1] = 8;
  etime_i[2] = 2;
  etime_i[3] = 1;
  etime_i[4] = 0;
  etime_i[5] = 0;
  etime_i[6] = 0;

  /* set default location of the OTPS package */
  mb_path otps_location_use;
  strcpy(otps_location_use, otps_location);

  // Set defaults for the AUV survey we were running on Coaxial Segment, Juan de Fuca Ridge
  mb_path otps_model = "";
  bool otps_model_set = false;
  bool otps_model_set_installed = false;
  bool otps_model_default_installed = false;
  mb_path otps_model_other = "";
  bool otps_model_other_installed = false;
  mb_path tide_file = "tide_model.txt";
  double tidelon = -129.588618;
  double tidelat = 46.50459;
  double interval = 60.0;  // TODO(schwehr): Why not the 300.0?
  int tideformat = 2;
  int tidestation_format = 2;
  mb_path read_file = "datalist.mb-1";
  int mbotps_mode = MBOTPS_MODE_POSITION;
  bool mbprocess_update = false;
  mb_path tidestation_file;
  bool skip_existing = false;
  double tidestation_lon = 0.0;
  double tidestation_lat = 0.0;
  bool help = false;


	/* apply the parsed options onto the mb_defaults-initialised locals */
	verbose = Ctrl->verbose;	/* the program's count (was GMT's verbosity level, not MB-System's) */
	help = Ctrl->H.active;
	if (Ctrl->A.active) {
		tideformat = Ctrl->A.tideformat;
		if (tideformat < 1 || tideformat > 4) tideformat = 2;
	}
	if (Ctrl->B.active) memcpy(btime_i, Ctrl->B.time_i, sizeof(btime_i));
	if (Ctrl->C.active) {
		tidestation_format = Ctrl->C.tidestation_format;
		if (tidestation_format < 1 || tidestation_format > 4) tidestation_format = 2;
	}
	if (Ctrl->D.active) interval = Ctrl->D.interval;
	if (Ctrl->E.active) memcpy(etime_i, Ctrl->E.time_i, sizeof(etime_i));
	if (Ctrl->F.active) format = Ctrl->F.format;
	if (Ctrl->I.active) {
		strncpy(read_file, Ctrl->I.file, sizeof(read_file) - 1);
		mbotps_mode = mbotps_mode | MBOTPS_MODE_NAVIGATION;
	}
	if (Ctrl->M.active) mbprocess_update = true;
	if (Ctrl->N.active) {
		strncpy(tidestation_file, Ctrl->N.file, sizeof(tidestation_file) - 1);
		mbotps_mode = mbotps_mode | MBOTPS_MODE_TIDESTATION;
	}
	if (Ctrl->O.active) strncpy(tide_file, Ctrl->O.file, sizeof(tide_file) - 1);
	if (Ctrl->P.active) strncpy(otps_location_use, Ctrl->P.path, sizeof(otps_location_use) - 1);
	if (Ctrl->R.active) {
		tidelon = Ctrl->R.lon;
		tidelat = Ctrl->R.lat;
	}
	if (Ctrl->S.active) skip_existing = true;
	if (Ctrl->T.active) {
		strncpy(otps_model, Ctrl->T.model, sizeof(otps_model) - 1);
		otps_model_set = true;
	}
	if (Ctrl->U.active) {
		tidestation_lon = Ctrl->U.lon;
		tidestation_lat = Ctrl->U.lat;
	}


  if (verbose == 1 || help) {
    fprintf(stderr, "\nProgram %s\n", program_name);
    fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
  }

  if (help) {
    fprintf(stderr, "\n%s\n", help_message);
    fprintf(stderr, "\nusage: %s\n", usage_message);
  }

  /* Check for available tide models */
  if (help || verbose > 0) {
    fprintf(stderr, "\nChecking for available OTPS tide models\n");
    fprintf(stderr, "  OTPS location: %s\n  Default OTPS model name: %s\n  Specified OTPS model name: %s\n  Possible OTPS tidal models:\n",
            otps_location_use, MBOTPS_DEFAULT_MODEL, otps_model);
  }

  int notpsmodels = 0;

  {
    /* candidate model names come from two sources under DATA/: standard
       OTPS "Model_<name>" control files, and (so that a model directory
       obtained from OSU can be used with no control file at all) any
       subdirectory of DATA/ taken as a model name directly - see
       mb_otps_model_open()/mb_otps_predict.h for how each is resolved */
    mb_path found_models[64];
    int nfound = 0;

    mb_pathplus datadir = "";
    snprintf(datadir, sizeof(datadir), "%s/DATA", otps_location_use);
    DIR *dp = opendir(datadir);
    if (dp != NULL) {
      struct dirent *entry;
      while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
          continue;
        mb_path modelname = "";
        if (strncmp(entry->d_name, "Model_", 6) == 0) {
          strncpy(modelname, entry->d_name + 6, sizeof(modelname) - 1);
        }
        else {
          mb_pathplus entrypath = "";
          snprintf(entrypath, sizeof(entrypath), "%s/%s", datadir, entry->d_name);
          struct stat entry_status;
          if (stat(entrypath, &entry_status) != 0 || !S_ISDIR(entry_status.st_mode))
            continue;
          strncpy(modelname, entry->d_name, sizeof(modelname) - 1);
        }

        /* skip a name already recorded, e.g. found via both a Model_ file
           and its own same-named directory */
        bool already_found = false;
        for (int k = 0; k < nfound; k++) {
          if (strcmp(found_models[k], modelname) == 0) {
            already_found = true;
            break;
          }
        }
        if (already_found)
          continue;
        if (nfound < (int)(sizeof(found_models) / sizeof(found_models[0])))
          strncpy(found_models[nfound++], modelname, sizeof(found_models[0]) - 1);
      }
      closedir(dp);
    }

    /* validate each candidate by actually opening it with the same
       tidal prediction engine used at run time */
    for (int k = 0; k < nfound; k++) {
      const char *modelname = found_models[k];
      if (help || verbose > 0) {
        fprintf(stderr, "    %s", modelname);
      }

      struct mbotps_model trial_model;
      int trial_error = MB_ERROR_NO_ERROR;
      const bool installed =
        (mb_otps_model_open(0, otps_location_use, modelname, &trial_model, &trial_error) == MB_SUCCESS);
      if (installed)
        mb_otps_model_close(0, &trial_model, &trial_error);

      if (installed) {
        if (otps_model_set && strcmp(modelname, otps_model) == 0) {
          otps_model_set_installed = true;
        }
        else if (strcmp(modelname, MBOTPS_DEFAULT_MODEL) == 0) {
          otps_model_default_installed = true;
        }
        else {
          strncpy(otps_model_other, modelname, sizeof(otps_model_other));
          otps_model_other_installed = true;
        }
        if (help || verbose > 0) {
          fprintf(stderr, " <installed>\n");
        }
        notpsmodels++;
      }
      else {
        if (help || verbose > 0) {
          fprintf(stderr, " <not installed>\n");
        }
      }
    }

    /* settle which model to use */
    if (!otps_model_set_installed) {
      if (otps_model_default_installed) {
        strncpy(otps_model, MBOTPS_DEFAULT_MODEL, sizeof(otps_model));
      }
      else if (otps_model_other_installed) {
        strncpy(otps_model, otps_model_other, sizeof(otps_model));
      }
    }
  }
  if (help || verbose > 0) {
    fprintf(stderr, "  Number of available OTPS tide models: %d\n", notpsmodels);
    fprintf(stderr, "\nUsing OTPS tide model:  %s\n", otps_model);
  }

  /* help (and the model listing above) is done: no model is needed for it, and the program exited
     0 here either way */
  if (help)
    Return(GMT_NOERROR);

  /* exit if no valid OTPS models can be found - an error (the program exited with MB_FAILURE, 0) */
  if (notpsmodels <= 0) {
    // error = MB_ERROR_OPEN_FAIL;
    fprintf(stderr, "\nUnable to find a valid OTPS tidal model\n");
    fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
    Return(GMT_ERROR_ON_FOPEN);
  }
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
    fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
    fprintf(stderr, "dbg2  Control Parameters:\n");
    fprintf(stderr, "dbg2       verbose:              %d\n", verbose);
    fprintf(stderr, "dbg2       help:                 %d\n", help);
    fprintf(stderr, "dbg2       otps_location:        %s\n", otps_location);
    fprintf(stderr, "dbg2       otps_location_use:    %s\n", otps_location_use);
    fprintf(stderr, "dbg2       otps_model_set:       %d\n", otps_model_set);
    fprintf(stderr, "dbg2       otps_model:           %s\n", otps_model);
    fprintf(stderr, "dbg2       mbotps_mode:          %d\n", mbotps_mode);
    fprintf(stderr, "dbg2       tidelon:              %f\n", tidelon);
    fprintf(stderr, "dbg2       tidelat:              %f\n", tidelat);
    fprintf(stderr, "dbg2       tidestation_file:     %s\n", tidestation_file);
    fprintf(stderr, "dbg2       tidestation_lon:       %f\n", tidestation_lon);
    fprintf(stderr, "dbg2       tidestation_lat:       %f\n", tidestation_lat);
    fprintf(stderr, "dbg2       tidestation_format:    %d\n", tidestation_format);
    fprintf(stderr, "dbg2       btime_i[0]:           %d\n", btime_i[0]);
    fprintf(stderr, "dbg2       btime_i[1]:           %d\n", btime_i[1]);
    fprintf(stderr, "dbg2       btime_i[2]:           %d\n", btime_i[2]);
    fprintf(stderr, "dbg2       btime_i[3]:           %d\n", btime_i[3]);
    fprintf(stderr, "dbg2       btime_i[4]:           %d\n", btime_i[4]);
    fprintf(stderr, "dbg2       btime_i[5]:           %d\n", btime_i[5]);
    fprintf(stderr, "dbg2       btime_i[6]:           %d\n", btime_i[6]);
    fprintf(stderr, "dbg2       etime_i[0]:           %d\n", etime_i[0]);
    fprintf(stderr, "dbg2       etime_i[1]:           %d\n", etime_i[1]);
    fprintf(stderr, "dbg2       etime_i[2]:           %d\n", etime_i[2]);
    fprintf(stderr, "dbg2       etime_i[3]:           %d\n", etime_i[3]);
    fprintf(stderr, "dbg2       etime_i[4]:           %d\n", etime_i[4]);
    fprintf(stderr, "dbg2       etime_i[5]:           %d\n", etime_i[5]);
    fprintf(stderr, "dbg2       etime_i[6]:           %d\n", etime_i[6]);
    fprintf(stderr, "dbg2       interval:             %f\n", interval);
    fprintf(stderr, "dbg2       tide_file:            %s\n", tide_file);
    fprintf(stderr, "dbg2       mbprocess_update:     %d\n", mbprocess_update);
    fprintf(stderr, "dbg2       skip_existing:        %d\n", skip_existing);
    fprintf(stderr, "dbg2       tideformat:           %d\n", tideformat);
    fprintf(stderr, "dbg2       format:               %d\n", format);
    fprintf(stderr, "dbg2       read_file:            %s\n", read_file);
  }

  /* open the tidal prediction engine on the selected OTPS model; this
     replaces the original mbotps's use of popen() to run the separate
     OTPS Fortran program predict_tide (see mb_otps_predict.h) */
  int error = MB_ERROR_NO_ERROR;
  struct mbotps_model model;
  if (mb_otps_model_open(verbose, otps_location_use, otps_model, &model, &error) != MB_SUCCESS) {
    fprintf(stderr, "\nUnable to open OTPS tidal model '%s' at '%s'\n", otps_model, otps_location_use);
    fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
    Return(GMT_ERROR_ON_FOPEN);
  }

  int ntidestation = 0;
  mb_path line = "";  // TODO(schwehr): Localize
  double *tidestation_time_d = NULL;
  double *tidestation_tide = NULL;
  double *tidestation_model = NULL;
  double *tidestation_correction = NULL;
  int time_i[7];
  int time_j[5];
  double sec;
  double time_d;
  int ihr;
  int ngood;
  double tide;

  /* -------------------------------------------------------------------------
   * if specified read in tide station data and calculate model values for the
   * same location and times- the difference is applied as a correction to the
   * model values calculated at the desired locations and times
   * -----------------------------------------------------------------------*/
  if (mbotps_mode & MBOTPS_MODE_TIDESTATION) {
    /* make sure longitude is positive */
    if (tidestation_lon < 0.0)
      tidestation_lon += 360.0;

    /* open the tide station data file */
    FILE *tfp = fopen(tidestation_file, "r");
    if (tfp == NULL) {
      // error = MB_ERROR_OPEN_FAIL;
      fprintf(stderr,
        "\nUnable to open tide station file <%s> for writing\n",
        tidestation_file);
      fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
      Return(GMT_ERROR_ON_FOPEN);
    }

    /* count the lines in the tide station data */
    ntidestation = 0;
    {
      char *result;
      while ((result = fgets(line, MB_PATH_MAXLINE, tfp)) == line)
        ntidestation++;
    }
    rewind(tfp);

    /* allocate memory for tide station arrays */
    const int size = ntidestation * sizeof(double);
    status = mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&tidestation_time_d, &error);
    if (error == MB_ERROR_NO_ERROR)
      status = mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&tidestation_tide, &error);
    if (error == MB_ERROR_NO_ERROR)
      status = mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&tidestation_model, &error);
    if (error == MB_ERROR_NO_ERROR)
      status = mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&tidestation_correction, &error);
    if (error != MB_ERROR_NO_ERROR) {
      char *message;
      mb_error(verbose, error, &message);
      fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
      fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
      Return(GMT_MEMORY_ERROR);
    }

    /* read the tide station data in the specified format */
    ntidestation = 0;
    char *result;
    while ((result = fgets(line, MB_PATH_MAXLINE, tfp)) == line) {
      bool tidestation_ok = false;

      /* ignore comments */
      if (line[0] != '#') {
        /* deal with tide station data in form: time_d tide */
        if (tidestation_format == 1) {
          const int nget = sscanf(line,
            "%lf %lf",
            &tidestation_time_d[ntidestation],
            &tidestation_tide[ntidestation]);
          if (nget == 2)
            tidestation_ok = true;
        } else if (tidestation_format == 2) {
          // deal with tide station data in form: yr mon day hour min sec tide
          const int nget = sscanf(line,
            "%d %d %d %d %d %lf %lf",
            &time_i[0],
            &time_i[1],
            &time_i[2],
            &time_i[3],
            &time_i[4],
            &sec,
            &tidestation_tide[ntidestation]);
          time_i[5] = (int)sec;
          time_i[6] = 1000000 * (sec - time_i[5]);
          mb_get_time(verbose, time_i, &time_d);
          tidestation_time_d[ntidestation] = time_d;
          if (nget == 7)
            tidestation_ok = true;
        } else if (tidestation_format == 3) {
          /* deal with tide station data in form: yr jday hour min sec tide */
          const int nget = sscanf(line,
            "%d %d %d %d %lf %lf",
            &time_j[0],
            &time_j[1],
            &ihr,
            &time_j[2],
            &sec,
            &tidestation_tide[ntidestation]);
          time_j[2] = time_j[2] + 60 * ihr;
          time_j[3] = (int)sec;
          time_j[4] = 1000000 * (sec - time_j[3]);
          mb_get_itime(verbose, time_j, time_i);
          mb_get_time(verbose, time_i, &time_d);
          tidestation_time_d[ntidestation] = time_d;
          if (nget == 6)
            tidestation_ok = true;
        } else if (tidestation_format == 4) {
          /* deal with tide station data in form: yr jday daymin sec tide */
          const int nget = sscanf(line,
            "%d %d %d %lf %lf",
            &time_j[0],
            &time_j[1],
            &time_j[2],
            &sec,
            &tidestation_tide[ntidestation]);
          time_j[3] = (int)sec;
          time_j[4] = 1000000 * (sec - time_j[3]);
          mb_get_itime(verbose, time_j, time_i);
          mb_get_time(verbose, time_i, &time_d);
          tidestation_time_d[ntidestation] = time_d;
          if (nget == 5)
            tidestation_ok = true;
          }
        }

      /* output some debug values */
      if (verbose >= 5 && tidestation_ok)
        {
        fprintf(stderr, "\ndbg5  New tide point read in program <%s>\n", program_name);
        fprintf(stderr, "dbg5       tide[%d]: %f %f\n", ntidestation,
          tidestation_time_d[ntidestation], tidestation_tide[ntidestation]);
        }
      else if (verbose >= 5)
        {
        fprintf(stderr,
          "\ndbg5  Error parsing line in tide file in program <%s>\n",
          program_name);
        fprintf(stderr, "dbg5       line: %s\n", line);
        }

      /* check for reverses or repeats in time */
      if (tidestation_ok) {
        if (ntidestation == 0) {
          ntidestation++;
        } else if (tidestation_time_d[ntidestation] > tidestation_time_d[ntidestation - 1]) {
          ntidestation++;
        } else if (ntidestation > 0 &&
                   tidestation_time_d[ntidestation] <= tidestation_time_d[ntidestation - 1] &&
                   verbose >= 5) {
          fprintf(stderr, "\ndbg5  Tide time error in program <%s>\n", program_name);
          fprintf(stderr,
                  "dbg5       tide[%d]: %f %f\n",
                  ntidestation - 1,
                  tidestation_time_d[ntidestation - 1],
                  tidestation_tide[ntidestation - 1]);
          fprintf(stderr,
                  "dbg5       tide[%d]: %f %f\n",
                  ntidestation,
                  tidestation_time_d[ntidestation],
                  tidestation_tide[ntidestation]);
        }
      }
      strncpy(line, "", sizeof(line));
    }
    fclose(tfp);

    /* now get the model tide value at the tide station's own location and
       times, so the difference from the tide station's observed values can
       be used later as a correction applied to tide models calculated
       elsewhere */
    ngood = 0;
    for (int i = 0; i < ntidestation; i++) {
      int ok = 0;
      int perror = MB_ERROR_NO_ERROR;
      double tide_val = 0.0;
      mb_otps_predict(verbose, &model, tidestation_lon, tidestation_lat,
                       tidestation_time_d[i], &ok, &tide_val, &perror);
      if (ok) {
        tidestation_model[i] = tide_val;
        tidestation_correction[i] = tidestation_tide[i] - tidestation_model[i];
        ngood++;
      }
    }
    if (ngood != ntidestation)
      {
      error = MB_ERROR_BAD_FORMAT;
      fprintf(stderr,
        "\nNumber of tide station values does not match number of model values <%d != %d>\n",
        ntidestation,
        ngood);
      fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
      Return(GMT_RUNTIME_ERROR);
      }

    /* get start end min max of tide station data */
    double tidestation_d_min = 0.0;
    double tidestation_d_max = 0.0;
    double tidestation_m_min = 0.0;
    double tidestation_m_max = 0.0;
    double tidestation_c_min = 0.0;
    double tidestation_c_max = 0.0;
    double tidestation_stime_d;
    double tidestation_etime_d;
    for (int i = 0; i < ntidestation; i++) {
      if (i == 0) {
        tidestation_d_min = tidestation_tide[i];
        tidestation_d_max = tidestation_tide[i];
        tidestation_m_min = tidestation_model[i];
        tidestation_m_max = tidestation_model[i];
        tidestation_c_min = tidestation_correction[i];
        tidestation_c_max = tidestation_correction[i];
        tidestation_stime_d = tidestation_time_d[i];
      } else {
        tidestation_d_min = MIN(tidestation_tide[i], tidestation_d_min);
        tidestation_d_max = MAX(tidestation_tide[i], tidestation_d_max);
        tidestation_m_min = MIN(tidestation_model[i], tidestation_m_min);
        tidestation_m_max = MAX(tidestation_model[i], tidestation_m_max);
        tidestation_c_min = MIN(tidestation_correction[i], tidestation_c_min);
        tidestation_c_max = MAX(tidestation_correction[i], tidestation_c_max);
        tidestation_etime_d = tidestation_time_d[i];
      }
    }
    int tidestation_stime_i[7];
    mb_get_date(verbose, tidestation_stime_d, tidestation_stime_i);
    int tidestation_etime_i[7];
    mb_get_date(verbose, tidestation_etime_d, tidestation_etime_i);

    /* output info on tide station data */
    if (verbose > 0 && mbotps_mode & MBOTPS_MODE_TIDESTATION) {
      fprintf(stderr, "\nTide station data file:             %s\n", tidestation_file);
      fprintf(stderr, "  Tide station longitude:           %f\n", tidestation_lon);
      fprintf(stderr, "  Tide station latitude:            %f\n", tidestation_lat);
      fprintf(stderr, "  Tide station format:              %d\n", tidestation_format);
      fprintf(stderr, "  Tide station data summary:\n");
      fprintf(stderr, "    Number of samples:              %d\n", ntidestation);
      fprintf(stderr,
              "    Start time:                     %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
              tidestation_stime_i[0],
              tidestation_stime_i[1],
              tidestation_stime_i[2],
              tidestation_stime_i[3],
              tidestation_stime_i[4],
              tidestation_stime_i[5],
              tidestation_stime_i[6]);
      fprintf(stderr,
              "    End time:                       %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
              tidestation_etime_i[0],
              tidestation_etime_i[1],
              tidestation_etime_i[2],
              tidestation_etime_i[3],
              tidestation_etime_i[4],
              tidestation_etime_i[5],
              tidestation_etime_i[6]);
      fprintf(stderr, "    Minimum values:     %7.3f %7.3f %7.3f\n",
              tidestation_d_min, tidestation_m_min, tidestation_c_min);
      fprintf(stderr, "    Maximum values:     %7.3f %7.3f %7.3f\n",
              tidestation_d_max, tidestation_m_max, tidestation_c_max);
    }
  }

  double file_weight;
  mb_path swath_file;
  mb_path file;
  mb_path dfile;
  int beams_bath;
  int beams_amp;
  int pixels_ss;

  void *mbio_ptr = NULL;
  void *store_ptr = NULL;
  int kind;
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

  /* mbotps control parameters */
  double btime_d;
  double etime_d;

  /* tide station data */
  int intstat;
  int itime;
  double correction;

  struct stat file_status;
  bool proceed = true;
  int input_size, input_modtime, output_size, output_modtime;

  /* -------------------------------------------------------------------------
   * calculate tide model  for a single position and time range
   * -----------------------------------------------------------------------*/
  if (!(mbotps_mode & MBOTPS_MODE_NAVIGATION))
    {
    /* make sure longitude is positive */
    if (tidelon < 0.0)
      tidelon += 360.0;

    FILE *ofp = NULL;
    if ((ofp = fopen(tide_file, "w")) == NULL) {
      fprintf(stderr, "\nUnable to open tide output file <%s>\n", tide_file);
      fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
      Return(GMT_ERROR_ON_FOPEN);
    }
    mbotps_write_header(ofp, otps_model, &model, tideformat);

    /* loop over the time of interest, predicting and writing out the tide
       model value at each sample */
    mb_get_time(verbose, btime_i, &btime_d);
    mb_get_time(verbose, etime_i, &etime_d);
    const int ntime = 1 + (int)floor((etime_d - btime_d) / interval);
    for (int i = 0; i < ntime; i++)
      {
      time_d = btime_d + i * interval;

      int ok = 0;
      int perror = MB_ERROR_NO_ERROR;
      mb_otps_predict(verbose, &model, tidelon, tidelat, time_d, &ok, &tide, &perror);
      if (!ok)
        continue;

      /* if tide station data have been loaded, interpolate the
       * correction value to apply to the tide model */
      if (mbotps_mode & MBOTPS_MODE_TIDESTATION && (ntidestation > 0))
        {
        intstat = mb_linear_interp(verbose,
          tidestation_time_d - 1,
          tidestation_correction - 1,
          ntidestation,
          time_d,
          &correction,
          &itime,
          &perror);
        if (intstat == MB_SUCCESS)
          tide += correction;
        }

      /* write out the tide model */
      if (tideformat == 2)
        {
        mb_get_date(verbose, time_d, time_i);
        fprintf(ofp,
          "%4.4d %2.2d %2.2d %2.2d %2.2d %2.2d %9.4f\n",
          time_i[0],
          time_i[1],
          time_i[2],
          time_i[3],
          time_i[4],
          time_i[5],
          tide);
        }
      else
        {
        fprintf(ofp, "%.3f %9.4f\n", time_d, tide);
        }
      }
    fclose(ofp);

    /* some helpful output */
    fprintf(stderr, "\nResults are in %s\n", tide_file);
    }  /* end single position mode */

  /* -------------------------------------------------------------------------
   * else get tides along the navigation contained in a set of swath files
   * -----------------------------------------------------------------------*/
  else if (mbotps_mode & MBOTPS_MODE_NAVIGATION)
    {
    fprintf(stderr, "\nModel tide for swath data referenced by %s\n", read_file);
    if (mbotps_mode & MBOTPS_MODE_TIDESTATION && (ntidestation > 0)) {
      fprintf(stderr, " - Also apply tide station correction\n");
    }
    if (mbprocess_update) {
      fprintf(stderr, " - Set mbprocess parameter files to apply tide correction\n");
      }
    fprintf(stderr, "\n");

    /* get format if required */
    if (format == 0)
      mb_get_format(verbose, read_file, NULL, &format, &error);

    /* determine whether to read one file or a list of files */
    const bool read_datalist = format < 0;
    void *datalist;
    bool read_data;

    /* open file list */
    if (read_datalist) {
      const int look_processed = MB_DATALIST_LOOK_UNSET;
      status = mb_datalist_open(verbose, &datalist, read_file, look_processed, &error);
      if (status != MB_SUCCESS) {
        fprintf(stderr, "\nUnable to open data list file: %s\n", read_file);
        fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
        Return(GMT_ERROR_ON_FOPEN);
      }
      if ((status =
        mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight,
          &error)) == MB_SUCCESS)
        read_data = true;
      else
        read_data = false;
      }
    /* else copy single filename to be read */
    else
      {
      strcpy(file, read_file);
      read_data = true;
      }

    double savetime_d;
    double lasttime_d;
    double lastlon;
    double lastlat;
    /* loop over all files to be read */
    while (read_data) {
      /* Figure out if the file needs a tide model - don't generate a new tide
         model if one was made previously and is up to date AND the
         appropriate request has been made */
      proceed = true;
      mb_pathplus tides_file = "";
      snprintf(tides_file, sizeof(tides_file), "%s.tde", file);
      if (skip_existing) {
        const int fstat1 = stat(file, &file_status);
        if (fstat1 == 0 && (file_status.st_mode & S_IFMT) != S_IFDIR) {
          input_modtime = file_status.st_mtime;
          input_size = file_status.st_size;
        } else {
          input_modtime = 0;
          input_size = 0;
        }
        const int fstat2 = stat(tides_file, &file_status);
        if (fstat2 == 0 && (file_status.st_mode & S_IFMT) != S_IFDIR) {
          output_modtime = file_status.st_mtime;
          output_size = file_status.st_size;
        } else {
          output_modtime = 0;
          output_size = 0;
        }
        if (output_modtime > input_modtime && input_size > 0 && output_size > 0)
          proceed = false;
      }

      /* skip the file */
      if (!proceed) {
        /* some helpful output */
        fprintf(stderr, "%s : skipped - tide model file is up to date\n", file);
      }

      /* predict and write out the tide model for this swath file's
         navigation */
      else {

        /* read fnv file if possible */
        strcpy(swath_file, file);
        mb_get_fnv(verbose, file, &format, &error);

        /* initialize reading the swath file */
        if ((status =
          mb_read_init(verbose, file, format, pings, lonflip, bounds, btime_i, etime_i,
            speedmin, timegap, &mbio_ptr, &btime_d, &etime_d, &beams_bath, &beams_amp,
            &pixels_ss, &error)) !=MB_SUCCESS) {
          char *message;
          mb_error(verbose, error, &message);
          fprintf(stderr,
            "\nMBIO Error returned from function <mb_read_init>:\n%s\n",
            message);
          fprintf(stderr, "\nMultibeam File <%s> not initialized for reading\n", file);
          fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
          Return(GMT_RUNTIME_ERROR);
        }

        /* allocate memory for data arrays */
        if (error == MB_ERROR_NO_ERROR) {
          // status =
          mb_register_array(verbose,
            mbio_ptr,
            MB_MEM_TYPE_BATHYMETRY,
            sizeof(char),
            (void **)&beamflag,
            &error);
        }
        if (error == MB_ERROR_NO_ERROR) {
          // status =
            mb_register_array(verbose,
            mbio_ptr,
            MB_MEM_TYPE_BATHYMETRY,
            sizeof(double),
            (void **)&bath,
            &error);
        }
        if (error == MB_ERROR_NO_ERROR) {
          // status =
          mb_register_array(verbose,
            mbio_ptr,
            MB_MEM_TYPE_AMPLITUDE,
            sizeof(double),
            (void **)&amp,
            &error);
        }
        if (error == MB_ERROR_NO_ERROR) {
          // status =
          mb_register_array(verbose,
            mbio_ptr,
            MB_MEM_TYPE_BATHYMETRY,
            sizeof(double),
            (void **)&bathacrosstrack,
            &error);
        }
        if (error == MB_ERROR_NO_ERROR) {
          // status =
          mb_register_array(verbose,
            mbio_ptr,
            MB_MEM_TYPE_BATHYMETRY,
            sizeof(double),
            (void **)&bathalongtrack,
            &error);
        }
        if (error == MB_ERROR_NO_ERROR) {
          // status =
          mb_register_array(verbose,
            mbio_ptr,
            MB_MEM_TYPE_SIDESCAN,
            sizeof(double),
            (void **)&ss,
            &error);
        }
        if (error == MB_ERROR_NO_ERROR) {
          // status =
          mb_register_array(verbose,
            mbio_ptr,
            MB_MEM_TYPE_SIDESCAN,
            sizeof(double),
            (void **)&ssacrosstrack,
            &error);
        }
        if (error == MB_ERROR_NO_ERROR) {
          // status =
          mb_register_array(verbose,
            mbio_ptr,
            MB_MEM_TYPE_SIDESCAN,
            sizeof(double),
            (void **)&ssalongtrack,
            &error);
        }

        /* if error initializing memory then quit */
        if (error != MB_ERROR_NO_ERROR) {
          char *message;
          mb_error(verbose, error, &message);
          fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
          fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
          Return(GMT_MEMORY_ERROR);
        }

        /* open this swath file's own tide output file */
        FILE *ofp = NULL;
        if ((ofp = fopen(tides_file, "w")) == NULL) {
          fprintf(stderr, "\nUnable to open tide output file <%s>\n", tides_file);
          fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
          Return(GMT_ERROR_ON_FOPEN);
        }
        mbotps_write_header(ofp, otps_model, &model, tideformat);

        /* set mbprocess usage of tide file */
        if (mbprocess_update) {
          status = mb_pr_update_tide(verbose,
            swath_file,
            MBP_TIDE_ON,
            tides_file,
            tideformat,
            &error);
        }

        /* read and use data */
        int nread = 0;
        int nuse = 0;
        while (error <= MB_ERROR_NO_ERROR) {
          /* reset error */
          error = MB_ERROR_NO_ERROR;
          bool output = false;

          /* read next data record */
          status = mb_get_all(verbose,
            mbio_ptr,
            &store_ptr,
            &kind,
            time_i,
            &time_d,
            &navlon,
            &navlat,
            &speed,
            &heading,
            &distance,
            &altitude,
            &sensordepth,
            &beams_bath,
            &beams_amp,
            &pixels_ss,
            beamflag,
            bath,
            amp,
            bathacrosstrack,
            bathalongtrack,
            ss,
            ssacrosstrack,
            ssalongtrack,
            comment,
            &error);

          /* print debug statements */
          if (verbose >= 2) {
            fprintf(stderr, "\ndbg2  Ping read in program <%s>\n", program_name);
            fprintf(stderr, "dbg2       kind:           %d\n", kind);
            fprintf(stderr, "dbg2       error:          %d\n", error);
            fprintf(stderr, "dbg2       status:         %d\n", status);
            }

          /* deal with nav and time from survey data only - not nav, sidescan, or
             subbottom */
          if (error <= MB_ERROR_NO_ERROR && kind == MB_DATA_DATA) {
            /* flag positions and times for output at specified interval */
            if (nread == 0 || time_d - savetime_d >= interval) {
              savetime_d = time_d;
              output = true;
            }
            lasttime_d = time_d;
            lastlon = navlon;
            lastlat = navlat;

            /* increment counter */
            nread++;
          }

          /* predict and write out the tide value if flagged or end of file */
          if (output || error == MB_ERROR_EOF) {
            if (lastlon < 0.0)
              lastlon += 360.0;

            int ok = 0;
            int perror = MB_ERROR_NO_ERROR;
            mb_otps_predict(verbose, &model, lastlon, lastlat, lasttime_d, &ok, &tide, &perror);
            if (!ok) {
              fprintf(stderr,
                "Skipping data: position %f %f is outside the model grid or located on land\n",
                lastlon, lastlat);
            }
            else {
              /* if tide station data have been loaded, interpolate the
               * correction value to apply to the tide model */
              if (mbotps_mode & MBOTPS_MODE_TIDESTATION && (ntidestation > 0)) {
                intstat = mb_linear_interp(verbose,
                                            tidestation_time_d - 1,
                                            tidestation_correction - 1,
                                            ntidestation,
                                            lasttime_d,
                                            &correction,
                                            &itime,
                                            &perror);
                if (intstat == MB_SUCCESS)
                  tide += correction;
              }

              if (tideformat == 2) {
                mb_get_date(verbose, lasttime_d, time_i);
                fprintf(ofp,
                  "%4.4d %2.2d %2.2d %2.2d %2.2d %2.2d %9.4f\n",
                  time_i[0],
                  time_i[1],
                  time_i[2],
                  time_i[3],
                  time_i[4],
                  time_i[5],
                  tide);
              } else {
                fprintf(ofp, "%.3f %9.4f\n", lasttime_d, tide);
              }
              nuse++;
            }
            }
          }

        /* close the swath file and its tide output file */
        status = mb_close(verbose, &mbio_ptr, &error);
        fclose(ofp);

        /* output read statistics */
        fprintf(stderr, "%s : model tide at %d of %d records\n", file, nuse, nread);
        }

      /* figure out whether and what to read next */
      if (read_datalist) {
        if ((status =
          mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight,
            &error)) == MB_SUCCESS)
          read_data = true;
        else
          read_data = false;
      } else {
        read_data = false;
      }

      /* end loop over files in list */
      }
    if (read_datalist)
      mb_datalist_close(verbose, &datalist, &error);
  }

  /* release the tidal prediction model */
  mb_otps_model_close(verbose, &model, &error);

  /* check memory */
  if (verbose >= 4)
    status = mb_memory_list(verbose, &error);

  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
    fprintf(stderr, "dbg2  Ending status:\n");
    fprintf(stderr, "dbg2       status:  %d\n", status);
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
