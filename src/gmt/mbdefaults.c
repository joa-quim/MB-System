/*--------------------------------------------------------------------
 *    The MB-system:	mbdefaults.c	1/23/93
 *
 *    Copyright (c) 1993-2025 by
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
 * MBDEFAULTS sets and retrieves the default MBIO control parameters
 * stored in the file ~/.mbio_defaults.  Only the parameters specified
 * by command line arguments will be changed; if no ~/.mbio_defaults
 * file exists one will be created.
 *
 * Author:	D. W. Caress
 * Date:	January 23, 1993
 */
/*
 * GMT-module port of src/utilities/mbdefaults.cc, built like GMT's own gmtdefaults: the options
 * come from GMT's option list (parse()), the program's long options are GMT long options (the
 * module_kw table: --lonflip=0 is -L0), and the listing is written as text records through the
 * GMT API, so a GMT.jl/Python/MATLAB caller gets it back as a dataset and the command line gets
 * it on stdout. The program's lower-case option aliases are kept: they are GMT common-option
 * letters (-b, -f, -i, -t, ...), but GMT_Parse_Common only parses the common options named in
 * THIS_MODULE_OPTIONS, so the others reach parse() untouched.
 */

#define THIS_MODULE_NAME "mbdefaults"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Set and list the default MBIO control parameters in ~/.mbio_defaults"
/* No data input; the current or new defaults are written as text records. */
#define THIS_MODULE_KEYS ">D}"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include "unistd_w.h"
#else
#include <unistd.h>
#endif
#include "mb_define.h"
#include "mb_status.h"
#include "mb_gmt_text.h"

/* colortable view mode defines */
enum { MBV_COLORTABLE_HAXBY = 0 };
enum { MBV_COLORTABLE_BRIGHT = 1 };
enum { MBV_COLORTABLE_MUTED = 2 };
enum { MBV_COLORTABLE_GRAY = 3 };
enum { MBV_COLORTABLE_FLAT = 4 };
enum { MBV_COLORTABLE_SEALEVEL1 = 5 };
enum { MBV_COLORTABLE_SEALEVEL2 = 6 };

/* colortable view mode defines */
typedef enum {
	MBV_COLORTABLE_NORMAL = 0,
	MBV_COLORTABLE_REVERSED = 1,
} colortable_mode_t;

/* shade view mode defines */
enum { MBV_SHADE_VIEW_NONE = 0 };
enum { MBV_SHADE_VIEW_ILLUMINATION = 1 };
enum { MBV_SHADE_VIEW_SLOPE = 2 };
enum { MBV_SHADE_VIEW_OVERLAY = 3 };

static const char program_name[] = "MBDEFAULTS";
static const char help_message[] =
    "MBDEFAULTS sets and retrieves the /default MBIO control\n"
    "parameters stored in the file ~/.mbio_defaults.\n"
    "Only the parameters specified by command line\n"
    "arguments will be changed; if no ~/.mbio_defaults\n"
    "file exists one will be created.";

/* Translation table from the program's long options to the module's short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'B', "file-io-buffer",  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'D', "ps-display",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'F', "fbt-version",     "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",            "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "image-display",   "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'L', "lonflip",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'M', "mbview-settings", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'T', "time-gap",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'U', "use-lock-files",  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'W', "project",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

#define MBDEFAULTS_N_M 16	/* -M may be given once per mbview setting */

struct MBDEFAULTS_CTRL {
	struct MBDEFAULTS_B { bool active; int fileiobuffer; } B;
	struct MBDEFAULTS_D { bool active; char *psdisplay; } D;
	struct MBDEFAULTS_F { bool active; int fbtversion; } F;
	struct MBDEFAULTS_H { bool active; } H;
	struct MBDEFAULTS_I { bool active; char *imgdisplay; } I;
	struct MBDEFAULTS_L { bool active; int lonflip; } L;
	struct MBDEFAULTS_M { unsigned int n; char *arg[MBDEFAULTS_N_M]; } M;
	struct MBDEFAULTS_T { bool active; double timegap; } T;
	struct MBDEFAULTS_U { bool active; bool uselockfiles; } U;
	struct MBDEFAULTS_W { bool active; char *mbproject; } W;
};

static void *New_Ctrl(struct GMT_CTRL *GMT) {
	struct MBDEFAULTS_CTRL *C = gmt_M_memory(GMT, NULL, 1, struct MBDEFAULTS_CTRL);
	return C;
}

