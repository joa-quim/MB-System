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
 * GMT-module port of src/utilities/mbdefaults.cc. The program's getopt_long() option loop
 * is kept as it is, running on the reentrant mb_getopt_long() (the state
 * lives in a local structure, so the module can run any number of times in
 * one GMT session), and main() becomes GMT_mbdefaults(), with every exit()
 * turned into Return().
 */

#define THIS_MODULE_NAME "mbdefaults"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Set and list the default MBIO control parameters in ~/.mbio_defaults"
/* No data input; the current or new defaults are listed on stdout. */
#define THIS_MODULE_KEYS ">D}"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "unistd_w.h"
#else
#include <unistd.h>
#endif
#include "mb_define.h"
#include "mb_status.h"

#include "mb_getopt.h"

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
static const char usage_message[] =
    "mbdefaults\n"
    "\t--fbt-version=fbtversion {-Ffbtversion}\n"
    "\t--file-io-buffer=fileiobuffer {-Bfileiobuffer}\n"
    "\t--help {-H}\n"
    "\t--image-display=imagedisplay {-Iimagedisplay}\n"
    "\t--lonflip=lonflip {-Llonflip}\n"
    "\t--mbview-settings=mbviewsettings {-Mmbviewsettings}\n"
    "\t--project=mbproject {-Wmbproject}\n"
    "\t--ps-display=psdisplay {-Dpsdisplay}\n"
    "\t--time-gap=timegap {-Ttimegap}\n"
    "\t--use-lock-files=yes|no {-Uyes|no}\n"
    "\t--verbose {-V}\n\n";

/*--------------------------------------------------------------------*/


/* --- GMT front end ---------------------------------------------------- */

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_PARSE_ERROR;
	GMT_Message(API, GMT_TIME_NONE, "%s\n", help_message);
	return GMT_PARSE_ERROR;
}

/* The options GMT itself should see: -V (verbosity) and -I (the input the
 * module keys bind). Everything else, long options included, is parsed by
 * the program's own option loop below. */
static char *mb_gmt_options_string(int argc, char **argv) {
	size_t total = 1;
	for (int i = 1; i < argc; i++)
		total += strlen(argv[i]) + 1;
	char *s = (char *)calloc(total + 8, 1);
	if (s == NULL)
		return NULL;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && (argv[i][1] == 'V' || (argv[i][1] == 'I' && argv[i][2] != '\0'))) {
			if (s[0] != '\0')
				strcat(s, " ");
			strcat(s, argv[i]);
		}
	}
	return s;
}

/* gmt_M_free_options() hard-codes a variable named "options", which the
   program's own option table shadows here, so destroy gmt_options directly */
