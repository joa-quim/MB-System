/*--------------------------------------------------------------------
 *    The MB-system:  mbclean.c   (GMT-module rewrite of mbclean.cc)
 *
 *    Original mbclean.c (1993-2015) by D. W. Caress and D. N. Chayes.
 *    GMT-plugin form (mbclean_j.c) by J. Luis 2015.
 *    Re-converted from current upstream src/utilities/mbclean.cc back to
 *    C and wrapped as a GMT module entry so it can be invoked from the
 *    GMT API (and therefore from Julia FFI / Matlab MEX via GMT).
 *
 *    Copyright (c) 1993-2026 by
 *    David W. Caress (caress@mbari.org)  — MBARI
 *    Dale N. Chayes  — University of New Hampshire CCOM
 *    Christian dos Santos Ferreira  — MARUM
 *
 *    See README.md for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * mbclean identifies and flags artifacts in swath sonar bathymetry data.
 *
 * Flagging algorithm order:
 *   1. Flag specified number of outer beams (-X).
 *   2. Flag beams outside acceptable depth range (-B).
 *   3. Flag beams outside fractional acceptable depth range (-G).
 *   4. Flag beams outside deviation from local median depth (-A).
 *   5. Flag beams associated with excessive slopes (-C, default).
 *   6. Zap "rails" (-Q).
 *   7. Flag pings with too few good soundings (-U).
 *   Plus: -E long acrosstrack, -K min range, -N ping-deviation tolerance,
 *         -P speed range, -R heading rate, -S spike, -T edit-timestamp fix,
 *         -W position bounds, -Y angle/distance flag/unflag, -Z zero-position.
 */

#define THIS_MODULE_NAME    "mbclean"
#define THIS_MODULE_LIB	    "mbsystem"
#define THIS_MODULE_PURPOSE "Identifies and flags artifacts in swath sonar bathymetry data"
/* Primary input is the swath file or datalist given with -I; edits .esf edit-save files in place; nothing is returned. */
#define THIS_MODULE_KEYS    "ID{"
#define THIS_MODULE_NEEDS		""
#define THIS_MODULE_OPTIONS		"->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#include "mb_status.h"
#include "mb_format.h"
#include "mb_define.h"
#include "mb_io.h"
#include "mb_swap.h"
#include "mb_process.h"
#include "mb_aux.h"
#include "mb_cube.h"
#include "mb_gmt_opts.h"

#include <sys/stat.h>
#include <time.h>

/* --- local defines / enums ------------------------------------------- */

#define MBCLEAN_FLAG_ONE   1
#define MBCLEAN_FLAG_BOTH  2

/* Y-option sub-modes (extended from original -Y two-arg form). */
#define MBCLEAN_Y_MODE_DISTANCE_FLAG   1
#define MBCLEAN_Y_MODE_DISTANCE_UNFLAG 2
#define MBCLEAN_Y_MODE_ANGLE_FLAG      3
#define MBCLEAN_Y_MODE_ANGLE_UNFLAG    4

#define MBCLEAN_NOACTION 0

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* --- structs --------------------------------------------------------- */

struct mbclean_ping_struct {
	int    time_i[7];
	double time_d;
	int    multiplicity;
	double navlon;
	double navlat;
	double speed;
	double heading;
	int    beams_bath;
	char  *beamflag;
	char  *beamflagorg;
	double *bath;
	double *bathacrosstrack;
	double *bathalongtrack;
	double *bathx;
	double *bathy;
};

struct bad_struct {
	bool   flag;
	int    ping;
	int    beam;
	double bath;
};

/* no data value of the CUBE predicted surface */
#define MBCLEAN_CUBE_NODATA 99999.0f

/* CUBE flagging criteria */
#define MBCLEAN_CUBE_FLAG_HYPOTHESIS 1	/* sounding belongs to a hypothesis not selected */
#define MBCLEAN_CUBE_FLAG_OUTLIER    2	/* sounding is a single sample outlier from the selected one */

/* CUBE flagging options, the program's --cube-* */
struct mbclean_cube_struct {
	struct mb_cube_options_struct options;	/* CUBE grid options shared with mbgrid -F9 */
	bool   use;					/* CUBE flagging requested with --cube-spacing */
	double dx;					/* node spacing east in m */
	double dy;					/* node spacing north in m, 0 for dx */
	bool   region_set;			/* grid bounds given with --cube-region */
	double region[4];			/* grid bounds west/east/south/north in degrees */
	int    flag;				/* criteria, MBCLEAN_CUBE_FLAG_* bits; default outlier */
	bool   threshold_set;		/* outlier threshold given with --cube-flag-threshold */
	double threshold;			/* outlier threshold as a normalized error */
	double strength_max;		/* largest node strength where the hypothesis criterion applies */
	bool   prediction_topography;	/* the predicted surface grid holds heights, positive up */
	bool   nadir_set;			/* nadir sector given with --cube-flag-nadir */
	double nadir_angle;			/* half-width of the nadir sector in degrees from vertical */
	double nadir_threshold;		/* outlier threshold in the nadir sector as a normalized error */
	bool   nadir_both;			/* nadir test two-sided; default only soundings shallower than the hypothesis */
};

EXTERN_MSC int GMT_mbclean(void *API, int mode, void *args);

/* --- Control structure ----------------------------------------------- */

struct MBCLEAN_CTRL {
	int verbose;	/* the program's -V/-v count */
	struct mbc_H { bool active; } H;

	struct mbcln_A { bool active; double deviation_max; } A;
	struct mbcln_B { bool active; double depth_low, depth_high; } B;
	struct mbcln_C { bool active; int slope_form; double slopemax; } C;
	struct mbcln_D { bool active; double distancemin, distancemax; } D;
	struct mbcln_E { bool active; double max_acrosstrack; } E;
	struct mbcln_F { bool active; int format; } F;
	struct mbcln_G { bool active; double fraction_low, fraction_high; } G;
	struct mbcln_I { bool active; char *inputfile; } I;
	struct mbcln_K { bool active; double range_min; } K;
	struct mbcln_L { bool active; int lonflip; } L;
	struct mbcln_M { bool active; int mode; } M;
	struct mbcln_N { bool active; double ping_deviation_tolerance; } N;
	struct mbcln_P { bool active; double speed_low, speed_high; } P;
	struct mbcln_Q { bool active; double backup_dist; } Q;
	struct mbcln_R { bool active; double max_heading_rate; } R;
	struct mbcln_S { bool active; int spike_mode, slope_form; double spikemax; } S;
	struct mbcln_T { bool active; double tolerance; } T;
	struct mbcln_U { bool active; int num_good_min; } U;
	struct mbcln_W { bool active; double west, east, south, north; } W;
	struct mbcln_X { bool active; int zap_beams_left, zap_beams_right; } X;
	struct mbcln_Y {
		bool   active;
		int    y_mode;
		double flag_distance_left, flag_distance_right;
		double unflag_distance_left, unflag_distance_right;
		double flag_angle_left, flag_angle_right;
		double unflag_angle_left, unflag_angle_right;
		bool   flag_distance, unflag_distance, flag_angle, unflag_angle;
	} Y;
	struct mbcln_Z { bool active; } Z;

	/* Aggregated "did the user request this check?" flags, set during parse. */
	struct mbcln_tranf {
		bool check_deviation;
		bool check_range;
		bool check_slope;
		bool zap_long_across;
		bool check_fraction;
		bool check_range_min;
		bool check_speed_good;
		bool zap_rails;
		bool zap_max_heading_rate;
		bool check_spike;
		bool fix_edit_timestamps;
		bool check_position_bounds;
		bool zap_beams;
		bool check_zero_position;
		bool check_num_good_min;
		bool check_ping_deviation;
	} transf;

	struct mbclean_cube_struct cube;	/* CUBE flagging, the program's --cube-* options */
};

/* --- forward decls --------------------------------------------------- */

static int mbclean_save_edit(int verbose, FILE *sofp, double time_d,
							 int beam, int action, int *error);

/* --- ctor / dtor / usage --------------------------------------------- */

static void *New_mbclean_Ctrl(struct GMT_CTRL *GMT) {
	struct MBCLEAN_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBCLEAN_CTRL);

	/* Defaults */
	Ctrl->C.slopemax        = 1.0;
	Ctrl->D.distancemin     = 0.01;
	Ctrl->D.distancemax     = 0.25;
	Ctrl->E.max_acrosstrack = 120.0;
	Ctrl->F.format          = 0;
	Ctrl->M.mode            = MBCLEAN_FLAG_ONE;
	Ctrl->N.ping_deviation_tolerance = 1.0;
	Ctrl->S.spikemax        = 1.0;
	Ctrl->S.spike_mode      = 1;

	/* CUBE flagging defaults, as the program */
	{
		int cube_error = MB_ERROR_NO_ERROR;
		mb_cube_default_options(0, &Ctrl->cube.options, &cube_error);
	}
	Ctrl->cube.flag         = MBCLEAN_CUBE_FLAG_OUTLIER;
	Ctrl->cube.strength_max = 5.0;

	return Ctrl;
}

static void Free_mbclean_Ctrl(struct GMT_CTRL *GMT, struct MBCLEAN_CTRL *Ctrl) {
	if (!Ctrl) return;
	if (Ctrl->I.inputfile) free(Ctrl->I.inputfile);
	gmt_M_free(GMT, Ctrl);
}

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'v', "verbose",                  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",                     "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'W', "bounds",                   "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'B', "depth-range",              "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'A', "deviation-max",            "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'D', "distance-range",           "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'Y', "flag-distance-angle",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'F', "format",                   "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'G', "fraction-range",           "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",                    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'L', "lonflip",                  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'E', "max-acrosstrack",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'R', "max-heading-rate",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'C', "max-slope",                "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'U', "min-good-beams",           "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'M', "mode",                     "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'N', "ping-deviation-tolerance", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'K', "range-min",                "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'P', "speed-range",              "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'S', "spike-max",                "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'T', "timestamp-tolerance",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'X', "zap-beams",                "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'Q', "zap-rails",                "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'Z', "zero-position",            "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

/* The program's CUBE flagging long options, which have no short letter. The first seven are
   mbclean's own; the rest are handed to the CUBE engine as they are */