static void Free_Ctrl(struct GMT_CTRL *GMT, struct MBDEFAULTS_CTRL *C) {
	if (!C) return;
	gmt_M_str_free(C->D.psdisplay);
	gmt_M_str_free(C->I.imgdisplay);
	gmt_M_str_free(C->W.mbproject);
	for (unsigned int k = 0; k < C->M.n; k++) gmt_M_str_free(C->M.arg[k]);
	gmt_M_free(GMT, C);
}

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Usage(API, 0, "usage: %s [-B<fileiobuffer>] [-D<psdisplay>] [-F<fbtversion>] [-H] [-I<imgdisplay>] "
	          "[-L<lonflip>] [-M<P|G|O|I|S><values>] [-T<timegap>] [-U<yes|no>] [-W<project>] [%s]\n",
	          THIS_MODULE_NAME, GMT_V_OPT);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n\n", help_message);
	GMT_Message(API, GMT_TIME_NONE, "  OPTIONAL ARGUMENTS:\n");
	GMT_Usage(API, 1, "\n-B<fileiobuffer> (--file-io-buffer=)");
	GMT_Usage(API, -2, "File i/o buffering: 0 = standard fread()/fwrite() buffering, >0 = buffer size in kB, <0 = mmap.");
	GMT_Usage(API, 1, "\n-D<psdisplay> (--ps-display=)");
	GMT_Usage(API, -2, "Program used to display PostScript.");
	GMT_Usage(API, 1, "\n-F<fbtversion> (--fbt-version=)");
	GMT_Usage(API, -2, "Fbt file version: 2 or old, 3 or new.");
	GMT_Usage(API, 1, "\n-H (--help)");
	GMT_Usage(API, -2, "Print this help.");
	GMT_Usage(API, 1, "\n-I<imgdisplay> (--image-display=)");
	GMT_Usage(API, -2, "Program used to display images.");
	GMT_Usage(API, 1, "\n-L<lonflip> (--lonflip=)");
	GMT_Usage(API, -2, "Longitude range: -1 = [-360,0], 0 = [-180,180], 1 = [0,360].");
	GMT_Usage(API, 1, "\n-M<setting> (--mbview-settings=)");
	GMT_Usage(API, -2, "mbview defaults, repeatable: P<colortable>/<mode>/<shademode> primary, G<colortable>/<mode> slope, "
	          "O<colortable>/<mode> overlay, I<magnitude>/<elevation>/<azimuth> illumination, S<magnitude> slope shading.");
	GMT_Usage(API, 1, "\n-T<timegap> (--time-gap=)");
	GMT_Usage(API, -2, "Time gap in minutes.");
	GMT_Usage(API, 1, "\n-U<yes|no> (--use-lock-files=)");
	GMT_Usage(API, -2, "Use lock files.");
	GMT_Usage(API, 1, "\n-W<project> (--project=)");
	GMT_Usage(API, -2, "Default project name.");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

/* The program's -F and -U argument rules, unchanged */
static int mbdefaults_fbtversion(const char *arg) {
	if (strncmp(arg, "new", 3) == 0 || strncmp(arg, "NEW", 3) == 0) return 3;
	if (strncmp(arg, "old", 2) == 0 || strncmp(arg, "OLD", 2) == 0) return 2;
	if (strncmp(arg, "2", 1) == 0) return 2;
	if (strncmp(arg, "3", 1) == 0) return 3;
	return 0;
}

static int mbdefaults_yesno(const char *arg) {
	if (strncmp(arg, "yes", 3) == 0 || strncmp(arg, "YES", 3) == 0) return 1;
	if (strncmp(arg, "no", 2) == 0 || strncmp(arg, "NO", 2) == 0) return 0;
	if (strncmp(arg, "1", 1) == 0) return 1;
	if (strncmp(arg, "0", 1) == 0) return 0;
	return -1;
}