#define bailout(code) { mb_getopt_args_free(argc, argv); free(gmt_args); GMT_Destroy_Options(API, &gmt_options); return (code); }
#define Return(code) { gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbdefaults(void *V_API, int gmt_mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbdefaults(void *V_API, int gmt_mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *gmt_options = NULL;
	char *gmt_args = NULL;
	char **argv = NULL;
	int argc = 0;
	struct mb_getopt_state getopt_state;
	mb_getopt_init(&getopt_state);

	if (!API) return GMT_NOT_A_SESSION;
	if (gmt_mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);

	/* the program's own argv[], whatever shape GMT handed us */
	argc = mb_getopt_args_build(THIS_MODULE_NAME, gmt_mode, args, &argv);
	if (argc == 2 && (strcmp(argv[1], "-") == 0 || strcmp(argv[1], "?") == 0))
		bailout(usage(API, GMT_USAGE));
	if (argc == 2 && strcmp(argv[1], "+") == 0)
		bailout(usage(API, GMT_SYNOPSIS));

	gmt_args = mb_gmt_options_string(argc, argv);
	gmt_options = GMT_Create_Options(API, GMT_MODULE_CMD, (gmt_args != NULL && gmt_args[0] != '\0') ? gmt_args : NULL);
	if (API->error) bailout(API->error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, NULL, &gmt_options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, gmt_options)) Return(API->error);

	int verbose = 0;
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

	bool flag = false;

	{
		static struct mb_getopt_option options[] = {
			{"file-io-buffer", mb_required_argument, NULL, 0},
			{"ps-display", mb_required_argument, NULL, 0},
			{"fbt-version", mb_required_argument, NULL, 0},
			{"help", mb_no_argument, NULL, 0},
			{"image-display", mb_required_argument, NULL, 0},
			{"lonflip", mb_required_argument, NULL, 0},
			{"mbview-settings", mb_required_argument, NULL, 0},
			{"time-gap", mb_required_argument, NULL, 0},
			{"use-lock-files", mb_required_argument, NULL, 0},
			{"verbose", mb_no_argument, NULL, 0},
			{"project", mb_required_argument, NULL, 0},
			{NULL, 0, NULL, 0}
		};

		bool errflg = false;
		bool help = false;
		int c;
		int option_index;
		while ((c = mb_getopt_long(&getopt_state, argc, argv, "B:b:D:d:F:f:HhI:i:L:l:M:m:T:t:U:u:VvW:w:", options, &option_index)) != -1)
		{
			switch (c) {
			case 0:
				if (strcmp("file-io-buffer", options[option_index].name) == 0) {
					sscanf(getopt_state.optarg, "%d", &fileiobuffer);
					flag = true;
				}
				else if (strcmp("ps-display", options[option_index].name) == 0) {
					sscanf(getopt_state.optarg, "%1023s", psdisplay);
					flag = true;
				}
				else if (strcmp("fbt-version", options[option_index].name) == 0) {
					char argstring[MB_PATH_MAXLINE];
					sscanf(getopt_state.optarg, "%1023s", argstring);
					if (strncmp(argstring, "new", 3) == 0 || strncmp(argstring, "NEW", 3) == 0)
						fbtversion = 3;
					else if (strncmp(argstring, "old", 2) == 0 || strncmp(argstring, "OLD", 2) == 0)
						fbtversion = 2;
					else if (strncmp(argstring, "2", 1) == 0)
						fbtversion = 2;
					else if (strncmp(argstring, "3", 1) == 0)
						fbtversion = 3;
					flag = true;
				}
				else if (strcmp("help", options[option_index].name) == 0) {
					help = true;
				}
				else if (strcmp("image-display", options[option_index].name) == 0) {
					sscanf(getopt_state.optarg, "%1023s", imgdisplay);
					flag = true;
				}
				else if (strcmp("lonflip", options[option_index].name) == 0) {
					sscanf(getopt_state.optarg, "%d", &lonflip);
					flag = true;
				}
				else if (strcmp("mbview-settings", options[option_index].name) == 0) {
					/* default primary colortable and modes */
					if (getopt_state.optarg[0] == 'P' || getopt_state.optarg[0] == 'p') {
						int tmp;
						/* n = */ sscanf(&getopt_state.optarg[1], "%d/%d/%d", &primary_colortable, &tmp, &primary_shade_mode);
						primary_colortable_mode = (colortable_mode_t)tmp;
					} else if (getopt_state.optarg[0] == 'G' || getopt_state.optarg[0] == 'g') {
						/* default slope colortable and mode */
						/* n = */ sscanf(&getopt_state.optarg[1], "%d/%d", &slope_colortable, &slope_colortable_mode);
					} else if (getopt_state.optarg[0] == 'O' || getopt_state.optarg[0] == 'o') {
						/* default overlay colortable and mode */
						int tmp;
						/* n = */ sscanf(&getopt_state.optarg[1], "%d/%d", &secondary_colortable, &tmp);
						secondary_colortable_mode = (colortable_mode_t)tmp;
					} else if (getopt_state.optarg[0] == 'I' || getopt_state.optarg[0] == 'i') {
						/* default illumination parameters */
						/* n = */ sscanf(&getopt_state.optarg[1], "%lf/%lf/%lf", &illuminate_magnitude, &illuminate_elevation, &illuminate_azimuth);
					} else if (getopt_state.optarg[0] == 'S' || getopt_state.optarg[0] == 's') {
						/* default slope shading magnitude */
						/* n = */ sscanf(&getopt_state.optarg[1], "%lf", &slope_magnitude);
					}

					flag = true;
				}
				else if (strcmp("time-gap", options[option_index].name) == 0) {
					sscanf(getopt_state.optarg, "%lf", &timegap);
					flag = true;
				}
				else if (strcmp("use-lock-files", options[option_index].name) == 0) {
					char argstring[MB_PATH_MAXLINE];
					sscanf(getopt_state.optarg, "%1023s", argstring);
					if (strncmp(argstring, "yes", 3) == 0 || strncmp(argstring, "YES", 3) == 0)
						uselockfiles = true;
					else if (strncmp(argstring, "no", 2) == 0 || strncmp(argstring, "NO", 2) == 0)
						uselockfiles = false;
					else if (strncmp(argstring, "1", 1) == 0)
						uselockfiles = true;
					else if (strncmp(argstring, "0", 1) == 0)
						uselockfiles = false;
					flag = true;
				}
				else if (strcmp("verbose", options[option_index].name) == 0) {
					verbose++;
				}
				else if (strcmp("project", options[option_index].name) == 0) {
					sscanf(getopt_state.optarg, "%1023s", mbproject);
					flag = true;
				}
				break;
			case 'B':
			case 'b':
				sscanf(getopt_state.optarg, "%d", &fileiobuffer);
				flag = true;
				break;
			case 'D':
			case 'd':
				sscanf(getopt_state.optarg, "%1023s", psdisplay);
				flag = true;
				break;
			case 'F':
			case 'f':
			{
				char argstring[MB_PATH_MAXLINE];
				sscanf(getopt_state.optarg, "%1023s", argstring);
				if (strncmp(argstring, "new", 3) == 0 || strncmp(argstring, "NEW", 3) == 0)
					fbtversion = 3;
				else if (strncmp(argstring, "old", 2) == 0 || strncmp(argstring, "OLD", 2) == 0)
					fbtversion = 2;
				else if (strncmp(argstring, "2", 1) == 0)
					fbtversion = 2;
				else if (strncmp(argstring, "3", 1) == 0)
					fbtversion = 3;
				flag = true;
				break;
			}
			case 'I':
			case 'i':
				sscanf(getopt_state.optarg, "%1023s", imgdisplay);
				flag = true;
				break;
			case 'H':
			case 'h':
				help = true;
				break;
			case 'L':
			case 'l':
				sscanf(getopt_state.optarg, "%d", &lonflip);
				flag = true;
				break;
			case 'M':
			case 'm':
			{
				/* default primary colortable and modes */
				if (getopt_state.optarg[0] == 'P' || getopt_state.optarg[0] == 'p') {
					int tmp;
					/* n = */ sscanf(&getopt_state.optarg[1], "%d/%d/%d", &primary_colortable, &tmp, &primary_shade_mode);
					primary_colortable_mode = (colortable_mode_t)tmp;
				} else if (getopt_state.optarg[0] == 'G' || getopt_state.optarg[0] == 'g') {
					/* default slope colortable and mode */
					/* n = */ sscanf(&getopt_state.optarg[1], "%d/%d", &slope_colortable, &slope_colortable_mode);
				} else if (getopt_state.optarg[0] == 'O' || getopt_state.optarg[0] == 'o') {
					/* default overlay colortable and mode */
					int tmp;
					/* n = */ sscanf(&getopt_state.optarg[1], "%d/%d", &secondary_colortable, &tmp);
					secondary_colortable_mode = (colortable_mode_t)tmp;
				} else if (getopt_state.optarg[0] == 'I' || getopt_state.optarg[0] == 'i') {
					/* default illumination parameters */
					/* n = */ sscanf(&getopt_state.optarg[1], "%lf/%lf/%lf", &illuminate_magnitude, &illuminate_elevation, &illuminate_azimuth);
				} else if (getopt_state.optarg[0] == 'S' || getopt_state.optarg[0] == 's') {
					/* default slope shading magnitude */
					/* n = */ sscanf(&getopt_state.optarg[1], "%lf", &slope_magnitude);
				}

				flag = true;
				break;
			}
			case 'T':
			case 't':
				sscanf(getopt_state.optarg, "%lf", &timegap);
				flag = true;
				break;
			case 'U':
			case 'u':
			{
				char argstring[MB_PATH_MAXLINE];
				sscanf(getopt_state.optarg, "%1023s", argstring);
				if (strncmp(argstring, "yes", 3) == 0 || strncmp(argstring, "YES", 3) == 0)
					uselockfiles = true;
				else if (strncmp(argstring, "no", 2) == 0 || strncmp(argstring, "NO", 2) == 0)
					uselockfiles = false;
				else if (strncmp(argstring, "1", 1) == 0)
					uselockfiles = true;
				else if (strncmp(argstring, "0", 1) == 0)
					uselockfiles = false;
				flag = true;
				break;
			}
			case 'V':
			case 'v':
				verbose++;
				break;
			case 'W':
			case 'w':
				sscanf(getopt_state.optarg, "%1023s", mbproject);
				flag = true;
				break;
			case '?':
				errflg = true;
			}
		}

		if (errflg) {
			fprintf(stderr, "usage: %s\n", usage_message);
			Return(MB_ERROR_BAD_USAGE);
		}

		if (verbose == 1 || help) {
			fprintf(stderr, "\nProgram %s\n", program_name);
			fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
		}

		if (verbose >= 2) {
			fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
			fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(stderr, "dbg2  Control Parameters:\n");
			fprintf(stderr, "dbg2       verbose:                    %d\n", verbose);
			fprintf(stderr, "dbg2       help:                       %d\n", help);
			fprintf(stderr, "dbg2       format:                     %d\n", format);
			fprintf(stderr, "dbg2       pings:                      %d\n", pings);
			fprintf(stderr, "dbg2       lonflip:                    %d\n", lonflip);
			fprintf(stderr, "dbg2       bounds[0]:                  %f\n", bounds[0]);
			fprintf(stderr, "dbg2       bounds[1]:                  %f\n", bounds[1]);
			fprintf(stderr, "dbg2       bounds[2]:                  %f\n", bounds[2]);
			fprintf(stderr, "dbg2       bounds[3]:                  %f\n", bounds[3]);
			fprintf(stderr, "dbg2       btime_i[0]:                 %d\n", btime_i[0]);
			fprintf(stderr, "dbg2       btime_i[1]:                 %d\n", btime_i[1]);
			fprintf(stderr, "dbg2       btime_i[2]:                 %d\n", btime_i[2]);
			fprintf(stderr, "dbg2       btime_i[3]:                 %d\n", btime_i[3]);
			fprintf(stderr, "dbg2       btime_i[4]:                 %d\n", btime_i[4]);
			fprintf(stderr, "dbg2       btime_i[5]:                 %d\n", btime_i[5]);
			fprintf(stderr, "dbg2       btime_i[6]:                 %d\n", btime_i[6]);
			fprintf(stderr, "dbg2       etime_i[0]:                 %d\n", etime_i[0]);
			fprintf(stderr, "dbg2       etime_i[1]:                 %d\n", etime_i[1]);
			fprintf(stderr, "dbg2       etime_i[2]:                 %d\n", etime_i[2]);
			fprintf(stderr, "dbg2       etime_i[3]:                 %d\n", etime_i[3]);
			fprintf(stderr, "dbg2       etime_i[4]:                 %d\n", etime_i[4]);
			fprintf(stderr, "dbg2       etime_i[5]:                 %d\n", etime_i[5]);
			fprintf(stderr, "dbg2       etime_i[6]:                 %d\n", etime_i[6]);
			fprintf(stderr, "dbg2       speedmin:                   %f\n", speedmin);
			fprintf(stderr, "dbg2       timegap:                    %f\n", timegap);
			fprintf(stderr, "dbg2       psdisplay:                  %s\n", psdisplay);
			fprintf(stderr, "dbg2       imgdisplay:                 %s\n", imgdisplay);
			fprintf(stderr, "dbg2       mbproject:                  %s\n", mbproject);
			fprintf(stderr, "dbg2       fbtversion:                 %d\n", fbtversion);
			fprintf(stderr, "dbg2       uselockfiles:               %d\n", uselockfiles);
			fprintf(stderr, "dbg2       fileiobuffer:               %d\n", fileiobuffer);
			fprintf(stderr, "dbg2       primary_colortable:         %d\n", primary_colortable);
			fprintf(stderr, "dbg2       primary_colortable_mode:    %d\n", primary_colortable_mode);
			fprintf(stderr, "dbg2       primary_shade_mode:         %d\n", primary_shade_mode);
			fprintf(stderr, "dbg2       slope_colortable:           %d\n", slope_colortable);
			fprintf(stderr, "dbg2       slope_colortable_mode:      %d\n", slope_colortable_mode);
			fprintf(stderr, "dbg2       secondary_colortable:       %d\n", secondary_colortable);
			fprintf(stderr, "dbg2       secondary_colortable_mode:  %d\n", secondary_colortable_mode);
			fprintf(stderr, "dbg2       illuminate_magnitude:       %f\n", illuminate_magnitude);
			fprintf(stderr, "dbg2       illuminate_elevation:       %f\n", illuminate_elevation);
			fprintf(stderr, "dbg2       illuminate_azimuth:         %f\n", illuminate_azimuth);
			fprintf(stderr, "dbg2       slope_magnitude:            %f\n", slope_magnitude);
		}

		if (help) {
			fprintf(stderr, "\n%s\n", help_message);
			fprintf(stderr, "\nusage: %s\n", usage_message);
			Return(MB_ERROR_NO_ERROR);
		}
	}

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
			fprintf(stderr, "Could not determine home directory (HOME environment variable not set)\n");
			Return(MB_ERROR_OPEN_FAIL);
		}
		char file[MB_PATH_MAXLINE];
		snprintf(file, sizeof(file), "%s/.mbio_defaults", home);
		FILE *fp = fopen(file, "w");
		if (fp == NULL) {
			fprintf(stderr, "Could not open file %s\n", file);
			Return(MB_ERROR_OPEN_FAIL);
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

		printf("\nNew MBIO Default Control Parameters:\n");
		printf("lonflip:    %d\n", lonflip);
		printf("timegap:    %f\n", timegap);
		printf("ps viewer:  %s\n", psdisplay);
		printf("img viewer: %s\n", imgdisplay);
		printf("project:    %s\n", mbproject);
		if (fbtversion == 2)
			printf("fbtversion: 2 (old)\n");
		else if (fbtversion == 3)
			printf("fbtversion: 3 (new)\n");
		else
			printf("fbtversion: %d\n", fbtversion);
		printf("uselockfiles: %d\n", uselockfiles);
		if (fileiobuffer == 0)
			printf("fileiobuffer: %d (use standard fread() & fwrite() buffering)\n", fileiobuffer);
		else if (fileiobuffer > 0)
			printf("fileiobuffer: %d (use %d kB buffer for fread() & fwrite())\n", fileiobuffer, fileiobuffer);
		else
			printf("fileiobuffer: %d (use mmap for file i/o)\n", fileiobuffer);
		if (primary_colortable == MBV_COLORTABLE_HAXBY)
			printf("mbview primary colortable:    %d  (Haxby)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_BRIGHT)
			printf("mbview primary colortable:    %d  (Bright)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_MUTED)
			printf("mbview primary colortable:    %d  (Muted)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_GRAY)
			printf("mbview primary colortable:    %d  (Grayscale)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_FLAT)
			printf("mbview primary colortable:    %d  (Flat  gray)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_SEALEVEL1)
			printf("mbview primary colortable:    %d  (Sealevel 1)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_SEALEVEL2)
			printf("mbview primary colortable:    %d  (Sealevel 2)\n", primary_colortable);
		if (primary_colortable_mode == MBV_COLORTABLE_NORMAL)
			printf("mbview primary colortable mode:    %d  (Normal: Cold to Hot)\n", primary_colortable_mode);
		else
			printf("mbview primary colortable mode:    %d  (Reversed: Hot to Cold)\n", primary_colortable_mode);
		if (primary_shade_mode == MBV_SHADE_VIEW_NONE)
			printf("mbview primary shade mode:    %d  (No shading)\n", primary_shade_mode);
		else if (primary_shade_mode == MBV_SHADE_VIEW_ILLUMINATION)
			printf("mbview primary shade mode:    %d  (Shading by illumination)\n", primary_shade_mode);
		else if (primary_shade_mode == MBV_SHADE_VIEW_SLOPE)
			printf("mbview primary shade mode:    %d  (Shading by slope magnitude)\n", primary_shade_mode);
		else if (primary_shade_mode == MBV_SHADE_VIEW_OVERLAY)
			printf("mbview primary shade mode:    %d  (Shading by overlay)\n", primary_shade_mode);

		if (slope_colortable == MBV_COLORTABLE_HAXBY)
			printf("mbview slope colortable:    %d  (Haxby)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_BRIGHT)
			printf("mbview slope colortable:    %d  (Bright)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_MUTED)
			printf("mbview slope colortable:    %d  (Muted)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_GRAY)
			printf("mbview slope colortable:    %d  (Grayscale)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_FLAT)
			printf("mbview slope colortable:    %d  (Flat  gray)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_SEALEVEL1)
			printf("mbview slope colortable:    %d  (Sealevel 1)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_SEALEVEL2)
			printf("mbview slope colortable:    %d  (Sealevel 2)\n", slope_colortable);
		if (slope_colortable_mode == MBV_COLORTABLE_NORMAL)
			printf("mbview slope colortable mode:    %d  (Normal: Cold to Hot)\n", slope_colortable_mode);
		else
			printf("mbview slope colortable mode:    %d  (Reversed: Hot to Cold)\n", slope_colortable_mode);

		if (secondary_colortable == MBV_COLORTABLE_HAXBY)
			printf("mbview overlay colortable:    %d  (Haxby)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_BRIGHT)
			printf("mbview overlay colortable:    %d  (Bright)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_MUTED)
			printf("mbview overlay colortable:    %d  (Muted)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_GRAY)
			printf("mbview overlay colortable:    %d  (Grayscale)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_FLAT)
			printf("mbview overlay colortable:    %d  (Flat  gray)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_SEALEVEL1)
			printf("mbview overlay colortable:    %d  (Sealevel 1)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_SEALEVEL2)
			printf("mbview overlay colortable:    %d  (Sealevel 2)\n", secondary_colortable);
		if (secondary_colortable_mode == MBV_COLORTABLE_NORMAL)
			printf("mbview overlay colortable mode:    %d  (Normal: Cold to Hot)\n", secondary_colortable_mode);
		else
			printf("mbview overlay colortable mode:    %d  (Reversed: Hot to Cold)\n", secondary_colortable_mode);
		printf("mbview illumination magnitude:    %f\n", illuminate_magnitude);
		printf("mbview illumination elevation:    %f degrees\n", illuminate_elevation);
		printf("mbview illumination azimuth:      %f degrees\n", illuminate_azimuth);
		printf("mbview slope magnitude:           %f\n", slope_magnitude);
	} else {
		/* else just list the current defaults */

		printf("\nCurrent MBIO Default Control Parameters:\n");
		printf("lonflip:    %d\n", lonflip);
		printf("timegap:    %f\n", timegap);
		printf("ps viewer:  %s\n", psdisplay);
		printf("img viewer: %s\n", imgdisplay);
		printf("project:    %s\n", mbproject);
		if (fbtversion == 2)
			printf("fbtversion: 2 (old)\n");
		else if (fbtversion == 3)
			printf("fbtversion: 3 (new)\n");
		else
			printf("fbtversion: %d\n", fbtversion);
		printf("uselockfiles: %d\n", uselockfiles);
		if (fileiobuffer == 0)
			printf("fileiobuffer: %d (use standard fread() & fwrite() buffering)\n", fileiobuffer);
		else if (fileiobuffer > 0)
			printf("fileiobuffer: %d (use %d kB buffer for fread() & fwrite())\n", fileiobuffer, fileiobuffer);
		else
			printf("fileiobuffer: %d (use mmap for file i/o)\n", fileiobuffer);
		if (primary_colortable == MBV_COLORTABLE_HAXBY)
			printf("mbview primary colortable:         %d  (Haxby)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_BRIGHT)
			printf("mbview primary colortable:         %d  (Bright)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_MUTED)
			printf("mbview primary colortable:         %d  (Muted)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_GRAY)
			printf("mbview primary colortable:         %d  (Grayscale)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_FLAT)
			printf("mbview primary colortable:         %d  (Flat  gray)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_SEALEVEL1)
			printf("mbview primary colortable:         %d  (Sealevel 1)\n", primary_colortable);
		else if (primary_colortable == MBV_COLORTABLE_SEALEVEL2)
			printf("mbview primary colortable:         %d  (Sealevel 2)\n", primary_colortable);
		if (primary_colortable_mode == MBV_COLORTABLE_NORMAL)
			printf("mbview primary colortable mode:    %d  (Normal: Cold to Hot)\n", primary_colortable_mode);
		else
			printf("mbview primary colortable mode:    %d  (Reversed: Hot to Cold)\n", primary_colortable_mode);
		if (primary_shade_mode == MBV_SHADE_VIEW_NONE)
			printf("mbview primary shade mode:         %d  (No shading)\n", primary_shade_mode);
		else if (primary_shade_mode == MBV_SHADE_VIEW_ILLUMINATION)
			printf("mbview primary shade mode:         %d  (Shading by illumination)\n", primary_shade_mode);
		else if (primary_shade_mode == MBV_SHADE_VIEW_SLOPE)
			printf("mbview primary shade mode:         %d  (Shading by slope magnitude)\n", primary_shade_mode);
		else if (primary_shade_mode == MBV_SHADE_VIEW_OVERLAY)
			printf("mbview primary shade mode:         %d  (Shading by overlay)\n", primary_shade_mode);

		if (slope_colortable == MBV_COLORTABLE_HAXBY)
			printf("mbview slope colortable:           %d  (Haxby)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_BRIGHT)
			printf("mbview slope colortable:           %d  (Bright)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_MUTED)
			printf("mbview slope colortable:           %d  (Muted)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_GRAY)
			printf("mbview slope colortable:           %d  (Grayscale)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_FLAT)
			printf("mbview slope colortable:           %d  (Flat  gray)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_SEALEVEL1)
			printf("mbview slope colortable:           %d  (Sealevel 1)\n", slope_colortable);
		else if (slope_colortable == MBV_COLORTABLE_SEALEVEL2)
			printf("mbview slope colortable:           %d  (Sealevel 2)\n", slope_colortable);
		if (slope_colortable_mode == MBV_COLORTABLE_NORMAL)
			printf("mbview slope colortable mode:      %d  (Normal: Cold to Hot)\n", slope_colortable_mode);
		else
			printf("mbview slope colortable mode:      %d  (Reversed: Hot to Cold)\n", slope_colortable_mode);

		if (secondary_colortable == MBV_COLORTABLE_HAXBY)
			printf("mbview overlay colortable:         %d  (Haxby)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_BRIGHT)
			printf("mbview overlay colortable:         %d  (Bright)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_MUTED)
			printf("mbview overlay colortable:         %d  (Muted)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_GRAY)
			printf("mbview overlay colortable:         %d  (Grayscale)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_FLAT)
			printf("mbview overlay colortable:         %d  (Flat  gray)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_SEALEVEL1)
			printf("mbview overlay colortable:         %d  (Sealevel 1)\n", secondary_colortable);
		else if (secondary_colortable == MBV_COLORTABLE_SEALEVEL2)
			printf("mbview overlay colortable:         %d  (Sealevel 2)\n", secondary_colortable);
		if (secondary_colortable_mode == MBV_COLORTABLE_NORMAL)
			printf("mbview overlay colortable mode:    %d  (Normal: Cold to Hot)\n", secondary_colortable_mode);
		else
			printf("mbview overlay colortable mode:    %d  (Reversed: Hot to Cold)\n", secondary_colortable_mode);
		printf("mbview illumination magnitude:     %f\n", illuminate_magnitude);
		printf("mbview illumination elevation:     %f degrees\n", illuminate_elevation);
		printf("mbview illumination azimuth:       %f degrees\n", illuminate_azimuth);
		printf("mbview slope magnitude:            %f\n", slope_magnitude);
	}

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
		fprintf(stderr, "dbg2  Ending status:\n");
		fprintf(stderr, "dbg2       status:  %d\n", status);
	}

	Return(MB_ERROR_NO_ERROR);
}
/*--------------------------------------------------------------------*/