static const struct MB_GMT_LONGOPT_DEF long_options[] = {
	{"cube-spacing", true},
	{"cube-region", true},
	{"cube-flag", true},
	{"cube-flag-nadir", true},
	{"cube-flag-threshold", true},
	{"cube-flag-strength-max", true},
	{"cube-prediction-topography", false},
	{"cube-bayes-factor", true},
	{"cube-blunder", true},
	{"cube-capture-min", true},
	{"cube-capture-scale", true},
	{"cube-context", true},
	{"cube-depth-reference", true},
	{"cube-dist-exp", true},
	{"cube-est-offset", true},
	{"cube-extractor", true},
	{"cube-horiz-scale", true},
	{"cube-iho-ccom", false},
	{"cube-iho-edition", true},
	{"cube-influence-scale", true},
	{"cube-median-length", true},
	{"cube-memory-limit", true},
	{"cube-model", true},
	{"cube-model-a", true},
	{"cube-model-b", true},
	{"cube-model-c", true},
	{"cube-order", true},
	{"cube-prediction", true},
	{"cube-prediction-hypotheses", true},
	{"cube-prediction-uncertainty", true},
	{"cube-quotient-limit", true},
	{"cube-runlength", true},
	{"cube-sigma-h", true},
	{"cube-sigma-v", true},
	{NULL, false}};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;

	GMT_Message(API, GMT_TIME_NONE,
		"usage: mbclean -I<inputfile> [-Adev_max] [-Blow/high] [-Cslope/unit]\n"
		"\t[-Dmin/max] [-E<max_acrosstrack>] [-Fformat] [-Gfrac_low/frac_high]\n"
		"\t[-K<min_range>] [-Llonflip] [-Mmode] [-N<ping_dev_tol>]\n"
		"\t[-Pmin_speed/max_speed] [-Q[<backup>]] [-R<max_heading_rate>]\n"
		"\t[-Sspike/mode/form] [-Ttolerance] [-Unum_good_min]\n"
		"\t[-Wwest/east/south/north] [-Xleft[/right]] [-Yleft/right[/mode]] [-Z]\n"
		"\t[-V -H]\n"
		"   or: mbclean -I<inputfile> --cube-spacing=dx[/dy] [--cube-region=west/east/south/north]\n"
		"\t[--cube-flag=hypothesis|outlier|both] [--cube-flag-nadir=angle/threshold[/shallow|both]]\n"
		"\t[--cube-flag-strength-max=strength] [--cube-flag-threshold=threshold]\n"
		"\t[--cube-prediction-topography] [--cube-* engine options]\n\n");
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;

	GMT_Message(API, GMT_TIME_NONE,
		"\t<inputfile> is an MB-System datalist referencing the swath data [datalist.mb-1].\n"
		"\tEvery option also has the program's lower-case and long forms.\n"
		"\t--cube-spacing turns on CUBE flagging, which cannot be combined with the other tests.\n"
		"\tThe CUBE engine options are those of the program: --cube-bayes-factor --cube-blunder\n"
		"\t--cube-capture-min --cube-capture-scale --cube-context --cube-depth-reference --cube-dist-exp\n"
		"\t--cube-est-offset --cube-extractor --cube-horiz-scale --cube-iho-ccom --cube-iho-edition\n"
		"\t--cube-influence-scale --cube-median-length --cube-memory-limit --cube-model --cube-model-a\n"
		"\t--cube-model-b --cube-model-c --cube-order --cube-prediction --cube-prediction-hypotheses\n"
		"\t--cube-prediction-uncertainty --cube-quotient-limit --cube-runlength --cube-sigma-h\n"
		"\t--cube-sigma-v; see the mbclean manual page.\n\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

/* --- parse ----------------------------------------------------------- */

static int parse(struct GMT_CTRL *GMT, struct MBCLEAN_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0, n_files = 0;
	int    n;
	struct GMT_OPTION *opt;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (opt = options; opt; opt = opt->next) {
		if (opt->option == MB_GMT_LONGOPT) {	/* a CUBE long option, parsed as the program does */
			struct mbclean_cube_struct *cube = &Ctrl->cube;
			const char *value;
			char extra;
			const int k = mb_gmt_long_option(opt, long_options, &value);
			if (k < 0) {
				GMT_Report(API, GMT_MSG_NORMAL, "Option --%s %s\n", opt->arg, k == -2 ? "requires an argument" : "is not recognized");
				n_errors++;
				continue;
			}
			const char *name = long_options[k].name;
			bool bad = false;
			if (strcmp(name, "cube-spacing") == 0) {
				n = sscanf(value, "%lf/%lf%c", &cube->dx, &cube->dy, &extra);
				bad = n < 1 || n > 2 || (n == 1 && strchr(value, '/') != NULL);
				cube->use = true;
			}
			else if (strcmp(name, "cube-region") == 0) {
				bad = sscanf(value, "%lf/%lf/%lf/%lf%c", &cube->region[0], &cube->region[1], &cube->region[2],
				             &cube->region[3], &extra) != 4;
				cube->region_set = true;
			}
			else if (strcmp(name, "cube-flag") == 0) {
				if (strcmp(value, "hypothesis") == 0)
					cube->flag = MBCLEAN_CUBE_FLAG_HYPOTHESIS;
				else if (strcmp(value, "outlier") == 0)
					cube->flag = MBCLEAN_CUBE_FLAG_OUTLIER;
				else if (strcmp(value, "both") == 0)
					cube->flag = MBCLEAN_CUBE_FLAG_HYPOTHESIS | MBCLEAN_CUBE_FLAG_OUTLIER;
				else
					bad = true;
			}
			else if (strcmp(name, "cube-flag-nadir") == 0) {
				char separator = '\0';
				char nadir_mode[16] = "";
				n = sscanf(value, "%lf/%lf%c%15[a-z]%c", &cube->nadir_angle, &cube->nadir_threshold, &separator, nadir_mode,
				           &extra);
				if (n == 2)
					cube->nadir_both = false;
				else if (n == 4 && separator == '/' && strcmp(nadir_mode, "shallow") == 0)
					cube->nadir_both = false;
				else if (n == 4 && separator == '/' && strcmp(nadir_mode, "both") == 0)
					cube->nadir_both = true;
				else
					bad = true;
				cube->nadir_set = true;
			}
			else if (strcmp(name, "cube-flag-threshold") == 0) {
				bad = sscanf(value, "%lf%c", &cube->threshold, &extra) != 1;
				cube->threshold_set = true;
			}
			else if (strcmp(name, "cube-flag-strength-max") == 0) {
				bad = sscanf(value, "%lf%c", &cube->strength_max, &extra) != 1;
			}
			else if (strcmp(name, "cube-prediction-topography") == 0) {
				cube->prediction_topography = true;
			}
			else {
				int cube_error = MB_ERROR_NO_ERROR;
				bad = mb_cube_parse_option(0, name, long_options[k].has_arg ? value : NULL, &cube->options,
				                           &cube_error) != MB_SUCCESS;
			}
			if (bad) {
				GMT_Report(API, GMT_MSG_NORMAL, "Invalid CUBE option: --%s%s%s\n", name, long_options[k].has_arg ? "=" : "", value);
				n_errors++;
			}
			continue;
		}
		switch (opt->option) {

		case '<':   /* Input file */
			Ctrl->I.active = true;
			if (gmt_check_filearg(GMT, '<', opt->arg, GMT_IN, GMT_IS_DATASET)) {
				Ctrl->I.inputfile = strdup(opt->arg);
				n_files = 1;
			} else {
				GMT_Report(API, GMT_MSG_NORMAL, "Syntax error: only one input file is allowed.\n");
				{ GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -%c option\n", opt->option); n_errors++; }
			}
			break;

		case 'A':
		case 'a':
			n = sscanf(opt->arg, "%lf", &Ctrl->A.deviation_max);
			if (n > 0) { Ctrl->A.active = true; Ctrl->transf.check_deviation = true; }
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -A option\n"); n_errors++; }
			break;

		case 'B':
		case 'b':
			n = sscanf(opt->arg, "%lf/%lf", &Ctrl->B.depth_low, &Ctrl->B.depth_high);
			if (n > 1) { Ctrl->B.active = true; Ctrl->transf.check_range = true; }
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -B option\n"); n_errors++; }
			break;

		case 'C':
		case 'c':
			Ctrl->C.slope_form = 0;
			n = sscanf(opt->arg, "%lf/%d", &Ctrl->C.slopemax, &Ctrl->C.slope_form);
			if (n > 0) {
				Ctrl->C.active = true;
				Ctrl->transf.check_slope = true;
				if (Ctrl->C.slope_form == 1)      Ctrl->C.slopemax = tan(Ctrl->C.slopemax);
				else if (Ctrl->C.slope_form == 2) Ctrl->C.slopemax = tan(DTR * Ctrl->C.slopemax);
			} else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -C option\n"); n_errors++; }
			break;

		case 'D':
		case 'd':
			n = sscanf(opt->arg, "%lf/%lf", &Ctrl->D.distancemin, &Ctrl->D.distancemax);
			if (n > 1) Ctrl->D.active = true;
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -D option\n"); n_errors++; }
			break;

		case 'E':
		case 'e':
			n = sscanf(opt->arg, "%lf", &Ctrl->E.max_acrosstrack);
			if (n > 0) { Ctrl->E.active = true; Ctrl->transf.zap_long_across = true; }
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -E option\n"); n_errors++; }
			break;

		case 'F':
		case 'f':
			n = sscanf(opt->arg, "%d", &Ctrl->F.format);
			if (n > 0) Ctrl->F.active = true;
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -F option\n"); n_errors++; }
			break;

		case 'G':
		case 'g':
			n = sscanf(opt->arg, "%lf/%lf", &Ctrl->G.fraction_low, &Ctrl->G.fraction_high);
			if (n > 1) { Ctrl->G.active = true; Ctrl->transf.check_fraction = true; }
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -G option\n"); n_errors++; }
			break;

		case 'I':
		case 'i':
			if (!gmt_access(GMT, opt->arg, R_OK)) {
				Ctrl->I.inputfile = strdup(opt->arg);
				Ctrl->I.active = true;
				n_files = 1;
			} else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -I option (file not found)\n"); n_errors++; }
			break;

		case 'K':
		case 'k':
			n = sscanf(opt->arg, "%lf", &Ctrl->K.range_min);
			if (n > 0) { Ctrl->K.active = true; Ctrl->transf.check_range_min = true; }
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -K option\n"); n_errors++; }
			break;

		case 'L':
		case 'l':
			n = sscanf(opt->arg, "%d", &Ctrl->L.lonflip);
			if (n > 0) Ctrl->L.active = true;
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -L option\n"); n_errors++; }
			break;

		case 'M':
		case 'm':
			n = sscanf(opt->arg, "%d", &Ctrl->M.mode);
			if (n > 0) Ctrl->M.active = true;
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -M option\n"); n_errors++; }
			break;

		case 'N':
		case 'n':
			n = sscanf(opt->arg, "%lf", &Ctrl->N.ping_deviation_tolerance);
			if (n > 0) { Ctrl->N.active = true; Ctrl->transf.check_ping_deviation = true; }
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -N option\n"); n_errors++; }
			break;

		case 'P':
		case 'p':
			n = sscanf(opt->arg, "%lf/%lf", &Ctrl->P.speed_low, &Ctrl->P.speed_high);
			if (n > 0) { Ctrl->P.active = true; Ctrl->transf.check_speed_good = true; }
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -P option\n"); n_errors++; }
			break;

		case 'Q':
		case 'q':
			Ctrl->Q.backup_dist = 0.0;
			sscanf(opt->arg, "%lf", &Ctrl->Q.backup_dist);
			Ctrl->Q.active = true;
			Ctrl->transf.zap_rails = true;
			break;

		case 'R':
		case 'r':
			n = sscanf(opt->arg, "%lf", &Ctrl->R.max_heading_rate);
			if (n > 0) { Ctrl->R.active = true; Ctrl->transf.zap_max_heading_rate = true; }
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -R option\n"); n_errors++; }
			break;

		case 'S':
		case 's':
			Ctrl->S.slope_form = 0;
			n = sscanf(opt->arg, "%lf/%d/%d",
					   &Ctrl->S.spikemax, &Ctrl->S.spike_mode, &Ctrl->S.slope_form);
			if (n > 1) {
				Ctrl->S.active = true;
				Ctrl->transf.check_spike = true;
				if (Ctrl->S.slope_form == 1)      Ctrl->S.spikemax = tan(Ctrl->S.spikemax);
				else if (Ctrl->S.slope_form == 2) Ctrl->S.spikemax = tan(DTR * Ctrl->S.spikemax);
			} else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -S option\n"); n_errors++; }
			break;

		case 'T':
		case 't':
			sscanf(opt->arg, "%lf", &Ctrl->T.tolerance);
			Ctrl->T.active = true;
			Ctrl->transf.fix_edit_timestamps = true;
			break;

		case 'U':
		case 'u':
			n = sscanf(opt->arg, "%d", &Ctrl->U.num_good_min);
			if (n > 0) { Ctrl->U.active = true; Ctrl->transf.check_num_good_min = true; }
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -U option\n"); n_errors++; }
			break;

		case 'W':
		case 'w':
			n = sscanf(opt->arg, "%lf/%lf/%lf/%lf",
					   &Ctrl->W.west, &Ctrl->W.east, &Ctrl->W.south, &Ctrl->W.north);
			if (n == 4) { Ctrl->W.active = true; Ctrl->transf.check_position_bounds = true; }
			else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -W option\n"); n_errors++; }
			break;

		case 'X':
		case 'x': {
			int left = 0, right = 0;
			n = sscanf(opt->arg, "%d/%d", &left, &right);
			if (n == 1) right = left;
			Ctrl->X.zap_beams_left  = left;
			Ctrl->X.zap_beams_right = right;
			Ctrl->X.active = true;
			Ctrl->transf.zap_beams = true;
			break;
		}

		case 'Y':
		case 'y': {
			double left = 0.0, right = 0.0;
			int    y_mode_tmp = 0;
			n = sscanf(opt->arg, "%lf/%lf/%d", &left, &right, &y_mode_tmp);
			Ctrl->Y.active = true;
			if (n == 1) {
				Ctrl->Y.flag_distance_left  = -fabs(left);
				Ctrl->Y.flag_distance_right =  fabs(left);
				Ctrl->Y.flag_distance = true;
			} else if (n == 2) {
				Ctrl->Y.flag_distance_left  = left;
				Ctrl->Y.flag_distance_right = right;
				Ctrl->Y.flag_distance = true;
			} else if (n == 3) {
				if (y_mode_tmp < 1 || y_mode_tmp > 4)
					y_mode_tmp = MBCLEAN_Y_MODE_DISTANCE_FLAG;
				Ctrl->Y.y_mode = y_mode_tmp;
				if (y_mode_tmp == MBCLEAN_Y_MODE_DISTANCE_FLAG) {
					Ctrl->Y.flag_distance_left  = left;
					Ctrl->Y.flag_distance_right = right;
					Ctrl->Y.flag_distance = true;
				} else if (y_mode_tmp == MBCLEAN_Y_MODE_DISTANCE_UNFLAG) {
					Ctrl->Y.unflag_distance_left  = left;
					Ctrl->Y.unflag_distance_right = right;
					Ctrl->Y.unflag_distance = true;
				} else if (y_mode_tmp == MBCLEAN_Y_MODE_ANGLE_FLAG) {
					Ctrl->Y.flag_angle_left  = left;
					Ctrl->Y.flag_angle_right = right;
					Ctrl->Y.flag_angle = true;
				} else { /* MBCLEAN_Y_MODE_ANGLE_UNFLAG */
					Ctrl->Y.unflag_angle_left  = left;
					Ctrl->Y.unflag_angle_right = right;
					Ctrl->Y.unflag_angle = true;
				}
			} else { GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -Y option\n"); n_errors++; }
			break;
		}

		case 'Z':
		case 'z':
			Ctrl->Z.active = true;
			Ctrl->transf.check_zero_position = true;
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

	/* no input file is the program's datalist.mb-1, as in the program */
	(void)n_files;

	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

/* --------------------------------------------------------------------
 * mbclean_cube flags the soundings that do not belong to a CUBE surface
 * built from all of the input data, with the grid, options and soundings
 * of mbgrid -F9: a geographic grid with the spacing in meters. The data are
 * read twice, three times with a median predicted surface: the first read
 * builds the CUBE grid, the second compares every sounding with it with
 * mb_cube_classify and appends filter edits to the edit save file of the
 * raw file. Processed files are read where the datalist asks for them; the
 * current edits of the raw file are applied to every read.
 * Returns the exit code of the program, MB_ERROR_NO_ERROR on success.
 * -------------------------------------------------------------------- */
static int mbclean_cube(struct GMTAPI_CTRL *API, int verbose, char *read_file, int format, int lonflip, int pings,
                        int btime_i[7], int etime_i[7], double speedmin, double timegap, bool uselockfiles,
                        struct mbclean_cube_struct *cube) {
	const char *program_name = THIS_MODULE_NAME;
	int error = MB_ERROR_NO_ERROR;
	int status = MB_SUCCESS;
	int exit_code = MB_ERROR_NO_ERROR;
	struct mb_cube_options_struct *options = &cube->options;

	/* everything released on the way out */
	void *cube_ptr = NULL;
	float *pred_depth = NULL;
	void *median_ptr = NULL;
	void *datalist = NULL;
	void *mbio_ptr = NULL;
	struct mb_esf_struct esf;
	bool esf_loaded = false;
	bool file_locked = false;
	char path[MB_PATH_MAXLINE] = "";

	/* resolve and check the CUBE options */
	char message[MB_PATH_MAXLINE];
	bool ok = true;
	if (mb_cube_resolve_options(verbose, options, message, sizeof(message), &error) != MB_SUCCESS) {
		GMT_Report(API, GMT_MSG_NORMAL, "%s\n", message);
		ok = false;
	}
	if (mb_cube_check_options(verbose, program_name, options, message, sizeof(message), &error) != MB_SUCCESS) {
		GMT_Report(API, GMT_MSG_NORMAL, "%s\n", message);
		ok = false;
	}
	if (!(cube->dx > 0.0) || cube->dy < 0.0) {
		GMT_Report(API, GMT_MSG_NORMAL, "The CUBE node spacing --cube-spacing must be positive\n");
		ok = false;
	}
	if (!(cube->strength_max >= 0.0)) {
		GMT_Report(API, GMT_MSG_NORMAL, "The CUBE strength limit --cube-flag-strength-max must not be negative\n");
		ok = false;
	}
	if (cube->threshold_set && !(cube->threshold > 0.0)) {
		GMT_Report(API, GMT_MSG_NORMAL, "The CUBE outlier threshold --cube-flag-threshold must be positive\n");
		ok = false;
	}
	if (cube->nadir_set && (!(cube->nadir_angle > 0.0) || !(cube->nadir_angle < 90.0) || !(cube->nadir_threshold > 0.0))) {
		GMT_Report(API, GMT_MSG_NORMAL, "The CUBE nadir sector --cube-flag-nadir needs an angle between 0 and 90 degrees "
		           "and a positive threshold\n");
		ok = false;
	}
	if (cube->nadir_set && !(cube->flag & MBCLEAN_CUBE_FLAG_OUTLIER)) {
		GMT_Report(API, GMT_MSG_NORMAL, "The CUBE nadir sector --cube-flag-nadir needs the outlier criterion, "
		           "--cube-flag=outlier|both\n");
		ok = false;
	}
	if (cube->prediction_topography && options->prediction != MB_CUBE_PREDICTION_FILE) {
		GMT_Report(API, GMT_MSG_NORMAL, "The option --cube-prediction-topography needs a predicted surface grid, "
		           "--cube-prediction=gridfile\n");
		ok = false;
	}
	if (!ok) {
		GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
		return (MB_ERROR_BAD_PARAMETER);
	}

	/* outlier threshold: the normalized error at which CUBE's monitor calls a
	   single sample an outlier, where the Bayes factor
	   exp(0.5 * (h^2 - 2 h |e|)) with h = est_offset falls below bayes_fac_t */
	if (!cube->threshold_set) {
		const double h = options->param.est_offset;
		cube->threshold = (h * h - 2.0 * log(options->param.bayes_fac_t)) / (2.0 * h);
	}

	/* is the input a datalist or a single swath file */
	if (format == 0)
		mb_get_format(verbose, read_file, NULL, &format, &error);
	const bool read_datalist = format < 0;

	/* grid bounds: the region given or the bounds of the data */
	double gbnd[4];
	if (cube->region_set) {
		for (int k = 0; k < 4; k++)
			gbnd[k] = cube->region[k];
	}
	else {
		struct mb_info_struct mb_info;
		if (read_datalist) {
			int formatread = -1;
			int formatmakeinf = formatread;
			/* mb_make_info_datalist exits on a datalist that cannot be opened */
			if (!mb_gmt_datalist_opens(API, verbose, read_file, formatmakeinf)) {
				GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
				return (MB_ERROR_OPEN_FAIL);
			}
			mb_make_info_datalist(verbose, false, read_file, &formatmakeinf, &error);
			status = mb_get_info_datalist(verbose, read_file, &formatread, &mb_info, lonflip, &error);
		}
		else {
			mb_make_info(verbose, false, read_file, format, &error);
			status = mb_get_info(verbose, read_file, &mb_info, lonflip, &error);
		}
		if (status != MB_SUCCESS || !(mb_info.lon_max > mb_info.lon_min) || !(mb_info.lat_max > mb_info.lat_min)) {
			GMT_Report(API, GMT_MSG_NORMAL, "Unable to get the bounds of the input data from %s; use --cube-region\n", read_file);
			GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
			return (MB_ERROR_BAD_PARAMETER);
		}
		gbnd[0] = mb_info.lon_min;
		gbnd[1] = mb_info.lon_max;
		gbnd[2] = mb_info.lat_min;
		gbnd[3] = mb_info.lat_max;
		status = MB_SUCCESS;
		error = MB_ERROR_NO_ERROR;
	}

	/* geographic grid with the spacing in meters, as in mbgrid -F9 -E dx/dy/meters;
	   dy = dx if not given */
	double mtodeglon;
	double mtodeglat;
	mb_coor_scale(verbose, 0.5 * (gbnd[2] + gbnd[3]), &mtodeglon, &mtodeglat);
	const int nx = (int)lrint((gbnd[1] - gbnd[0]) / (mtodeglon * cube->dx) + 1);
	const double dy_set = cube->dy > 0.0 ? cube->dy : cube->dx;
	const int ny = (int)lrint((gbnd[3] - gbnd[2]) / (mtodeglat * dy_set) + 1);
	if (!(gbnd[1] > gbnd[0]) || !(gbnd[3] > gbnd[2]) || nx < 2 || ny < 2) {
		GMT_Report(API, GMT_MSG_NORMAL, "The CUBE grid of region %f/%f/%f/%f with spacing %f m has fewer than 2 x 2 nodes\n",
		           gbnd[0], gbnd[1], gbnd[2], gbnd[3], cube->dx);
		GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
		return (MB_ERROR_BAD_PARAMETER);
	}
	const double dlon = (gbnd[1] - gbnd[0]) / (nx - 1);
	const double dlat = (gbnd[3] - gbnd[2]) / (ny - 1);
	const double cube_dx = dlon / mtodeglon;
	const double cube_dy = dlat / mtodeglat;
	{
		/* CUBE distances use the longitude scale of the grid center */
		double mtodeglon_south, mtodeglon_north, mtodeglat_tmp;
		mb_coor_scale(verbose, gbnd[2], &mtodeglon_south, &mtodeglat_tmp);
		mb_coor_scale(verbose, gbnd[3], &mtodeglon_north, &mtodeglat_tmp);
		if (fabs(mtodeglon_south - mtodeglon) > 0.01 * mtodeglon || fabs(mtodeglon_north - mtodeglon) > 0.01 * mtodeglon)
			fprintf(stderr, "\nWarning: CUBE distances in this geographic grid are in error by more than 1%%\n"
			                "at its northern or southern edge; use a smaller --cube-region\n");
	}

	/* bounds for reading the data, extended as in mbgrid */
	double bounds[4];
	for (int k = 0; k < 4; k++)
		bounds[k] = gbnd[k];
	{
		const double xx = MIN(0.05 * (bounds[1] - bounds[0]), 0.1);
		const double yy = MIN(0.05 * (bounds[3] - bounds[2]), 0.1);
		bounds[0] -= xx;
		bounds[1] += xx;
		bounds[2] -= yy;
		bounds[3] += yy;
	}

	/* memory */
	double cube_memory = 0.0;
	mb_cube_estimate_memory(verbose, &options->param, nx, ny, &cube_memory, &error);
	if (cube_memory > options->memory_limit * 1.0e9) {
		GMT_Report(API, GMT_MSG_NORMAL, "The CUBE grid of %d x %d nodes may need %.1f GB of memory, more than the limit of "
		           "%.1f GB\n", nx, ny, cube_memory / 1.0e9, options->memory_limit);
		GMT_Report(API, GMT_MSG_NORMAL, "set by --cube-memory-limit; use a larger grid spacing or a smaller area\n");
		GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
		return (MB_ERROR_BAD_PARAMETER);
	}

	if (verbose >= 0) {
		const char *extractor_name[] = {"prior, most soundings", "likelihood, closest to a nearby unambiguous node",
		                                "posterior, likelihood and number of soundings",
		                                "predicted, closest to the predicted surface"};
		fprintf(stderr, "\nCUBE flagging:\n");
		fprintf(stderr, "  Grid bounds:            %f %f %f %f\n", gbnd[0], gbnd[1], gbnd[2], gbnd[3]);
		fprintf(stderr, "  Grid dimensions:        %d x %d\n", nx, ny);
		fprintf(stderr, "  Node spacing:           %f m x %f m\n", cube_dx, cube_dy);
		if (options->model == MB_CUBE_MODEL_IHO)
			fprintf(stderr, "  Sounding uncertainty:   %s; TVU a %.3f m, b %.4f; THU %.1f m + %.0f%% of depth\n",
			        options->iho_label, options->param.tvu_a, options->param.tvu_b, options->thu_fixed,
			        100.0 * options->thu_fraction);
		else if (options->model == MB_CUBE_MODEL_CONSTANT)
			fprintf(stderr, "  Sounding uncertainty:   constant, vertical sigma %f m, horizontal sigma %f m\n",
			        options->sigma_v, options->sigma_h);
		else
			fprintf(stderr, "  Sounding uncertainty:   vertical sigma sqrt[%f^2 + [%f * depth]^2] m,\n"
			                "                          horizontal sigma sqrt[%f^2 + [%f * depth]^2] m\n",
			        options->model_a, options->model_b, options->sigma_h, options->model_c);
		fprintf(stderr, "  Depth used in models:   %s\n",
		        options->depth_below_sonar ? "depth below the sonar" : "depth below the sea surface");
		fprintf(stderr, "  Hypothesis selection:   %s\n", extractor_name[options->param.extractor]);
		if (options->param.extractor == MB_CUBE_EXTRACT_LIKELIHOOD || options->param.extractor == MB_CUBE_EXTRACT_POSTERIOR) {
			int context_min = 0;
			int context_max = 0;
			int context_error = MB_ERROR_NO_ERROR;
			mb_cube_context_nodes(verbose, &options->param, cube_dx, cube_dy, &context_min, &context_max, &context_error);
			fprintf(stderr, "  Guide search:           %.1f to %.1f m, %d to %d nodes\n", options->param.min_context,
			        options->param.max_context, context_min, context_max);
			if (context_max < 1) {
				const double spacing = MIN(cube_dx, cube_dy);
				fprintf(stderr, "  Warning: --cube-context=%g/%g is shorter than the node spacing of %.1f m; no neighbouring\n"
				                "           node is searched and the selection falls back to the prior rule.\n"
				                "           For this grid try --cube-context=%g/%g, 1 to 5 nodes.\n",
				        options->param.min_context, options->param.max_context, spacing, spacing, 5.0 * spacing);
			}
		}
		if (options->prediction == MB_CUBE_PREDICTION_MEDIAN)
			fprintf(stderr, "  Predicted surface:      median of the soundings, %d times the node spacing\n",
			        options->prediction_factor);
		else if (options->prediction == MB_CUBE_PREDICTION_FILE)
			fprintf(stderr, "  Predicted surface:      grid %s%s\n", options->prediction_file,
			        cube->prediction_topography ? ", heights positive up" : "");
		if (cube->flag & MBCLEAN_CUBE_FLAG_HYPOTHESIS)
			fprintf(stderr, "  Flag:                   soundings not belonging to the selected hypothesis%s\n",
			        cube->strength_max < 5.0 ? ", at nodes of strength up to the limit" : "");
		if (cube->flag & MBCLEAN_CUBE_FLAG_OUTLIER)
			fprintf(stderr, "  Flag:                   soundings more than %.3f normalized errors from the selected hypothesis\n",
			        cube->threshold);
		if (cube->nadir_set)
			fprintf(stderr, "  Nadir sector:           beam angle < %.1f deg: soundings more than %.3f normalized errors %s\n",
			        cube->nadir_angle, cube->nadir_threshold,
			        cube->nadir_both ? "from the selected hypothesis" : "shallower than the selected hypothesis");
		if (cube->flag & MBCLEAN_CUBE_FLAG_HYPOTHESIS && cube->strength_max < 5.0)
			fprintf(stderr, "  Strength limit:         %f\n", cube->strength_max);
		fprintf(stderr, "  Memory estimate:        %.2f GB, limit %.2f GB\n", cube_memory / 1.0e9, options->memory_limit);
	}

	/* initialize the CUBE grid */
	status = mb_cube_init(verbose, &options->param, nx, ny, cube_dx, cube_dy, &cube_ptr, &error);
	if (status != MB_SUCCESS) {
		char *mbmessage = NULL;
		mb_error(verbose, error, &mbmessage);
		GMT_Report(API, GMT_MSG_NORMAL, "MBIO Error allocating CUBE grid:\n%s\n", mbmessage);
		GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
		cube_ptr = NULL;
		exit_code = error;
		goto cleanup;
	}

	/* predicted surface from a geographic grid file of depths */
	if (options->prediction != MB_CUBE_PREDICTION_NONE)
		status = mb_mallocd(verbose, __FILE__, __LINE__, (size_t)nx * ny * sizeof(float), (void **)&pred_depth, &error);
	if (status == MB_SUCCESS && options->prediction == MB_CUBE_PREDICTION_FILE) {
		int pgrid_mode;
		char pgrid_id[MB_PATH_MAXLINE] = "";
		float pgrid_nodata;
		int pgrid_nxy, pgrid_nx, pgrid_ny;
		double pgrid_min, pgrid_max, pgrid_xmin, pgrid_xmax, pgrid_ymin, pgrid_ymax, pgrid_dx, pgrid_dy;
		float *pgrid = NULL;
		status = mb_read_gmt_grd(verbose, options->prediction_file, &pgrid_mode, pgrid_id, &pgrid_nodata, &pgrid_nxy,
		                         &pgrid_nx, &pgrid_ny, &pgrid_min, &pgrid_max, &pgrid_xmin, &pgrid_xmax, &pgrid_ymin,
		                         &pgrid_ymax, &pgrid_dx, &pgrid_dy, &pgrid, NULL, NULL, &error);
		if (status != MB_SUCCESS) {
			char *mbmessage = NULL;
			mb_error(verbose, error, &mbmessage);
			GMT_Report(API, GMT_MSG_NORMAL, "MBIO Error reading the CUBE predicted surface %s:\n%s\n",
			           options->prediction_file, mbmessage);
			GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
			exit_code = error;
			goto cleanup;
		}
		const long pgrid_reg_x = lrint((pgrid_xmax - pgrid_xmin) / pgrid_dx) + 1 - pgrid_nx;
		const long pgrid_reg_y = lrint((pgrid_ymax - pgrid_ymin) / pgrid_dy) + 1 - pgrid_ny;
		if (pgrid_mode == MB_PROJECTION_PROJECTED || pgrid_reg_x != pgrid_reg_y || (pgrid_reg_x != 0 && pgrid_reg_x != 1)) {
			GMT_Report(API, GMT_MSG_NORMAL, "The predicted surface %s must be a geographic grid with gridline or pixel "
			           "registration\n", options->prediction_file);
			GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
			mb_freed(verbose, __FILE__, __LINE__, (void **)&pgrid, &error);
			exit_code = MB_ERROR_BAD_PARAMETER;
			goto cleanup;
		}
		mb_cube_sample_grid(verbose, pgrid, pgrid_nx, pgrid_ny, pgrid_xmin + 0.5 * pgrid_reg_x * pgrid_dx,
		                    pgrid_ymin + 0.5 * pgrid_reg_y * pgrid_dy, pgrid_dx, pgrid_dy, pgrid_nodata,
		                    cube->prediction_topography ? -1.0 : 1.0, nx, ny, gbnd[0], gbnd[2], dlon, dlat,
		                    MBCLEAN_CUBE_NODATA, pred_depth, &error);
		mb_freed(verbose, __FILE__, __LINE__, (void **)&pgrid, &error);
		int npred = 0;
		status = mb_cube_apply_prediction(verbose, cube_ptr, options, nx, ny, pred_depth, MBCLEAN_CUBE_NODATA, &npred, &error);
		if (status == MB_SUCCESS) {
			fprintf(stderr, "\nPredicted surface set at %d of %d nodes\n", npred, nx * ny);
			if (npred == 0)
				fprintf(stderr, "Warning: the predicted surface %s covers no node of the CUBE grid\n", options->prediction_file);
		}
	}
	if (status == MB_SUCCESS && options->prediction == MB_CUBE_PREDICTION_MEDIAN)
		status = mb_cube_median_init(verbose, nx, ny, cube_dx, cube_dy, options->prediction_factor,
		                             options->memory_limit * 1.0e9 - cube_memory, &median_ptr, &error);
	if (status != MB_SUCCESS) {
		char *mbmessage = NULL;
		mb_error(verbose, error, &mbmessage);
		GMT_Report(API, GMT_MSG_NORMAL, "MBIO Error allocating the CUBE predicted surface:\n%s\n", mbmessage);
		GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
		exit_code = error;
		goto cleanup;
	}

	/* counters, 64 bit for the totals of large surveys */
	int nfiletot = 0;
	long long ninserttot = 0;
	long long ntestedtot = 0;
	long long nuntestedtot = 0;
	long long nhypothesistot = 0;
	long long noutliertot = 0;
	long long nblundertot = 0;
	long long nflagtot = 0;

	/* read all of the data: pass 0 bins the soundings for a median predicted
	   surface, pass 1 builds the CUBE grid, pass 2 flags the soundings */
	for (int pass = options->prediction == MB_CUBE_PREDICTION_MEDIAN ? 0 : 1; pass < 3; pass++) {
		if (verbose >= 0) {
			if (pass == 0)
				fprintf(stderr, "\nReading data for the median predicted surface...\n");
			else if (pass == 1)
				fprintf(stderr, "\nReading data for the CUBE surface...\n");
			else
				fprintf(stderr, "\nComparing soundings with the CUBE surface...\n");
		}

		if (read_datalist && mb_datalist_open(verbose, &datalist, read_file, MB_DATALIST_LOOK_UNSET, &error) != MB_SUCCESS) {
			GMT_Report(API, GMT_MSG_NORMAL, "Unable to open data list file: %s\n", read_file);
			GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
			datalist = NULL;
			exit_code = MB_ERROR_OPEN_FAIL;
			goto cleanup;
		}
		int pstatus = MB_PROCESSED_NONE;
		int astatus = MB_ALTNAV_NONE;
		char ppath[MB_PATH_MAXLINE] = "";
		char apath[MB_PATH_MAXLINE] = "";
		char dpath[MB_PATH_MAXLINE] = "";
		int fformat = format;
		double file_weight = 1.0;
		bool more;
		if (read_datalist) {
			more = mb_datalist_read3(verbose, datalist, &pstatus, path, ppath, &astatus, apath, dpath, &fformat, &file_weight,
			                         &error) == MB_SUCCESS;
		}
		else {
			snprintf(path, sizeof(path), "%s", read_file);
			more = true;
		}
		while (more) {
			if (fformat > 0 && path[0] != '#') {
				/* the file read: processed where the datalist asks for it */
				char file[MB_PATH_MAXLINE];
				snprintf(file, sizeof(file), "%s", pstatus == MB_PROCESSED_USE ? ppath : path);

				/* warn if a processed file is older than the files it is made from */
				if (pass == 1 && pstatus == MB_PROCESSED_USE) {
					struct stat file_status;
					time_t ptime = 0;
					if (stat(ppath, &file_status) == 0)
						ptime = file_status.st_mtime;
					char sfile[MB_PATH_MAXLINE + 8];
					bool stale = stat(path, &file_status) == 0 && file_status.st_mtime > ptime;
					snprintf(sfile, sizeof(sfile), "%s.par", path);
					stale = stale || (stat(sfile, &file_status) == 0 && file_status.st_mtime > ptime);
					snprintf(sfile, sizeof(sfile), "%s.esf", path);
					stale = stale || (stat(sfile, &file_status) == 0 && file_status.st_mtime > ptime);
					if (stale)
						fprintf(stderr, "\nWarning: the processed file %s is older than its raw file, parameter file or edit\n"
						                "save file; run mbprocess so that the CUBE surface matches mbgrid\n",
						        ppath);
				}

				/* skip files outside the grid */
				bool file_in_bounds = true;
				if (mb_check_info(verbose, file, lonflip, bounds, &file_in_bounds, &error) != MB_SUCCESS) {
					file_in_bounds = true;
					error = MB_ERROR_NO_ERROR;
				}

				/* lock the raw file while its edits are written */
				bool oktoprocess = file_in_bounds;
				if (oktoprocess && pass == 2 && uselockfiles) {
					if (mb_pr_lockswathfile(verbose, path, MBP_LOCK_EDITBATHY, program_name, &error) != MB_SUCCESS) {
						GMT_Report(API, GMT_MSG_NORMAL, "Unable to lock file %s; its soundings are not flagged\n", path);
						oktoprocess = false;
						error = MB_ERROR_NO_ERROR;
					}
					else
						file_locked = true;
				}

				/* current edits of the raw file, read only or appended to */
				char esffile[MB_PATH_MAXLINE] = "";
				if (oktoprocess) {
					if (mb_esf_load(verbose, program_name, path, true, pass == 2 ? MBP_ESF_APPEND : MBP_ESF_NOWRITE, esffile,
					                &esf, &error) == MB_SUCCESS) {
						esf_loaded = true;
					}
					else if (pass == 2) {
						GMT_Report(API, GMT_MSG_NORMAL, "Unable to open the edit save file of %s; its soundings are not "
						           "flagged\n", path);
						if (file_locked) {
							mb_pr_unlockswathfile(verbose, path, MBP_LOCK_EDITBATHY, program_name, &error);
							file_locked = false;
						}
						oktoprocess = false;
					}
					error = MB_ERROR_NO_ERROR;
				}

				/* read the file */
				int nflagfile = 0;
				if (oktoprocess) {
					char rfile[MB_PATH_MAXLINE];
					snprintf(rfile, sizeof(rfile), "%s", file);
					int rformat = fformat;
					mb_get_fbt(verbose, rfile, &rformat, &error);
					double btime_d;
					double etime_d;
					int beams_bath;
					int beams_amp;
					int pixels_ss;
					if (mb_read_init_altnav(verbose, rfile, rformat, pings, lonflip, bounds, btime_i, etime_i, speedmin, timegap,
					                        astatus, apath, &mbio_ptr, &btime_d, &etime_d, &beams_bath, &beams_amp, &pixels_ss,
					                        &error) != MB_SUCCESS) {
						char *mbmessage = NULL;
						mb_error(verbose, error, &mbmessage);
						GMT_Report(API, GMT_MSG_NORMAL, "MBIO Error returned from function <mb_read_init_altnav>:\n%s\n", mbmessage);
						GMT_Report(API, GMT_MSG_NORMAL, "Multibeam File <%s> not initialized for reading\n", rfile);
						GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
						mbio_ptr = NULL;	/* not a handle to close */
						exit_code = error;
						goto cleanup;
					}
					char *beamflag = NULL;
					double *bath = NULL;
					double *amp = NULL;
					double *bathlon = NULL;
					double *bathlat = NULL;
					double *ss = NULL;
					double *sslon = NULL;
					double *sslat = NULL;
					if (error == MB_ERROR_NO_ERROR)
						mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(char), (void **)&beamflag, &error);
					if (error == MB_ERROR_NO_ERROR)
						mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bath, &error);
					if (error == MB_ERROR_NO_ERROR)
						mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_AMPLITUDE, sizeof(double), (void **)&amp, &error);
					if (error == MB_ERROR_NO_ERROR)
						mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathlon, &error);
					if (error == MB_ERROR_NO_ERROR)
						mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathlat, &error);
					if (error == MB_ERROR_NO_ERROR)
						mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ss, &error);
					if (error == MB_ERROR_NO_ERROR)
						mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&sslon, &error);
					if (error == MB_ERROR_NO_ERROR)
						mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&sslat, &error);
					if (error != MB_ERROR_NO_ERROR) {
						char *mbmessage = NULL;
						mb_error(verbose, error, &mbmessage);
						GMT_Report(API, GMT_MSG_NORMAL, "MBIO Error allocating data arrays:\n%s\n", mbmessage);
						GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
						exit_code = error;
						goto cleanup;
					}

					if (pass == 2 && verbose >= 0)
						fprintf(stderr, "\nProcessing %s\n", file);
					int ndata = 0;
					int ntested = 0;
					int nuntested = 0;
					int nhypothesis = 0;
					int noutlier = 0;
					int nblunder = 0;
					int nflag = 0;
					int multiplicity = 0;
					double time_d_last = 0.0;
					char comment[MB_COMMENT_MAXLINE];
					while (error <= MB_ERROR_NO_ERROR) {
						int kind;
						int rpings;
						int time_i[7];
						double time_d;
						double navlon, navlat, speed, heading, distance, altitude, sensordepth;
						status = mb_read(verbose, mbio_ptr, &kind, &rpings, time_i, &time_d, &navlon, &navlat, &speed, &heading,
						                 &distance, &altitude, &sensordepth, &beams_bath, &beams_amp, &pixels_ss, beamflag, bath,
						                 amp, bathlon, bathlat, ss, sslon, sslat, comment, &error);

						/* time gaps are not a problem here */
						if (error == MB_ERROR_TIME_GAP) {
							error = MB_ERROR_NO_ERROR;
							status = MB_SUCCESS;
						}
						if (error != MB_ERROR_NO_ERROR || kind != MB_DATA_DATA)
							continue;

						/* ping multiplicity, as in the edit save files of mbclean */
						void *store_ptr = NULL;
						int sensorhead = 0;
						int sensorhead_error = MB_ERROR_NO_ERROR;
						mb_get_store(verbose, mbio_ptr, &store_ptr, &sensorhead_error);
						if (mb_sensorhead(verbose, mbio_ptr, store_ptr, &sensorhead, &sensorhead_error) == MB_SUCCESS)
							multiplicity = sensorhead;
						else if (ndata > 0 && fabs(time_d - time_d_last) < MB_ESF_MAXTIMEDIFF)
							multiplicity++;
						else
							multiplicity = 0;
						time_d_last = time_d;
						ndata++;

						/* current edits */
						if (esf_loaded)
							mb_esf_apply(verbose, &esf, time_d, multiplicity, beams_bath, beamflag, &error);

						/* meters to degrees at the sonar, for the beam angles of the nadir sector */
						double ping_mtodeglon = 0.0;
						double ping_mtodeglat = 0.0;
						if (pass == 2 && cube->nadir_set)
							mb_coor_scale(verbose, navlat, &ping_mtodeglon, &ping_mtodeglat);

						for (int ib = 0; ib < beams_bath; ib++) {
							if (!mb_beam_ok(beamflag[ib]))
								continue;
							const double xx = (bathlon[ib] - gbnd[0]) / mtodeglon;
							const double yy = (bathlat[ib] - gbnd[2]) / mtodeglat;
							if (pass == 0) {
								double bins_memory = 0.0;
								if (mb_cube_median_add(verbose, median_ptr, xx, yy, bath[ib], &bins_memory, &error) != MB_SUCCESS) {
									GMT_Report(API, GMT_MSG_NORMAL, "The CUBE grid and the median predicted surface would need "
									           "%.1f GB of memory, more than the\nlimit of %.1f GB set by --cube-memory-limit, "
									           "or the memory could not be allocated\n",
									           (cube_memory + bins_memory) / 1.0e9, options->memory_limit);
									GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
									exit_code = MB_ERROR_MEMORY_FAIL;
									goto cleanup;
								}
								continue;
							}
							const double h = fabs(options->depth_below_sonar ? bath[ib] - sensordepth : bath[ib]);
							double var_depth;
							double var_horiz;
							mb_cube_sounding_variance(verbose, options, h, &var_depth, &var_horiz, &error);
							if (pass == 1) {
								int nused = 0;
								if (mb_cube_insert(verbose, cube_ptr, xx, yy, bath[ib], h, var_depth, var_horiz, &nused, &error) !=
								    MB_SUCCESS) {
									if (error == MB_ERROR_MEMORY_FAIL) {
										char *mbmessage = NULL;
										mb_error(verbose, error, &mbmessage);
										GMT_Report(API, GMT_MSG_NORMAL, "MBIO Error inserting data into the CUBE grid:\n%s\n",
										           mbmessage);
										GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
										exit_code = error;
										goto cleanup;
									}
									/* soundings with invalid values are skipped */
								}
								else if (nused > 0) {
									ninserttot++;
								}
								error = MB_ERROR_NO_ERROR;
								continue;
							}

							/* pass 2: compare the sounding with the CUBE surface */
							int node, nhyp, is_selected, is_blunder;
							double error_selected, strength;
							if (mb_cube_classify(verbose, cube_ptr, xx, yy, bath[ib], h, var_depth, var_horiz, &node, &nhyp,
							                     &is_selected, &is_blunder, &error_selected, &strength, &error) != MB_SUCCESS ||
							    node < 0) {
								nuntested++;
								error = MB_ERROR_NO_ERROR;
								continue;
							}
							ntested++;

							/* beam angle from vertical, from the horizontal distance of the
							   sounding to the sonar and its depth below the sonar */
							double beam_angle = 90.0;
							if (cube->nadir_set) {
								const double dx = (bathlon[ib] - navlon) / ping_mtodeglon;
								const double dy = (bathlat[ib] - navlat) / ping_mtodeglat;
								const double dz = bath[ib] - sensordepth;
								if (dz > 0.0)
									beam_angle = RTD * atan2(sqrt(dx * dx + dy * dy), dz);
							}

							const char *reason = NULL;
							if (is_blunder) {
								reason = "blunder";
								nblunder++;
							}
							else if ((cube->flag & MBCLEAN_CUBE_FLAG_HYPOTHESIS) && !is_selected && strength <= cube->strength_max) {
								reason = "hypothesis";
								nhypothesis++;
							}
							else if (cube->flag & MBCLEAN_CUBE_FLAG_OUTLIER) {
								const bool nadir = cube->nadir_set && beam_angle < cube->nadir_angle;
								bool outlier;
								if (nadir && cube->nadir_both)
									outlier = fabs(error_selected) > cube->nadir_threshold;
								else if (nadir)
									outlier = error_selected < -cube->nadir_threshold;
								else
									outlier = fabs(error_selected) > cube->threshold;
								if (outlier) {
									reason = "outlier";
									noutlier++;
								}
							}
							if (reason != NULL) {
								if (verbose >= 1 && cube->nadir_set)
									fprintf(stderr, "c: %4d %2d %2d %2.2d:%2.2d:%2.2d.%6.6d  %4d %8.2f %s %d %.3f %.2f %.1f\n",
									        time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6], ib, bath[ib],
									        reason, nhyp, error_selected, strength, beam_angle);
								else if (verbose >= 1)
									fprintf(stderr, "c: %4d %2d %2d %2.2d:%2.2d:%2.2d.%6.6d  %4d %8.2f %s %d %.3f %.2f\n",
									        time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6], ib, bath[ib],
									        reason, nhyp, error_selected, strength);
								beamflag[ib] = mb_beam_set_flag_filter(beamflag[ib]);
								if (mb_esf_save(verbose, &esf, time_d, ib + multiplicity * MB_ESF_MULTIPLICITY_FACTOR, MBP_EDIT_FILTER,
								                &error) != MB_SUCCESS) {
									GMT_Report(API, GMT_MSG_NORMAL, "Unable to write the edit save file %s\n", esffile);
									GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
									exit_code = MB_ERROR_WRITE_FAIL;
									goto cleanup;
								}
								nflag++;
							}
						}
					}
					mb_close(verbose, &mbio_ptr, &error);
					mbio_ptr = NULL;
					error = MB_ERROR_NO_ERROR;
					status = MB_SUCCESS;

					if (pass == 2) {
						nfiletot++;
						ntestedtot += ntested;
						nuntestedtot += nuntested;
						nhypothesistot += nhypothesis;
						noutliertot += noutlier;
						nblundertot += nblunder;
						nflagtot += nflag;
						nflagfile = nflag;
						if (verbose >= 0) {
							fprintf(stderr, "%d bathymetry data records processed\n", ndata);
							fprintf(stderr, "%d soundings compared with the CUBE surface\n", ntested);
							fprintf(stderr, "%d soundings reaching no CUBE node with data, not tested\n", nuntested);
							fprintf(stderr, "%d soundings belonging to a hypothesis not selected\n", nhypothesis);
							fprintf(stderr, "%d soundings exceeding the outlier threshold\n", noutlier);
							if (options->param.blunder && options->prediction != MB_CUBE_PREDICTION_NONE)
								fprintf(stderr, "%d soundings rejected by the blunder test\n", nblunder);
							fprintf(stderr, "%d beams flagged\n", nflag);
						}
					}
				}

				/* close the edit save file once its edits are on disk, register it if
				   edits were added, since an unchanged parameter file keeps processed files
				   up to date, and unlock the raw file */
				if (esf_loaded && pass == 2 && esf.esffp != NULL && (fflush(esf.esffp) != 0 || ferror(esf.esffp) != 0)) {
					GMT_Report(API, GMT_MSG_NORMAL, "Unable to write the edit save file %s\n", esffile);
					GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
					exit_code = MB_ERROR_WRITE_FAIL;
					goto cleanup;
				}
				if (esf_loaded) {
					mb_esf_close(verbose, &esf, &error);
					esf_loaded = false;
				}
				if (oktoprocess && pass == 2 && nflagfile > 0) {
					mb_pr_update_format(verbose, path, true, fformat, &error);
					mb_pr_update_edit(verbose, path, MBP_EDIT_ON, esffile, &error);
				}
				if (file_locked) {
					mb_pr_unlockswathfile(verbose, path, MBP_LOCK_EDITBATHY, program_name, &error);
					file_locked = false;
				}
				error = MB_ERROR_NO_ERROR;
			}

			/* next file */
			if (read_datalist)
				more = mb_datalist_read3(verbose, datalist, &pstatus, path, ppath, &astatus, apath, dpath, &fformat,
				                         &file_weight, &error) == MB_SUCCESS;
			else
				more = false;
		}
		if (read_datalist) {
			mb_datalist_close(verbose, &datalist, &error);
			datalist = NULL;
		}
		error = MB_ERROR_NO_ERROR;

		/* median predicted surface, then the estimators and the selection */
		if (pass == 0) {
			int nused = 0;
			status = mb_cube_median_surface(verbose, median_ptr, MBCLEAN_CUBE_NODATA, pred_depth, &nused, &error);
			if (status == MB_SUCCESS)
				fprintf(stderr, "%d data points used for the median predicted surface\n", nused);
			mb_cube_median_deall(verbose, &median_ptr, &error);
			median_ptr = NULL;
		}
		if (status == MB_SUCCESS && pass == 0 && options->prediction == MB_CUBE_PREDICTION_MEDIAN) {
			int npred = 0;
			status = mb_cube_apply_prediction(verbose, cube_ptr, options, nx, ny, pred_depth, MBCLEAN_CUBE_NODATA, &npred, &error);
			if (status == MB_SUCCESS)
				fprintf(stderr, "Predicted surface set at %d of %d nodes\n", npred, nx * ny);
		}
		if (status == MB_SUCCESS && pass == 1) {
			fprintf(stderr, "%lld soundings inserted into the CUBE grid\n", ninserttot);

			/* a predicted surface grid of the wrong sign makes most soundings
			   blunders and reverses the slope correction */
			double nsame = 0.0;
			double nopposite = 0.0;
			if (options->prediction == MB_CUBE_PREDICTION_FILE &&
			    mb_cube_prediction_sign(verbose, cube_ptr, &nsame, &nopposite, &error) != MB_SUCCESS) {
				GMT_Report(API, GMT_MSG_NORMAL, "The predicted surface %s has the opposite sign to %.0f of the %.0f soundings "
				           "compared with it:\na grid of heights, positive up, needs --cube-prediction-topography, a grid of "
				           "depths, positive\ndown, must be used without it\n",
				           options->prediction_file, nopposite, nsame + nopposite);
				GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
				exit_code = MB_ERROR_BAD_PARAMETER;
				goto cleanup;
			}
			status = mb_cube_flush(verbose, cube_ptr, &error);
		}
		if (status != MB_SUCCESS) {
			char *mbmessage = NULL;
			mb_error(verbose, error, &mbmessage);
			GMT_Report(API, GMT_MSG_NORMAL, "MBIO Error building the CUBE surface:\n%s\n", mbmessage);
			GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", program_name);
			exit_code = error;
			goto cleanup;
		}
	}

	/* give the total statistics */
	if (verbose >= 0) {
		fprintf(stderr, "\nMBclean CUBE Processing Totals:\n");
		fprintf(stderr, "-------------------------------\n");
		fprintf(stderr, "%d total swath data files processed\n", nfiletot);
		fprintf(stderr, "%lld total soundings inserted into the CUBE grid\n", ninserttot);
		fprintf(stderr, "%lld total soundings compared with the CUBE surface\n", ntestedtot);
		fprintf(stderr, "%lld total soundings reaching no CUBE node with data, not tested\n", nuntestedtot);
		fprintf(stderr, "%lld total soundings belonging to a hypothesis not selected\n", nhypothesistot);
		fprintf(stderr, "%lld total soundings exceeding the outlier threshold\n", noutliertot);
		if (options->param.blunder && options->prediction != MB_CUBE_PREDICTION_NONE)
			fprintf(stderr, "%lld total soundings rejected by the blunder test\n", nblundertot);
		fprintf(stderr, "%lld total beams flagged\n", nflagtot);
	}