static int parse(struct GMT_CTRL *GMT, struct MBDEFAULTS_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0, n_files = 0;
	struct GMT_OPTION *opt = NULL;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
			case '<':	/* No input files */
				n_files++;
				break;
			/* Every option keeps the program's lower-case alias. GMT_Parse_Common only touches the
			   common options named in THIS_MODULE_OPTIONS (-V), so -b, -d, -f, -h, -i, -l, -m, -t,
			   -u, -v and -w arrive here untouched and mean what they mean in MB-System. */
			case 'B': case 'b':
				n_errors += gmt_M_repeated_module_option(API, Ctrl->B.active);
				n_errors += gmt_M_check_condition(GMT, sscanf(opt->arg, "%d", &Ctrl->B.fileiobuffer) != 1,
				                                  "Option -B: Expected -B<fileiobuffer>\n");
				break;
			case 'D': case 'd':
				n_errors += gmt_M_repeated_module_option(API, Ctrl->D.active);
				n_errors += gmt_get_required_string(GMT, opt->arg, opt->option, 0, &Ctrl->D.psdisplay);
				break;
			case 'F': case 'f':
				n_errors += gmt_M_repeated_module_option(API, Ctrl->F.active);
				Ctrl->F.fbtversion = mbdefaults_fbtversion(opt->arg);
				n_errors += gmt_M_check_condition(GMT, Ctrl->F.fbtversion == 0, "Option -F: Expected 2, 3, old or new\n");
				break;
			case 'H': case 'h':
				Ctrl->H.active = true;
				break;
			case 'I': case 'i':
				n_errors += gmt_M_repeated_module_option(API, Ctrl->I.active);
				n_errors += gmt_get_required_string(GMT, opt->arg, opt->option, 0, &Ctrl->I.imgdisplay);
				break;
			case 'L': case 'l':
				n_errors += gmt_M_repeated_module_option(API, Ctrl->L.active);
				n_errors += gmt_M_check_condition(GMT, sscanf(opt->arg, "%d", &Ctrl->L.lonflip) != 1,
				                                  "Option -L: Expected -L<lonflip>\n");
				break;
			case 'M': case 'm':
				if (Ctrl->M.n == MBDEFAULTS_N_M || strchr("PpGgOoIiSs", opt->arg[0]) == NULL || opt->arg[0] == '\0') {
					GMT_Report(API, GMT_MSG_ERROR, "Option -M: Expected -MP|G|O|I|S<values>\n");
					n_errors++;
				}
				else
					Ctrl->M.arg[Ctrl->M.n++] = strdup(opt->arg);
				break;
			case 'T': case 't':
				n_errors += gmt_M_repeated_module_option(API, Ctrl->T.active);
				n_errors += gmt_get_required_double(GMT, opt->arg, opt->option, 0, &Ctrl->T.timegap);
				break;
			case 'U': case 'u':
			{
				n_errors += gmt_M_repeated_module_option(API, Ctrl->U.active);
				const int yn = mbdefaults_yesno(opt->arg);
				n_errors += gmt_M_check_condition(GMT, yn < 0, "Option -U: Expected yes or no\n");
				Ctrl->U.uselockfiles = (yn == 1);
				break;
			}
			case 'W': case 'w':
				n_errors += gmt_M_repeated_module_option(API, Ctrl->W.active);
				n_errors += gmt_get_required_string(GMT, opt->arg, opt->option, 0, &Ctrl->W.mbproject);
				break;
			case 'v':	/* the program's -v: verbosity, as -V */
				GMT->current.setting.verbose = GMT_MSG_INFORMATION;
				break;
			default:
				n_errors += gmt_default_option_error(GMT, opt);
				break;
		}
	}

	n_errors += gmt_M_check_condition(GMT, n_files, "No input files are expected\n");

	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

static const char *mbdefaults_colortable_name(int colortable) {
	switch (colortable) {
		case MBV_COLORTABLE_HAXBY: return "Haxby";
		case MBV_COLORTABLE_BRIGHT: return "Bright";
		case MBV_COLORTABLE_MUTED: return "Muted";
		case MBV_COLORTABLE_GRAY: return "Grayscale";
		case MBV_COLORTABLE_FLAT: return "Flat  gray";
		case MBV_COLORTABLE_SEALEVEL1: return "Sealevel 1";
		case MBV_COLORTABLE_SEALEVEL2: return "Sealevel 2";
		default: return NULL;
	}
}

static const char *mbdefaults_shade_name(int shade_mode) {
	switch (shade_mode) {
		case MBV_SHADE_VIEW_NONE: return "No shading";
		case MBV_SHADE_VIEW_ILLUMINATION: return "Shading by illumination";
		case MBV_SHADE_VIEW_SLOPE: return "Shading by slope magnitude";
		case MBV_SHADE_VIEW_OVERLAY: return "Shading by overlay";
		default: return NULL;
	}
}

#define bailout(code) { gmt_M_free_options(mode); return code; }
#define Return(code) { Free_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }

EXTERN_MSC int GMT_mbdefaults(void *V_API, int mode, void *args);

int GMT_mbdefaults(void *V_API, int mode, void *args) {
	int error = 0;
	struct MBDEFAULTS_CTRL *Ctrl = NULL;
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MB_GMT_TEXT *T = NULL;
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);

	/*----------------------- Standard module initialization and parsing ----------------------*/

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;

	/* 1: like gmtdefaults, no options at all is a normal run (list the defaults), not a usage request */
	if ((error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(error);

	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS,
	                           module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = New_Ctrl(GMT);
	if ((error = parse(GMT, Ctrl, options)) != 0) Return(error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	/*---------------------------- This is the mbdefaults main code ----------------------------*/

	const int verbose = (GMT->current.setting.verbose >= GMT_MSG_DEBUG) ? 2 : 0;

	int format;
	int pings;
	int lonflip;
	double bounds[4];
	int btime_i[7];
	int etime_i[7];
	double speedmin;
	double timegap;
	int status = mb_defaults(verbose, &format, &pings, &lonflip, bounds, btime_i, etime_i, &speedmin, &timegap);

	char psdisplay[MB_PATH_MAXLINE];
	char imgdisplay[MB_PATH_MAXLINE];
	char mbproject[MB_PATH_MAXLINE];
	status = mb_env(verbose, psdisplay, imgdisplay, mbproject);

	int primary_colortable;
	colortable_mode_t primary_colortable_mode;
	int primary_shade_mode;
	int slope_colortable;
	int slope_colortable_mode;
	int secondary_colortable;
	colortable_mode_t secondary_colortable_mode;
	double illuminate_magnitude;
	double illuminate_elevation;
	double illuminate_azimuth;
	double slope_magnitude;
	{
		int primary_colortable_mode_tmp;
		int secondary_colortable_mode_tmp;
		status = mb_mbview_defaults(
			verbose, &primary_colortable, &primary_colortable_mode_tmp, &primary_shade_mode, &slope_colortable,
		        &slope_colortable_mode, &secondary_colortable, &secondary_colortable_mode_tmp, &illuminate_magnitude,
		        &illuminate_elevation, &illuminate_azimuth, &slope_magnitude);
		primary_colortable_mode = (colortable_mode_t)primary_colortable_mode_tmp;
		secondary_colortable_mode = (colortable_mode_t)secondary_colortable_mode_tmp;
	}

	int fbtversion = 3;
	status &= mb_fbtversion(verbose, &fbtversion);

	bool uselockfiles = true;
	status &= mb_uselockfiles(verbose, &uselockfiles);

	int fileiobuffer = 0;
	status &= mb_fileiobuffer(verbose, &fileiobuffer);

	/* The options override the current defaults; any of them means a new ~/.mbio_defaults */
	bool flag = false;
	if (Ctrl->B.active) { fileiobuffer = Ctrl->B.fileiobuffer; flag = true; }
	if (Ctrl->D.active) { snprintf(psdisplay, sizeof (psdisplay), "%s", Ctrl->D.psdisplay); flag = true; }
	if (Ctrl->F.active) { fbtversion = Ctrl->F.fbtversion; flag = true; }
	if (Ctrl->I.active) { snprintf(imgdisplay, sizeof (imgdisplay), "%s", Ctrl->I.imgdisplay); flag = true; }
	if (Ctrl->L.active) { lonflip = Ctrl->L.lonflip; flag = true; }
	if (Ctrl->T.active) { timegap = Ctrl->T.timegap; flag = true; }
	if (Ctrl->U.active) { uselockfiles = Ctrl->U.uselockfiles; flag = true; }
	if (Ctrl->W.active) { snprintf(mbproject, sizeof (mbproject), "%s", Ctrl->W.mbproject); flag = true; }
	for (unsigned int k = 0; k < Ctrl->M.n; k++) {
		const char *arg = Ctrl->M.arg[k];
		if (arg[0] == 'P' || arg[0] == 'p') {	/* default primary colortable and modes */
			int tmp = primary_colortable_mode;
			/* n = */ sscanf(&arg[1], "%d/%d/%d", &primary_colortable, &tmp, &primary_shade_mode);
			primary_colortable_mode = (colortable_mode_t)tmp;
		}
		else if (arg[0] == 'G' || arg[0] == 'g') {	/* default slope colortable and mode */
			/* n = */ sscanf(&arg[1], "%d/%d", &slope_colortable, &slope_colortable_mode);
		}
		else if (arg[0] == 'O' || arg[0] == 'o') {	/* default overlay colortable and mode */
			int tmp = secondary_colortable_mode;
			/* n = */ sscanf(&arg[1], "%d/%d", &secondary_colortable, &tmp);
			secondary_colortable_mode = (colortable_mode_t)tmp;
		}
		else if (arg[0] == 'I' || arg[0] == 'i') {	/* default illumination parameters */
			/* n = */ sscanf(&arg[1], "%lf/%lf/%lf", &illuminate_magnitude, &illuminate_elevation, &illuminate_azimuth);
		}
		else if (arg[0] == 'S' || arg[0] == 's') {	/* default slope shading magnitude */
			/* n = */ sscanf(&arg[1], "%lf", &slope_magnitude);
		}
		flag = true;
	}

	GMT_Report(API, GMT_MSG_INFORMATION, "Program %s\n", program_name);
	GMT_Report(API, GMT_MSG_INFORMATION, "MB-system Version %s\n", MB_VERSION);
	GMT_Report(API, GMT_MSG_DEBUG, "Control Parameters:\n");
	GMT_Report(API, GMT_MSG_DEBUG, "     lonflip:                    %d\n", lonflip);
	GMT_Report(API, GMT_MSG_DEBUG, "     timegap:                    %f\n", timegap);
	GMT_Report(API, GMT_MSG_DEBUG, "     psdisplay:                  %s\n", psdisplay);
	GMT_Report(API, GMT_MSG_DEBUG, "     imgdisplay:                 %s\n", imgdisplay);
	GMT_Report(API, GMT_MSG_DEBUG, "     mbproject:                  %s\n", mbproject);
	GMT_Report(API, GMT_MSG_DEBUG, "     fbtversion:                 %d\n", fbtversion);
	GMT_Report(API, GMT_MSG_DEBUG, "     uselockfiles:               %d\n", uselockfiles);
	GMT_Report(API, GMT_MSG_DEBUG, "     fileiobuffer:               %d\n", fileiobuffer);

	/* write out new ~/.mbio_defaults file if needed */
	if (flag) {
		const char *home = getenv("HOME");
#ifdef _WIN32
		/* Windows does not set HOME (only some shells do): mbio reads
		   .mbio_defaults from USERPROFILE then, so write it there too */
		if (home == NULL || home[0] == '\0')
			home = getenv("USERPROFILE");
#endif
		if (home == NULL) {
			GMT_Report(API, GMT_MSG_ERROR, "Could not determine home directory (HOME environment variable not set)\n");
			Return(GMT_RUNTIME_ERROR);
		}
		char file[MB_PATH_MAXLINE];
		snprintf(file, sizeof (file), "%s/.mbio_defaults", home);
		FILE *fp = fopen(file, "w");
		if (fp == NULL) {
			GMT_Report(API, GMT_MSG_ERROR, "Could not open file %s\n", file);
			Return(GMT_ERROR_ON_FOPEN);
		}
		fprintf(fp, "MBIO Default Control Parameters\n");
		fprintf(fp, "lonflip:    %d\n", lonflip);
		fprintf(fp, "timegap:    %f\n", timegap);
		fprintf(fp, "ps viewer:  %s\n", psdisplay);
		fprintf(fp, "img viewer: %s\n", imgdisplay);
		fprintf(fp, "project:    %s\n", mbproject);
		fprintf(fp, "fbtversion: %d\n", fbtversion);
		fprintf(fp, "uselockfiles:%d\n", uselockfiles);
		fprintf(fp, "fileiobuffer:%d\n", fileiobuffer);
		fprintf(fp, "mbview_primary_colortable:        %d\n", primary_colortable);
		fprintf(fp, "mbview_primary_colortable_mode:   %d\n", primary_colortable_mode);
		fprintf(fp, "mbview_primary_shade_mode:        %d\n", primary_shade_mode);
		fprintf(fp, "mbview_slope_colortable:          %d\n", slope_colortable);
		fprintf(fp, "mbview_slope_colortable_mode:     %d\n", slope_colortable_mode);
		fprintf(fp, "mbview_secondary_colortable:      %d\n", secondary_colortable);
		fprintf(fp, "mbview_secondary_colortable_mode: %d\n", secondary_colortable_mode);
		fprintf(fp, "mbview_illuminate_magnitude:      %f\n", illuminate_magnitude);
		fprintf(fp, "mbview_illuminate_elevation:      %f\n", illuminate_elevation);
		fprintf(fp, "mbview_illuminate_azimuth:        %f\n", illuminate_azimuth);
		fprintf(fp, "mbview_slope_magnitude:           %f\n", slope_magnitude);
		fclose(fp);
	}

	/* The listing: the new defaults when any was set, else the current ones. Text records through
	   the GMT API (like gmtinfo/grdinfo), so the caller receives them, whatever it is. */
	if ((T = mb_gmt_text_begin(GMT, options)) == NULL)
		Return(API->error);

	const char *name;
	mb_gmt_text_put(T, "%s MBIO Default Control Parameters:\n", flag ? "New" : "Current");
	mb_gmt_text_put(T, "lonflip:    %d\n", lonflip);
	mb_gmt_text_put(T, "timegap:    %f\n", timegap);
	mb_gmt_text_put(T, "ps viewer:  %s\n", psdisplay);
	mb_gmt_text_put(T, "img viewer: %s\n", imgdisplay);
	mb_gmt_text_put(T, "project:    %s\n", mbproject);
	if (fbtversion == 2)
		mb_gmt_text_put(T, "fbtversion: 2 (old)\n");
	else if (fbtversion == 3)
		mb_gmt_text_put(T, "fbtversion: 3 (new)\n");
	else
		mb_gmt_text_put(T, "fbtversion: %d\n", fbtversion);
	mb_gmt_text_put(T, "uselockfiles: %d\n", uselockfiles);
	if (fileiobuffer == 0)
		mb_gmt_text_put(T, "fileiobuffer: %d (use standard fread() & fwrite() buffering)\n", fileiobuffer);
	else if (fileiobuffer > 0)
		mb_gmt_text_put(T, "fileiobuffer: %d (use %d kB buffer for fread() & fwrite())\n", fileiobuffer, fileiobuffer);
	else
		mb_gmt_text_put(T, "fileiobuffer: %d (use mmap for file i/o)\n", fileiobuffer);
	if ((name = mbdefaults_colortable_name(primary_colortable)) != NULL)
		mb_gmt_text_put(T, "mbview primary colortable:         %d  (%s)\n", primary_colortable, name);
	if (primary_colortable_mode == MBV_COLORTABLE_NORMAL)
		mb_gmt_text_put(T, "mbview primary colortable mode:    %d  (Normal: Cold to Hot)\n", primary_colortable_mode);
	else
		mb_gmt_text_put(T, "mbview primary colortable mode:    %d  (Reversed: Hot to Cold)\n", primary_colortable_mode);
	if ((name = mbdefaults_shade_name(primary_shade_mode)) != NULL)
		mb_gmt_text_put(T, "mbview primary shade mode:         %d  (%s)\n", primary_shade_mode, name);
	if ((name = mbdefaults_colortable_name(slope_colortable)) != NULL)
		mb_gmt_text_put(T, "mbview slope colortable:           %d  (%s)\n", slope_colortable, name);
	if (slope_colortable_mode == MBV_COLORTABLE_NORMAL)
		mb_gmt_text_put(T, "mbview slope colortable mode:      %d  (Normal: Cold to Hot)\n", slope_colortable_mode);
	else
		mb_gmt_text_put(T, "mbview slope colortable mode:      %d  (Reversed: Hot to Cold)\n", slope_colortable_mode);
	if ((name = mbdefaults_colortable_name(secondary_colortable)) != NULL)
		mb_gmt_text_put(T, "mbview overlay colortable:         %d  (%s)\n", secondary_colortable, name);
	if (secondary_colortable_mode == MBV_COLORTABLE_NORMAL)
		mb_gmt_text_put(T, "mbview overlay colortable mode:    %d  (Normal: Cold to Hot)\n", secondary_colortable_mode);
	else
		mb_gmt_text_put(T, "mbview overlay colortable mode:    %d  (Reversed: Hot to Cold)\n", secondary_colortable_mode);
	mb_gmt_text_put(T, "mbview illumination magnitude:     %f\n", illuminate_magnitude);
	mb_gmt_text_put(T, "mbview illumination elevation:     %f degrees\n", illuminate_elevation);
	mb_gmt_text_put(T, "mbview illumination azimuth:       %f degrees\n", illuminate_azimuth);
	mb_gmt_text_put(T, "mbview slope magnitude:            %f\n", slope_magnitude);

	if (mb_gmt_text_end(T))
		Return(API->error);

	GMT_Report(API, GMT_MSG_DEBUG, "Program <%s> completed, status %d\n", program_name, status);

	Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