cleanup:
	/* on a fatal error: close the swath file and the edit save file, unlock the
	   raw file and close the datalist; always release the CUBE grid and the
	   predicted surface */
	{
		int release_error = MB_ERROR_NO_ERROR;
		if (mbio_ptr != NULL)
			mb_close(verbose, &mbio_ptr, &release_error);
		if (esf_loaded)
			mb_esf_close(verbose, &esf, &release_error);
		if (file_locked)
			mb_pr_unlockswathfile(verbose, path, MBP_LOCK_EDITBATHY, program_name, &release_error);
		if (datalist != NULL)
			mb_datalist_close(verbose, &datalist, &release_error);
		if (median_ptr != NULL)
			mb_cube_median_deall(verbose, &median_ptr, &release_error);
		if (pred_depth != NULL)
			mb_freed(verbose, __FILE__, __LINE__, (void **)&pred_depth, &release_error);
		if (cube_ptr != NULL)
			mb_cube_deall(verbose, &cube_ptr, &release_error);
	}
	return (exit_code);
}

#define bailout(code)  { gmt_M_free_options(mode); return (code); }
#define Return(code)   { Free_mbclean_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }

/* ====================================================================
 * GMT_mbclean  — GMT module entry point.
 * ==================================================================== */

int GMT_mbclean(void *V_API, int mode, void *args) {
	int    error   = MB_ERROR_NO_ERROR;

	struct MBCLEAN_CTRL *Ctrl = NULL;
	struct GMT_CTRL     *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION   *options = NULL;
	struct GMTAPI_CTRL  *API = gmt_get_api_ptr(V_API);

	/* ------- standard GMT module preamble -------------------------- */
	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program (on datalist.mb-1) */
	if ((error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(error);

	/* the program's CUBE long options, which have no short letter, kept out of the keyword translation and of
	   GMT's --PAR=value handling */
	mb_gmt_mark_long_options(API, &options, long_options);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	Ctrl = New_mbclean_Ctrl(GMT);
	if ((error = parse(GMT, Ctrl, options)) != 0) Return (error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	/* ------- pull parsed control values into locals ---------------- */
	bool   check_deviation       = Ctrl->transf.check_deviation;
	bool   check_range           = Ctrl->transf.check_range;
	bool   check_slope           = Ctrl->transf.check_slope;
	bool   zap_long_across       = Ctrl->transf.zap_long_across;
	bool   check_fraction        = Ctrl->transf.check_fraction;
	bool   check_range_min       = Ctrl->transf.check_range_min;
	bool   check_speed_good      = Ctrl->transf.check_speed_good;
	bool   zap_rails             = Ctrl->transf.zap_rails;
	bool   zap_max_heading_rate  = Ctrl->transf.zap_max_heading_rate;
	bool   check_spike           = Ctrl->transf.check_spike;
	bool   fix_edit_timestamps   = Ctrl->transf.fix_edit_timestamps;
	bool   check_position_bounds = Ctrl->transf.check_position_bounds;
	bool   zap_beams             = Ctrl->transf.zap_beams;
	bool   check_zero_position   = Ctrl->transf.check_zero_position;
	bool   check_num_good_min    = Ctrl->transf.check_num_good_min;
	bool   check_ping_deviation  = Ctrl->transf.check_ping_deviation;

	bool   flag_distance         = Ctrl->Y.flag_distance;
	bool   unflag_distance       = Ctrl->Y.unflag_distance;
	bool   flag_angle            = Ctrl->Y.flag_angle;
	bool   unflag_angle          = Ctrl->Y.unflag_angle;

	int    verbose = Ctrl->verbose;
	int    status;

	/* ------- MBIO defaults + reset ---------------------------------- */
	int    format, pings, lonflip;
	double bounds[4];
	int    btime_i[7], etime_i[7];
	double speedmin, timegap;
	int    uselockfiles_i;
	bool   uselockfiles;

	status = mb_defaults(verbose, &format, &pings, &lonflip, bounds, btime_i, etime_i, &speedmin, &timegap);
	mb_uselockfiles(verbose, &uselockfiles);
	uselockfiles_i = uselockfiles ? 1 : 0;
	(void)uselockfiles_i;
	if (Ctrl->L.active) lonflip = Ctrl->L.lonflip;
	if (Ctrl->F.active) format  = Ctrl->F.format;

	pings = 1;
	bounds[0] = -360.;  bounds[1] = 360.;  bounds[2] = -90.;  bounds[3] = 90.;
	btime_i[0]=1962; btime_i[1]=2; btime_i[2]=21; btime_i[3]=10; btime_i[4]=30; btime_i[5]=0; btime_i[6]=0;
	etime_i[0]=2062; etime_i[1]=2; etime_i[2]=21; etime_i[3]=10; etime_i[4]=30; etime_i[5]=0; etime_i[6]=0;
	speedmin = 0.0;
	timegap  = 1000000000.0;

	/* ------- copy Ctrl values into named locals (algorithm style) -- */
	double deviation_max         = Ctrl->A.deviation_max;
	double depth_low             = Ctrl->B.depth_low;
	double depth_high            = Ctrl->B.depth_high;
	double slopemax              = Ctrl->C.slopemax;
	double distancemin           = Ctrl->D.distancemin;
	double distancemax           = Ctrl->D.distancemax;
	double max_acrosstrack       = Ctrl->E.max_acrosstrack;
	double fraction_low          = Ctrl->G.fraction_low;
	double fraction_high         = Ctrl->G.fraction_high;
	double range_min             = Ctrl->K.range_min;
	int    clean_mode            = Ctrl->M.mode;
	double ping_deviation_tol    = Ctrl->N.ping_deviation_tolerance;
	double speed_low             = Ctrl->P.speed_low;
	double speed_high            = Ctrl->P.speed_high;
	double backup_dist           = Ctrl->Q.backup_dist;
	double max_heading_rate      = Ctrl->R.max_heading_rate;
	double spikemax              = Ctrl->S.spikemax;
	int    spike_mode            = Ctrl->S.spike_mode;
	double tolerance             = Ctrl->T.tolerance;
	int    num_good_min          = Ctrl->U.num_good_min;
	double west                  = Ctrl->W.west;
	double east                  = Ctrl->W.east;
	double south                 = Ctrl->W.south;
	double north                 = Ctrl->W.north;
	int    zap_beams_left        = Ctrl->X.zap_beams_left;
	int    zap_beams_right       = Ctrl->X.zap_beams_right;
	double flag_distance_left    = Ctrl->Y.flag_distance_left;
	double flag_distance_right   = Ctrl->Y.flag_distance_right;
	double unflag_distance_left  = Ctrl->Y.unflag_distance_left;
	double unflag_distance_right = Ctrl->Y.unflag_distance_right;
	double flag_angle_left       = Ctrl->Y.flag_angle_left;
	double flag_angle_right      = Ctrl->Y.flag_angle_right;
	double unflag_angle_left     = Ctrl->Y.unflag_angle_left;
	double unflag_angle_right    = Ctrl->Y.unflag_angle_right;

	mb_path read_file;
	strcpy(read_file, Ctrl->I.active ? Ctrl->I.inputfile : "datalist.mb-1");

	/* CUBE flagging runs alone: run mbclean twice to combine it with other tests */
	if (Ctrl->cube.use) {
		if (check_slope || zap_beams || flag_distance || unflag_distance || flag_angle || unflag_angle || zap_rails ||
		    check_spike || check_range || check_range_min || check_fraction || check_speed_good || check_deviation ||
		    check_num_good_min || check_position_bounds || check_zero_position || fix_edit_timestamps ||
		    zap_max_heading_rate || zap_long_across || check_ping_deviation) {
			GMT_Report(API, GMT_MSG_NORMAL, "CUBE flagging --cube-spacing cannot be combined with the other tests of %s;\n"
			           "run %s once with the other tests and once with CUBE flagging\n", THIS_MODULE_NAME, THIS_MODULE_NAME);
			Return(GMT_PARSE_ERROR);
		}
		const int cube_exit = mbclean_cube(API, verbose, read_file, format, lonflip, pings, btime_i, etime_i, speedmin,
		                                   timegap, uselockfiles, &Ctrl->cube);
		if (mb_memory_list(verbose, &error) == MB_FAILURE)
			GMT_Report(API, GMT_MSG_NORMAL, "Program %s completed but leaked memory\n", THIS_MODULE_NAME);
		Return(cube_exit == MB_ERROR_NO_ERROR ? GMT_NOERROR : GMT_RUNTIME_ERROR);
	}

	/* If nothing else asked, default to slope checking. */
	if (!check_slope && !zap_beams && !flag_distance && !unflag_distance &&
		!zap_rails && !check_spike && !check_range && !check_fraction &&
		!check_speed_good && !check_deviation && !check_num_good_min &&
		!check_position_bounds && !check_zero_position && !fix_edit_timestamps &&
		!zap_max_heading_rate)
		check_slope = true;

	GMT_Report(API, GMT_MSG_VERBOSE,
			   "Processing input files (MB-System Version %s)\n", MB_VERSION);

	if (format == 0) mb_get_format(verbose, read_file, NULL, &format, &error);

	/* ------- open datalist (or single file) ------------------------ */
	bool   read_datalist = (format < 0);
	bool   read_data;
	void  *datalist = NULL;
	char   swathfile[MB_PATH_MAXLINE];
	char   dfile[MB_PATH_MAXLINE];
	double file_weight;
	int    look_processed = MB_DATALIST_LOOK_UNSET;

	if (read_datalist) {
		if (mb_datalist_open(verbose, &datalist, read_file, look_processed, &error) != MB_SUCCESS) {
			GMT_Report(API, GMT_MSG_NORMAL, "\nUnable to open data list file: %s\n", read_file);
			GMT_Report(API, GMT_MSG_NORMAL, "Program <%s> Terminated\n", THIS_MODULE_NAME);
			Return(GMT_ERROR_ON_FOPEN);
		}
		read_data = (mb_datalist_read(verbose, datalist, swathfile, dfile, &format, &file_weight, &error) == MB_SUCCESS);
	} else {
		strcpy(swathfile, read_file);
		read_data = true;
	}

	/* ------- shared state across files ----------------------------- */
	double btime_d, etime_d;
	bool   locked = false;
	int    lock_purpose = MBP_LOCK_NONE;
	mb_path lock_program, lock_cpu, lock_user;
	char    lock_date[25];

	char    swathfileread[MB_PATH_MAXLINE];
	int     formatread;
	bool    variable_beams, traveltime;
	double  distance, altitude, sensordepth;
	int     beams_bath, beams_amp, pixels_ss;

	void   *mbio_ptr = NULL;
	void   *store_ptr = NULL;
	int     kind;
	struct mbclean_ping_struct ping[3];
	int     pingsread;
	char    comment[MB_COMMENT_MAXLINE];

	/* totals (across files) */
	int nfiletot=0, ndatatot=0, ndepthrangetot=0, nminrangetot=0, nfractiontot=0;
	int ndeviationtot=0, nouterbeamstot=0, nouterdistancetot=0, ninnerdistancetot=0;
	int nouterangletot=0, ninnerangletot=0, nrailtot=0, nlong_acrosstot=0;
	int nmintot=0, nbadtot=0, nspiketot=0, npingdeviationtot=0;
	int nflagtot=0, nunflagtot=0, nflagesftot=0, nunflagesftot=0, nzeroesftot=0;
	int nmax_heading_ratetot=0;

	char  esffile[MB_PATH_MAXLINE];
	struct mb_esf_struct esf;
	int   sensorhead = 0, sensorhead_error = MB_ERROR_NO_ERROR;
	bool  beam_flagging;
	double mtodeglon, mtodeglat;
	int    nlist;
	double median = 0.0;

	/* ================================================================
	 * Per-file loop
	 * ============================================================== */
	while (read_data) {
		bool oktoprocess = true;

		if ((status = mb_format_flags(verbose, &format, &variable_beams, &traveltime,
									  &beam_flagging, &error)) != MB_SUCCESS) {
			char *message = NULL;
			mb_error(verbose, error, &message);
			GMT_Report(API, GMT_MSG_NORMAL, "MBIO error from mb_format_flags (format %d): %s\n", format, message);
			GMT_Report(API, GMT_MSG_NORMAL, "File <%s> skipped by program <%s>\n", swathfile, THIS_MODULE_NAME);
			oktoprocess = false;
			status = MB_SUCCESS; error = MB_ERROR_NO_ERROR;
		}

		if (!beam_flagging)
			GMT_Report(API, GMT_MSG_NORMAL, "Warning: MBIO format %d does not allow flagging; mbprocess will null soundings.\n", format);

		/* lock handling */
		if (uselockfiles)
			status = mb_pr_lockswathfile(verbose, swathfile, MBP_LOCK_EDITBATHY, THIS_MODULE_NAME, &error);
		else {
			mb_pr_lockinfo(verbose, swathfile, &locked, &lock_purpose,
						   lock_program, lock_user, lock_cpu, lock_date, &error);
			if (error == MB_ERROR_FILE_LOCKED) {
				GMT_Report(API, GMT_MSG_NORMAL, "File %s locked but lock ignored\n", swathfile);
				error = MB_ERROR_NO_ERROR;
			}
		}
		if (status == MB_FAILURE) {
			if (error == MB_ERROR_FILE_LOCKED) {
				mb_pr_lockinfo(verbose, swathfile, &locked, &lock_purpose,
							   lock_program, lock_user, lock_cpu, lock_date, &error);
				GMT_Report(API, GMT_MSG_NORMAL,
						   "Unable to open %s — locked by <%s> running <%s> on <%s> at <%s>\n",
						   swathfile, lock_user, lock_program, lock_cpu, lock_date);
			} else if (error == MB_ERROR_OPEN_FAIL) {
				GMT_Report(API, GMT_MSG_NORMAL, "Unable to create lock for %s (permissions?)\n", swathfile);
			}
			oktoprocess = false;
			status = MB_SUCCESS; error = MB_ERROR_NO_ERROR;
		}

		if (oktoprocess) {
			/* fast bathymetry / fbt redirection */
			strcpy(swathfileread, swathfile);
			formatread = format;
			mb_get_fbt(verbose, swathfileread, &formatread, &error);

			if (mb_read_init(verbose, swathfileread, formatread, pings, lonflip, bounds,
							 btime_i, etime_i, speedmin, timegap, &mbio_ptr,
							 &btime_d, &etime_d, &beams_bath, &beams_amp, &pixels_ss,
							 &error) != MB_SUCCESS) {
				char *message = NULL;
				mb_error(verbose, error, &message);
				GMT_Report(API, GMT_MSG_NORMAL, "mb_read_init failed: %s\n", message);
				GMT_Report(API, GMT_MSG_NORMAL, "Multibeam File <%s> not initialized\n", swathfile);
				Return(GMT_RUNTIME_ERROR);
			}

			/* per-file counters */
			int ndata=0, ndepthrange=0, nminrange=0, nfraction=0, nspeed=0;
			int nzeropos=0, nrangepos=0, ndeviation=0, nouterbeams=0;
			int nouterdistance=0, ninnerdistance=0, nouterangle=0, ninnerangle=0;
			int nrail=0, nlong_across=0, nmin=0, nbad=0, nspike=0;
			int nmax_heading_rate=0, npingdeviation=0;
			int nflag=0, nunflag=0, nflagesf=0, nunflagesf=0, nzeroesf=0;

			GMT_Report(API, GMT_MSG_VERBOSE, "\nProcessing %s\n", swathfileread);

			/* allocate per-ping arrays via mb_register_array */
			int i;
			for (i = 0; i < 3; i++) {
				ping[i].beamflag = NULL; ping[i].beamflagorg = NULL;
				ping[i].bath = NULL; ping[i].bathacrosstrack = NULL;
				ping[i].bathalongtrack = NULL; ping[i].bathx = NULL; ping[i].bathy = NULL;
				if (error == MB_ERROR_NO_ERROR)
					status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY,
											   sizeof(char), (void **)&ping[i].beamflag, &error);
				if (error == MB_ERROR_NO_ERROR)
					status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY,
											   sizeof(char), (void **)&ping[i].beamflagorg, &error);
				if (error == MB_ERROR_NO_ERROR)
					status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY,
											   sizeof(double), (void **)&ping[i].bath, &error);
				if (error == MB_ERROR_NO_ERROR)
					status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY,
											   sizeof(double), (void **)&ping[i].bathacrosstrack, &error);
				if (error == MB_ERROR_NO_ERROR)
					status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY,
											   sizeof(double), (void **)&ping[i].bathalongtrack, &error);
				if (error == MB_ERROR_NO_ERROR)
					status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY,
											   sizeof(double), (void **)&ping[i].bathx, &error);
				if (error == MB_ERROR_NO_ERROR)
					status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY,
											   sizeof(double), (void **)&ping[i].bathy, &error);
			}
			double *amp = NULL, *ss = NULL, *ssacrosstrack = NULL, *ssalongtrack = NULL;
			double *list = NULL;
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_AMPLITUDE,
										   sizeof(double), (void **)&amp, &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN,
										   sizeof(double), (void **)&ss, &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN,
										   sizeof(double), (void **)&ssacrosstrack, &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN,
										   sizeof(double), (void **)&ssalongtrack, &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY,
										   4 * sizeof(double), (void **)&list, &error);

			if (error != MB_ERROR_NO_ERROR) {
				char *message = NULL;
				mb_error(verbose, error, &message);
				GMT_Report(API, GMT_MSG_NORMAL, "MBIO error allocating data arrays: %s\n", message);
				Return(GMT_MEMORY_ERROR);
			}

			/* edit save file */
			bool esffile_open = false;
			if (status == MB_SUCCESS) {
				GMT_Report(API, GMT_MSG_VERBOSE, "Sorting old edits...\n");
				status = mb_esf_load(verbose, THIS_MODULE_NAME, swathfile, true, true,
									 esffile, &esf, &error);
				if (status == MB_SUCCESS && esf.esffp != NULL) esffile_open = true;
				if (status == MB_FAILURE && error == MB_ERROR_OPEN_FAIL) {
					esffile_open = false;
					GMT_Report(API, GMT_MSG_NORMAL, "Unable to open new ESF %s\n", esf.esffile);
				} else if (status == MB_FAILURE && error == MB_ERROR_MEMORY_FAIL) {
					esffile_open = false;
					GMT_Report(API, GMT_MSG_NORMAL, "Unable to allocate memory for ESF\n");
				}
				GMT_Report(API, GMT_MSG_VERBOSE, "%d old edits sorted...\n", esf.nedit);
			}

			int nrec = 0;
			int action;
			double dev, ping_deviation;
			int center;
			double lowdist, highdist;
			int num_good;
			int j, k;

			/* ============================================================
			 * Per-ping loop
			 * ============================================================ */
			bool done = false;
			while (!done) {
				error = MB_ERROR_NO_ERROR;
				status = mb_get(verbose, mbio_ptr, &kind, &pingsread,
								ping[nrec].time_i, &ping[nrec].time_d,
								&ping[nrec].navlon, &ping[nrec].navlat,
								&ping[nrec].speed, &ping[nrec].heading,
								&distance, &altitude, &sensordepth,
								&ping[nrec].beams_bath, &beams_amp, &pixels_ss,
								ping[nrec].beamflag, ping[nrec].bath, amp,
								ping[nrec].bathacrosstrack, ping[nrec].bathalongtrack,
								ss, ssacrosstrack, ssalongtrack, comment, &error);

				if (status == MB_SUCCESS && kind == MB_DATA_DATA) {
					/* ping multiplicity */
					status = mb_get_store(verbose, mbio_ptr, &store_ptr, &error);
					const int sh_status = mb_sensorhead(verbose, mbio_ptr, store_ptr,
														&sensorhead, &sensorhead_error);
					if (sh_status == MB_SUCCESS)
						ping[nrec].multiplicity = sensorhead;
					else if (nrec > 0 && fabs(ping[nrec].time_d - ping[nrec-1].time_d) < MB_ESF_MAXTIMEDIFF)
						ping[nrec].multiplicity = ping[nrec-1].multiplicity + 1;
					else
						ping[nrec].multiplicity = 0;

					/* save original beamflags */
					for (i = 0; i < ping[nrec].beams_bath; i++)
						ping[nrec].beamflagorg[i] = ping[nrec].beamflag[i];

					/* local coordinates */
					mb_coor_scale(verbose, ping[nrec].navlat, &mtodeglon, &mtodeglat);
					const double headingx = sin(ping[nrec].heading * DTR);
					const double headingy = cos(ping[nrec].heading * DTR);
					for (j = 0; j <= nrec; j++) {
						for (i = 0; i < ping[j].beams_bath; i++) {
							ping[j].bathx[i] = (ping[j].navlon - ping[0].navlon) / mtodeglon
											 + headingy * ping[j].bathacrosstrack[i]
											 + headingx * ping[j].bathalongtrack[i];
							ping[j].bathy[i] = (ping[j].navlat - ping[0].navlat) / mtodeglat
											 - headingx * ping[j].bathacrosstrack[i]
											 + headingy * ping[j].bathalongtrack[i];
						}
					}

					if (fix_edit_timestamps)
						mb_esf_fixtimestamps(verbose, &esf, ping[nrec].time_d, tolerance, &error);

					mb_esf_apply(verbose, &esf, ping[nrec].time_d, ping[nrec].multiplicity,
								 ping[nrec].beams_bath, ping[nrec].beamflag, &error);

					for (i = 0; i < ping[nrec].beams_bath; i++) {
						if (ping[nrec].beamflag[i] != ping[nrec].beamflagorg[i]) {
							if (mb_beam_ok(ping[nrec].beamflag[i])) nunflagesf++;
							else                                    nflagesf++;
						}
					}
					ndata++;
					nrec++;
				}
				else if (error > MB_ERROR_NO_ERROR) {
					done = true;
				}

				/* ----- process one record ----- */
				if (nrec > 0) {
					const int irec = (nrec >= 2) ? 1 : 0;
					center = ping[irec].beams_bath / 2;

					/* zap outer beams by number */
					if (zap_beams) {
						for (i = 0; i < MIN(zap_beams_left, center); i++) {
							if (mb_beam_ok(ping[irec].beamflag[i])) {
								ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
								nouterbeams++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_FILTER, &error);
							}
						}
						for (i = 0; i < MIN(zap_beams_right, center); i++) {
							int jj = ping[irec].beams_bath - i - 1;
							if (mb_beam_ok(ping[irec].beamflag[jj])) {
								ping[irec].beamflag[jj] = MB_FLAG_FLAG + MB_FLAG_FILTER;
								nouterbeams++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											jj + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_FILTER, &error);
							}
						}
					}

					/* flag by distance */
					if (flag_distance) {
						for (i = 0; i < ping[irec].beams_bath; i++) {
							if (mb_beam_ok(ping[irec].beamflag[i]) &&
								(ping[irec].bathacrosstrack[i] <= flag_distance_left ||
								 ping[irec].bathacrosstrack[i] >= flag_distance_right)) {
								ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
								nouterdistance++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_FILTER, &error);
							}
						}
					}

					/* unflag by distance */
					if (unflag_distance) {
						for (i = 0; i < ping[irec].beams_bath; i++) {
							if (ping[irec].beamflag[i] != MB_FLAG_NULL &&
								!mb_beam_ok(ping[irec].beamflag[i]) &&
								ping[irec].bathacrosstrack[i] >= unflag_distance_left &&
								ping[irec].bathacrosstrack[i] <= unflag_distance_right) {
								ping[irec].beamflag[i] = MB_FLAG_NONE;
								ninnerdistance++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_UNFLAG, &error);
							}
						}
					}

					/* flag by acrosstrack angle */
					if (flag_angle) {
						for (i = 0; i < ping[irec].beams_bath; i++) {
							if (mb_beam_ok(ping[irec].beamflag[i])) {
								double theta, phi, pitch, roll;
								const double xx = ping[irec].bathacrosstrack[i];
								const double yy = ping[irec].bathalongtrack[i];
								const double zz = ping[irec].bath[i] - sensordepth;
								mb_xyz_to_takeoff(verbose, xx, yy, zz, &theta, &phi, &error);
								mb_takeoff_to_rollpitch(verbose, theta, phi, &pitch, &roll, &error);
								roll = 90.0 - roll;
								if (roll <= flag_angle_left || roll >= flag_angle_right) {
									ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
									nouterangle++; nflag++;
									mb_ess_save(verbose, &esf, ping[irec].time_d,
												i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
												MBP_EDIT_FILTER, &error);
								}
							}
						}
					}

					/* unflag by acrosstrack angle */
					if (unflag_angle) {
						for (i = 0; i < ping[irec].beams_bath; i++) {
							if (ping[irec].beamflag[i] != MB_FLAG_NULL &&
								!mb_beam_ok(ping[irec].beamflag[i])) {
								double theta, phi, pitch, roll;
								const double xx = ping[irec].bathacrosstrack[i];
								const double yy = ping[irec].bathalongtrack[i];
								const double zz = ping[irec].bath[i] - sensordepth;
								mb_xyz_to_takeoff(verbose, xx, yy, zz, &theta, &phi, &error);
								mb_takeoff_to_rollpitch(verbose, theta, phi, &pitch, &roll, &error);
								roll = 90.0 - roll;
								if (roll >= unflag_angle_left && roll <= unflag_angle_right) {
									ping[irec].beamflag[i] = MB_FLAG_NONE;
									ninnerangle++; nflag++;
									mb_ess_save(verbose, &esf, ping[irec].time_d,
												i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
												MBP_EDIT_UNFLAG, &error);
								}
							}
						}
					}

					/* speed range */
					if (check_speed_good) {
						if (ping[irec].speed > speed_high || ping[irec].speed < speed_low) {
							for (i = 0; i < ping[irec].beams_bath; i++) {
								ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
								nspeed++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_FILTER, &error);
							}
						}
					}

					/* position bounds */
					if (check_position_bounds) {
						if (ping[irec].navlon < west || ping[irec].navlon > east ||
							ping[irec].navlat < south || ping[irec].navlat > north) {
							for (i = 0; i < ping[irec].beams_bath; i++) {
								ping[irec].beamflag[i] = MB_FLAG_NULL;
								nrangepos++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_ZERO, &error);
							}
						}
					}

					/* zero position */
					if (check_zero_position) {
						if (ping[irec].navlon == 0.0 && ping[irec].navlat == 0.0) {
							for (i = 0; i < ping[irec].beams_bath; i++) {
								ping[irec].beamflag[i] = MB_FLAG_NULL;
								nzeropos++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_ZERO, &error);
							}
						}
					}

					/* depth range */
					if (check_range) {
						for (i = 0; i < ping[irec].beams_bath; i++) {
							if (mb_beam_ok(ping[irec].beamflag[i]) &&
								(ping[irec].bath[i] < depth_low || ping[irec].bath[i] > depth_high)) {
								ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
								ndepthrange++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_FILTER, &error);
							}
						}
					}

					/* minimum range */
					if (check_range_min) {
						for (i = 0; i < ping[irec].beams_bath; i++) {
							if (mb_beam_ok(ping[irec].beamflag[i])) {
								const double xx = ping[irec].bathacrosstrack[i];
								const double yy = ping[irec].bathalongtrack[i];
								const double zz = ping[irec].bath[i] - sensordepth;
								if (sqrt(xx*xx + yy*yy + zz*zz) < range_min) {
									ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
									nminrange++; nflag++;
									mb_ess_save(verbose, &esf, ping[irec].time_d,
												i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
												MBP_EDIT_FILTER, &error);
								}
							}
						}
					}

					/* max heading rate */
					if (zap_max_heading_rate) {
						double heading_rate;
						if (nrec > 1) {
							double dh = ping[nrec-1].heading - ping[0].heading;
							if (dh >  180) dh -= 360;
							if (dh < -180) dh += 360;
							heading_rate = dh / (ping[nrec-1].time_d - ping[0].time_d);
						} else {
							heading_rate = 0.0;
						}
						for (i = 0; i < ping[irec].beams_bath; i++) {
							if (fabs(heading_rate) > max_heading_rate) {
								ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
								nmax_heading_rate++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_FILTER, &error);
							}
						}
					}

					/* zap rails */
					if (zap_rails) {
						lowdist = 0.0; highdist = 0.0;
						for (j = center; j < ping[irec].beams_bath; j++) {
							if (mb_beam_ok(ping[irec].beamflag[j]) &&
								ping[irec].bathacrosstrack[j] <= highdist - backup_dist) {
								ping[irec].beamflag[j] = MB_FLAG_FLAG + MB_FLAG_FILTER;
								nrail++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											j + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_FILTER, &error);
							} else {
								highdist = ping[irec].bathacrosstrack[j];
							}
							int kk = center - (j - center) - 1;
							if (kk >= 0 && kk < ping[irec].beams_bath &&
								mb_beam_ok(ping[irec].beamflag[kk]) &&
								ping[irec].bathacrosstrack[kk] >= lowdist + backup_dist) {
								ping[irec].beamflag[kk] = MB_FLAG_FLAG + MB_FLAG_FILTER;
								nrail++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											kk + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_FILTER, &error);
							} else if (kk >= 0 && kk < ping[irec].beams_bath) {
								lowdist = ping[irec].bathacrosstrack[kk];
							}
						}
					}

					/* zap long acrosstrack */
					if (zap_long_across) {
						for (j = 0; j < ping[irec].beams_bath; j++) {
							if (mb_beam_ok(ping[irec].beamflag[j]) &&
								fabs(ping[irec].bathacrosstrack[j]) > max_acrosstrack) {
								ping[irec].beamflag[j] = MB_FLAG_FLAG + MB_FLAG_FILTER;
								nlong_across++; nflag++;
								mb_ess_save(verbose, &esf, ping[irec].time_d,
											j + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											MBP_EDIT_FILTER, &error);
							}
						}
					}

					/* tests that loop over all available beams */
					if (check_fraction || check_deviation || check_spike || check_slope) {
						for (i = 0; i < ping[irec].beams_bath; i++) {
							if (mb_beam_ok(ping[irec].beamflag[i])) {
								if (median <= 0.0) median = ping[irec].bath[i];
								nlist = 0;
								for (j = 0; j < nrec; j++) {
									for (k = 0; k < ping[j].beams_bath; k++) {
										if (mb_beam_ok(ping[j].beamflag[k])) {
											const double ddx = ping[j].bathx[k] - ping[irec].bathx[i];
											const double ddy = ping[j].bathy[k] - ping[irec].bathy[i];
											const double dd  = sqrt(ddx*ddx + ddy*ddy);
											if (dd <= distancemax * median) {
												list[nlist] = ping[j].bath[k];
												nlist++;
											}
										}
									}
								}
								qsort((char *)list, nlist, sizeof(double), mb_double_compare);
								if (nlist > 0) median = list[nlist/2];

								/* fractional deviation from median */
								if (check_fraction && median > 0.0) {
									if (ping[irec].bath[i] / median < fraction_low ||
										ping[irec].bath[i] / median > fraction_high) {
										ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
										nfraction++; nflag++;
										mb_ess_save(verbose, &esf, ping[irec].time_d,
													i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
													MBP_EDIT_FILTER, &error);
									}
								}

								/* absolute deviation from median */
								if (check_deviation && median > 0.0) {
									if (fabs(ping[irec].bath[i] - median) > deviation_max) {
										ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
										ndeviation++; nflag++;
										mb_ess_save(verbose, &esf, ping[irec].time_d,
													i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
													MBP_EDIT_FILTER, &error);
									}
								}

								/* spike check (acrosstrack) */
								if (check_spike && (spike_mode & 1) && median > 0.0 &&
									i > 0 && i < ping[irec].beams_bath - 1 &&
									mb_beam_ok(ping[irec].beamflag[i-1]) &&
									mb_beam_ok(ping[irec].beamflag[i+1])) {
									const double ax = ping[irec].bathx[i-1] - ping[irec].bathx[i];
									const double ay = ping[irec].bathy[i-1] - ping[irec].bathy[i];
									const double dd = sqrt(ax*ax + ay*ay);
									if (dd > distancemin*median && dd <= distancemax*median) {
										const double slope = (ping[irec].bath[i-1] - ping[irec].bath[i]) / dd;
										const double bx = ping[irec].bathx[i+1] - ping[irec].bathx[i];
										const double by = ping[irec].bathy[i+1] - ping[irec].bathy[i];
										const double dd2 = sqrt(bx*bx + by*by);
										if (dd2 > distancemin*median && dd2 <= distancemax*median) {
											const double slope2 = (ping[irec].bath[i] - ping[irec].bath[i+1]) / dd2;
											if ((slope > spikemax && slope2 < -spikemax) ||
												(slope2 > spikemax && slope < -spikemax)) {
												ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
												nspike++; nflag++;
												mb_ess_save(verbose, &esf, ping[irec].time_d,
															i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
															MBP_EDIT_FILTER, &error);
											}
										}
									}
								}

								/* spike check (alongtrack, needs 3 pings) */
								if (check_spike && nrec == 3 && (spike_mode & 2) &&
									mb_beam_ok(ping[0].beamflag[i]) &&
									mb_beam_ok(ping[2].beamflag[i])) {
									const double ax = ping[0].bathx[i] - ping[1].bathx[i];
									const double ay = ping[0].bathy[i] - ping[1].bathy[i];
									const double dd = sqrt(ax*ax + ay*ay);
									if (dd > distancemin*median && dd <= distancemax*median) {
										const double slope = (ping[0].bath[i] - ping[1].bath[i]) / dd;
										const double bx = ping[2].bathx[i] - ping[1].bathx[i];
										const double by = ping[2].bathy[i] - ping[1].bathy[i];
										const double dd2 = sqrt(bx*bx + by*by);
										if (dd2 > distancemin*median && dd2 <= distancemax*median) {
											const double slope2 = (ping[1].bath[i] - ping[2].bath[i]) / dd2;
											if ((slope > spikemax && slope2 < -spikemax) ||
												(slope2 > spikemax && slope < -spikemax)) {
												ping[1].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
												nspike++; nflag++;
												mb_ess_save(verbose, &esf, ping[1].time_d,
															i + ping[1].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
															MBP_EDIT_FILTER, &error);
											}
										}
									}
								}

								/* slope check (needs 3 pings) */
								if (check_slope && nrec == 3 && median > 0.0) {
									for (j = 0; j < nrec; j++) {
										for (k = 0; k < ping[j].beams_bath; k++) {
											if (mb_beam_ok(ping[j].beamflag[k])) {
												const double ax = ping[j].bathx[k] - ping[1].bathx[i];
												const double ay = ping[j].bathy[k] - ping[1].bathy[i];
												const double dd = sqrt(ax*ax + ay*ay);
												const double slope = (dd > 0.0 && dd <= distancemax * median)
													? fabs((ping[j].bath[k] - ping[1].bath[i]) / dd) : 0.0;
												struct bad_struct bad[2] = {
													{ false, 0, 0, 0.0 }, { false, 0, 0, 0.0 }
												};
												if (slope > slopemax && dd > distancemin*median) {
													if (clean_mode == MBCLEAN_FLAG_BOTH) {
														bad[0].flag=true; bad[0].ping=j; bad[0].beam=k; bad[0].bath=ping[j].bath[k];
														bad[1].flag=true; bad[1].ping=1; bad[1].beam=i; bad[1].bath=ping[1].bath[i];
														ping[j].beamflag[k] = MB_FLAG_FLAG + MB_FLAG_FILTER;
														ping[1].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
														nbad++; nflag += 2;
														mb_ess_save(verbose, &esf, ping[j].time_d,
																	k + ping[j].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
																	MBP_EDIT_FILTER, &error);
														mb_ess_save(verbose, &esf, ping[1].time_d,
																	i + ping[1].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
																	MBP_EDIT_FILTER, &error);
													} else {
														if (fabs(ping[j].bath[k] - median) >
															fabs(ping[1].bath[i] - median)) {
															bad[0].flag=true; bad[0].ping=j; bad[0].beam=k; bad[0].bath=ping[j].bath[k];
															ping[j].beamflag[k] = MB_FLAG_FLAG + MB_FLAG_FILTER;
															mb_ess_save(verbose, &esf, ping[j].time_d,
																		k + ping[j].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
																		MBP_EDIT_FILTER, &error);
														} else {
															bad[0].flag=true; bad[0].ping=1; bad[0].beam=i; bad[0].bath=ping[1].bath[i];
															ping[1].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
															mb_ess_save(verbose, &esf, ping[1].time_d,
																		i + ping[1].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
																		MBP_EDIT_FILTER, &error);
														}
														nbad++; nflag++;
													}
												}
											}
										}
									}
								}
							}
						}
					}

					/* minimum good beams per swath side */
					if (check_num_good_min && num_good_min > 0) {
						/* port side */
						num_good = 0;
						for (i = 0; i < center; i++)
							if (mb_beam_ok(ping[irec].beamflag[i])) num_good++;
						if (num_good < num_good_min) {
							for (i = 0; i < center; i++) {
								if (mb_beam_ok(ping[irec].beamflag[i])) {
									ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
									nmin++; nflag++;
									mb_ess_save(verbose, &esf, ping[irec].time_d,
												i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
												MBP_EDIT_FILTER, &error);
								}
							}
						}
						/* starboard side */
						num_good = 0;
						for (i = center + 1; i < ping[irec].beams_bath; i++)
							if (mb_beam_ok(ping[irec].beamflag[i])) num_good++;
						if (num_good < num_good_min) {
							for (i = center + 1; i < ping[irec].beams_bath; i++) {
								if (mb_beam_ok(ping[irec].beamflag[i])) {
									ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
									nmin++; nflag++;
									mb_ess_save(verbose, &esf, ping[irec].time_d,
												i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
												MBP_EDIT_FILTER, &error);
								}
							}
						}
					}

					/* ping deviation */
					if (check_ping_deviation && nrec >= 3) {
						double devsqsum = 0.0;
						int    ndevsqsum = 0;
						for (i = 0; i < ping[irec].beams_bath; i++) {
							if (mb_beam_ok(ping[irec-1].beamflag[i]) &&
								mb_beam_ok(ping[irec  ].beamflag[i]) &&
								mb_beam_ok(ping[irec+1].beamflag[i])) {
								dev = (ping[irec].bath[i] - ping[irec+1].bath[i])
									+ (ping[irec].bath[i] - ping[irec-1].bath[i]);
								devsqsum += dev * dev;
								ndevsqsum++;
							}
						}
						if (ndevsqsum > (ping[irec].beams_bath / 4)) {
							ping_deviation = sqrt(devsqsum / ndevsqsum);
							if (ping_deviation > ping_deviation_tol) {
								for (i = 0; i < ping[irec].beams_bath; i++) {
									if (mb_beam_ok(ping[irec].beamflag[i])) {
										ping[irec].beamflag[i] = MB_FLAG_FLAG + MB_FLAG_FILTER;
										npingdeviation++; nflag++;
										mb_ess_save(verbose, &esf, ping[irec].time_d,
													i + ping[irec].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
													MBP_EDIT_FILTER, &error);
									}
								}
							}
						}
					}
				}

				/* write out edits from completed pings */
				if ((status == MB_SUCCESS && nrec == 3) || done) {
					int how_many = done ? nrec : 1;
					int irec2;
					for (irec2 = 0; irec2 < how_many; irec2++) {
						for (i = 0; i < ping[irec2].beams_bath; i++) {
							if (ping[irec2].beamflag[i] != ping[irec2].beamflagorg[i]) {
								if (mb_beam_ok(ping[irec2].beamflag[i]))
									action = MBP_EDIT_UNFLAG;
								else if (mb_beam_check_flag_filter2(ping[irec2].beamflag[i]))
									action = MBP_EDIT_FILTER;
								else if (mb_beam_check_flag_filter(ping[irec2].beamflag[i]))
									action = MBP_EDIT_FILTER;
								else if (ping[irec2].beamflag[i] != MB_FLAG_NULL)
									action = MBP_EDIT_FLAG;
								else
									action = MBP_EDIT_ZERO;
								mb_esf_save(verbose, &esf, ping[irec2].time_d,
											i + ping[irec2].multiplicity * MB_ESF_MULTIPLICITY_FACTOR,
											action, &error);
							}
						}
					}
				}

				/* slide records back by one */
				if (status == MB_SUCCESS && nrec == 3) {
					nrec = 2;
					for (j = 0; j < 2; j++) {
						for (i = 0; i < 7; i++) ping[j].time_i[i] = ping[j+1].time_i[i];
						ping[j].time_d        = ping[j+1].time_d;
						ping[j].multiplicity  = ping[j+1].multiplicity;
						ping[j].navlon        = ping[j+1].navlon;
						ping[j].navlat        = ping[j+1].navlat;
						ping[j].speed         = ping[j+1].speed;
						ping[j].heading       = ping[j+1].heading;
						ping[j].beams_bath    = ping[j+1].beams_bath;
						for (i = 0; i < ping[j].beams_bath; i++) {
							ping[j].beamflag[i]        = ping[j+1].beamflag[i];
							ping[j].beamflagorg[i]     = ping[j+1].beamflagorg[i];
							ping[j].bath[i]            = ping[j+1].bath[i];
							ping[j].bathacrosstrack[i] = ping[j+1].bathacrosstrack[i];
							ping[j].bathalongtrack[i]  = ping[j+1].bathalongtrack[i];
							ping[j].bathx[i]           = ping[j+1].bathx[i];
							ping[j].bathy[i]           = ping[j+1].bathy[i];
						}
					}
				}
			}  /* per-ping while */

			status = mb_close(verbose, &mbio_ptr, &error);
			status = mb_esf_close(verbose, &esf, &error);

			if (esffile_open) {
				status = mb_pr_update_format(verbose, swathfile, true, format, &error);
				status = mb_pr_update_edit(verbose, swathfile, MBP_EDIT_ON, esffile, &error);
			}
			if (uselockfiles)
				status = mb_pr_unlockswathfile(verbose, swathfile, MBP_LOCK_EDITBATHY,
											   THIS_MODULE_NAME, &error);

			/* update totals */
			nfiletot++;
			ndatatot          += ndata;
			nflagesftot       += nflagesf;
			nunflagesftot     += nunflagesf;
			nzeroesftot       += nzeroesf;
			ndepthrangetot    += ndepthrange;
			nminrangetot      += nminrange;
			nfractiontot      += nfraction;
			ndeviationtot     += ndeviation;
			nouterbeamstot    += nouterbeams;
			nouterdistancetot += nouterdistance;
			ninnerdistancetot += ninnerdistance;
			nouterangletot    += nouterangle;
			ninnerangletot    += ninnerangle;
			nrailtot          += nrail;
			nlong_acrosstot   += nlong_across;
			nmax_heading_ratetot += nmax_heading_rate;
			nmintot           += nmin;
			nbadtot           += nbad;
			nspiketot         += nspike;
			npingdeviationtot += npingdeviation;
			nflagtot          += nflag;
			nunflagtot        += nunflag;

			GMT_Report(API, GMT_MSG_NORMAL,
				"%d records | flag:%d unflag:%d outer:%d dist:%d angle:%d rails:%d "
				"long:%d hdgrate:%d depth:%d minrange:%d fraction:%d speed:%d "
				"zeropos:%d devmedian:%d nfew:%d slopes:%d spikes:%d pingdev:%d\n",
				ndata, nflag, nunflag, nouterbeams, nouterdistance, nouterangle,
				nrail, nlong_across, nmax_heading_rate, ndepthrange, nminrange,
				nfraction, nspeed, nzeropos, ndeviation, nmin, nbad, nspike,
				npingdeviation);
		}

		/* next file? */
		if (read_datalist) {
			read_data = (mb_datalist_read(verbose, datalist, swathfile, dfile,
										  &format, &file_weight, &error) == MB_SUCCESS);
		} else {
			read_data = false;
		}
	}  /* per-file while */

	if (read_datalist) mb_datalist_close(verbose, &datalist, &error);

	/* give the total statistics, as the program does */
	fprintf(stderr, "\nMBclean Processing Totals:\n");
	fprintf(stderr, "-------------------------\n");
	fprintf(stderr, "%d total swath data files processed\n", nfiletot);
	fprintf(stderr, "%d total bathymetry data records processed\n", ndatatot);
	fprintf(stderr, "%d total beams flagged in old esf files\n", nflagesftot);
	fprintf(stderr, "%d total beams unflagged in old esf files\n", nunflagesftot);
	fprintf(stderr, "%d total beams zeroed in old esf files\n", nzeroesftot);
	fprintf(stderr, "%d total beams zapped by beam number\n", nouterbeamstot);
	fprintf(stderr, "%d total beams zapped by distance\n", nouterdistancetot);
	fprintf(stderr, "%d total beams unzapped by distance\n", ninnerdistancetot);
	fprintf(stderr, "%d total beams zapped by angle\n", nouterangletot);
	fprintf(stderr, "%d total beams unzapped by angle\n", ninnerangletot);
	fprintf(stderr, "%d total beams zapped for too few good beams in ping\n", nmintot);
	fprintf(stderr, "%d total beams out of acceptable depth range\n", ndepthrangetot);
	fprintf(stderr, "%d total beams less than minimum range\n", nminrangetot);
	fprintf(stderr, "%d total beams out of acceptable fractional depth range\n", nfractiontot);
	// int nspeedtot = 0;
	// fprintf(stderr, "%d total beams out of acceptable speed range\n", nspeedtot);
	// int nzeropostot = 0;
	// fprintf(stderr, "%d total beams zero position (lat/lon)\n", nzeropostot);
	fprintf(stderr, "%d total beams exceed acceptable deviation from median depth\n", ndeviationtot);
	fprintf(stderr, "%d total bad rail beams identified\n", nrailtot);
	fprintf(stderr, "%d total long acrosstrack beams identified\n", nlong_acrosstot);
	fprintf(stderr, "%d total max heading rate beams identified\n", nmax_heading_ratetot);
	fprintf(stderr, "%d total excessive spikes identified\n", nspiketot);
	fprintf(stderr, "%d total excessive slopes identified\n", nbadtot);
	fprintf(stderr, "%d ping deviations identified\n", npingdeviationtot);
	fprintf(stderr, "%d total beams flagged\n", nflagtot);
	fprintf(stderr, "%d total beams unflagged\n", nunflagtot);
	if (mb_memory_list(verbose, &error) == MB_FAILURE)
		GMT_Report(API, GMT_MSG_NORMAL,
				   "Program %s completed but leaked memory\n", THIS_MODULE_NAME);

	Return(GMT_NOERROR);
}

/* --------------------------------------------------------------------
 * mbclean_save_edit — byteswap-aware write of one edit record.
 * -------------------------------------------------------------------- */
static int mbclean_save_edit(int verbose, FILE *sofp, double time_d,
							 int beam, int action, int *error) {
	int status = MB_SUCCESS;

	if (sofp != NULL) {
#ifdef BYTESWAPPED
		mb_swap_double(&time_d);
		beam   = mb_swap_int(beam);
		action = mb_swap_int(action);
#endif
		if (fwrite(&time_d, sizeof(double), 1, sofp) != 1) {
			status = MB_FAILURE; *error = MB_ERROR_WRITE_FAIL;
		}
		if (status == MB_SUCCESS && fwrite(&beam, sizeof(int), 1, sofp) != 1) {
			status = MB_FAILURE; *error = MB_ERROR_WRITE_FAIL;
		}
		if (status == MB_SUCCESS && fwrite(&action, sizeof(int), 1, sofp) != 1) {
			status = MB_FAILURE; *error = MB_ERROR_WRITE_FAIL;
		}
	}

	if (verbose > 5) {
		/* keep the param-use linter quiet */
		(void)time_d;
	}
	return status;
}
/* end mbclean.c */
