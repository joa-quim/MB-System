/*--------------------------------------------------------------------
 *    The MB-system:  mbminirovnav.c  9/7/2017
 *
 *    Copyright (c) 2017-2025 by
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
 * MBminirov reads USBL tracking and CTD day files from the MBARI MiniROV
 * and produces a single ROV navigation file in one of the standard MBARI
 * formats.
 *
 * This program replaces the several format-specific preprocessing programs
 * found in MB-System version 5 releases with a single program for version 6.
 *
 * Author:  D. W. Caress
 * Date:  7 September, 2017
 */
/*
 * GMT-module port of src/utilities/mbminirovnav.cc. The program's getopt_long() option loop
 * is kept as it is, running on the reentrant mb_getopt_long() (the state
 * lives in a local structure, so the module can run any number of times in
 * one GMT session), and main() becomes GMT_mbminirovnav(), with every exit()
 * turned into Return().
 */

#define THIS_MODULE_NAME "mbminirovnav"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Merge MBARI MiniROV USBL tracking and CTD day files into ROV navigation"
/* The input day files are named by long options; output files are written by the module itself. */
#define THIS_MODULE_KEYS "ID("
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include "unistd_w.h"
#else
#include <unistd.h>
#endif
#include "mb_aux.h"
#include "mb_define.h"
#include "mb_format.h"
#include "mb_io.h"
#include "mb_process.h"
#include "mb_status.h"

#include "mb_getopt.h"

static const char program_name[] = "mbminirovnav";
static const char help_message[] =
    " MBminirov reads USBL tracking and CTD day files from the MBARI MiniROV\n"
    "\tand produces a single ROV navigation file in one of the standard MBARI\n"
    "\tformats handles preprocessing of swath sonar data as part of setting up\n"
    "\tan MB-System processing structure for a dataset.\n";
static const char usage_message[] =
    "mbminirovnav\n"
    "\t--help\n\n"
    "\t--input=fileroot\n"
    "\t--input-ctd-file=file\n"
    "\t--input-dvl-file=file\n"
    "\t--input-nav-file=file\n"
    "\t--input-rov-file=file\n"
    "\t--interpolate-position\n"
    "\t--interval=seconds\n"
    "\t--output=file\n"
    "\t--rov-dive-start=yyyymmddhhmmss\n"
    "\t--rov-dive-end=yyyymmddhhmmss\n"
    "\t--utm-zone=zone_id/NorS\n"
    "\t--verbose\n\n";

// Return the number of lines that don't start with # and are at least 5 char long.

// Rewinds the file before returning.
static int GetNumRecords(FILE *fp) {
  int nrecord = 0;

  char buffer[MB_PATH_MAXLINE];
  char *result = NULL;
  while ((result = fgets(buffer, (MB_PATH_MAXLINE - 1), fp)) == buffer)
    if (buffer[0] != '#' && strlen(buffer) > 5)
      nrecord++;

  rewind(fp);

  return nrecord;
}

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
EXTERN_MSC int GMT_mbminirovnav(void *V_API, int gmt_mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbminirovnav(void *V_API, int gmt_mode, void *args) {
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

  /* ROV dive time start and end */
  bool rov_dive_start_time_set = false;
  double rov_dive_start_time_d;
  int rov_dive_start_time_i[7];
  bool rov_dive_end_time_set = false;
  double rov_dive_end_time_d;
  int rov_dive_end_time_i[7];
  bool interpolate_position = false;

  int verbose = 0;

  double interval = 1.0;
  bool utm_zone_set = false;
  int utm_zone = 0;
  char NorS;
  char EorW;
  mb_path projection_id;
  mb_path input_nav_file = "";
  mb_path input_ctd_file = "";
  mb_path input_dvl_file = "";
  mb_path input_rov_file = "";
  mb_path output_file = "";

  {
    static struct mb_getopt_option options[] = {
      {"help", mb_no_argument, NULL, 0},
      {"input", mb_required_argument, NULL, 0},
      {"input-nav-file", mb_required_argument, NULL, 0},
      {"input-ctd-file", mb_required_argument, NULL, 0},
      {"input-dvl-file", mb_required_argument, NULL, 0},
      {"input-rov-file", mb_required_argument, NULL, 0},
      {"interpolate-position", mb_no_argument, NULL, 0},
      {"interval", mb_required_argument, NULL, 0},
      {"output", mb_required_argument, NULL, 0},
      {"rov-dive-start", mb_required_argument, NULL, 0},
      {"rov-dive-end", mb_required_argument, NULL, 0},
      {"utm-zone", mb_required_argument, NULL, 0},
      {"verbose", mb_no_argument, NULL, 0},
      {NULL, 0, NULL, 0}};

    int option_index;
    bool errflg = false;
    int c;
    bool help = false;
    while ((c = mb_getopt_long(&getopt_state, argc, argv, "", options, &option_index)) != -1)
    {
      switch (c) {
        /* long options all return c=0 */
        case 0:
          if (strcmp("verbose", options[option_index].name) == 0) {
            verbose++;
          } else if (strcmp("help", options[option_index].name) == 0) {
            help = true;
          }
          // Define input and output files
          else if (strcmp("input-nav-file", options[option_index].name) == 0) {
            snprintf(input_nav_file, sizeof(mb_path), "%s", getopt_state.optarg);
          } else if (strcmp("input-rov-file", options[option_index].name) == 0) {
            snprintf(input_rov_file, sizeof(mb_path), "%s", getopt_state.optarg);
          } else if (strcmp("input-ctd-file", options[option_index].name) == 0) {
            snprintf(input_ctd_file, sizeof(mb_path), "%s", getopt_state.optarg);
          } else if (strcmp("input-dvl-file", options[option_index].name) == 0) {
            snprintf(input_dvl_file, sizeof(mb_path), "%s", getopt_state.optarg);
          } else if (strcmp("output", options[option_index].name) == 0) {
            snprintf(output_file, sizeof(mb_path), "%s", getopt_state.optarg);
          } else if (strcmp("interval", options[option_index].name) == 0) {
            /* const int nscan = */ sscanf(getopt_state.optarg, "%lf", &interval);
            if (interval <= 0.0) {
              fprintf(stderr,"Program %s command error: %s %s\n\toutput interval reset to 1.0 seconds\n",
                      program_name, options[option_index].name, getopt_state.optarg);
            }
          } else if (strcmp("rov-dive-start", options[option_index].name) == 0) {
            const int nscan = sscanf(getopt_state.optarg, "%d/%d/%d/%d/%d/%d", &rov_dive_start_time_i[0],
                           &rov_dive_start_time_i[1], &rov_dive_start_time_i[2],
                           &rov_dive_start_time_i[3], &rov_dive_start_time_i[4],
                           &rov_dive_start_time_i[5]);
            if (nscan == 6) {
              rov_dive_start_time_i[6] = 0;
              mb_get_time(verbose, rov_dive_start_time_i, &rov_dive_start_time_d);
              rov_dive_start_time_set = true;
            } else {
              fprintf(stderr,"Program %s command error: %s %s\n",
                      program_name, options[option_index].name, getopt_state.optarg);
            }
          } else if (strcmp("rov-dive-end", options[option_index].name) == 0) {
            const int nscan = sscanf(getopt_state.optarg, "%d/%d/%d/%d/%d/%d", &rov_dive_end_time_i[0],
                           &rov_dive_end_time_i[1], &rov_dive_end_time_i[2],
                           &rov_dive_end_time_i[3], &rov_dive_end_time_i[4],
                           &rov_dive_end_time_i[5]);
            if (nscan == 6) {
              rov_dive_end_time_i[6] = 0;
              mb_get_time(verbose, rov_dive_end_time_i, &rov_dive_end_time_d);
              rov_dive_end_time_set = true;
            } else {
              fprintf(stderr,"Program %s command error: %s %s\n",
                      program_name, options[option_index].name, getopt_state.optarg);
            }
          } else if (strcmp("utm-zone", options[option_index].name) == 0) {
            int nscan = sscanf(getopt_state.optarg, "%d/%c", &utm_zone, &NorS);
            if (nscan < 2)
              nscan = sscanf(getopt_state.optarg, "%d%c", &utm_zone, &NorS);
            if (nscan == 2) {
              utm_zone_set = true;
              if (NorS == 'N' || NorS == 'n')
                snprintf(projection_id, sizeof(projection_id), "UTM%2.2dN", utm_zone);
              else if (NorS == 'S' || NorS == 's')
                snprintf(projection_id, sizeof(projection_id), "UTM%2.2dS", utm_zone);
              else
                snprintf(projection_id, sizeof(projection_id), "UTM%2.2dN", utm_zone);
            } else {
              fprintf(stderr,"Program %s command error: %s %s\n",
                      program_name, options[option_index].name, getopt_state.optarg);
            }
          }
          // Over gaps in USBL fixes (rather than repeat position values)
          else if (strcmp("interpolate-position", options[option_index].name) == 0) {
            interpolate_position = true;
          }
          break;
        case '?':
          errflg = true;
          break;
      }
    }
    if (errflg) {
      fprintf(stderr, "usage: %s\n", usage_message);
      fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
      Return(MB_ERROR_BAD_USAGE);
    }

    if (verbose >= 1 || help) {
      fprintf(stderr, "\nProgram %s\n", program_name);
      fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
    }

    if (help) {
      fprintf(stderr, "\n%s\n", help_message);
      fprintf(stderr, "\nusage: %s\n", usage_message);
      Return(MB_ERROR_NO_ERROR);
    }
  }

  if (verbose >= 2) {
    fprintf(stderr, "dbg2  Control Parameters:\n");
    fprintf(stderr, "dbg2       verbose:                      %d\n", verbose);
    fprintf(stderr, "dbg2       input_nav_file:               %s\n", input_nav_file);
    fprintf(stderr, "dbg2       input_ctd_file:               %s\n", input_ctd_file);
    fprintf(stderr, "dbg2       input_dvl_file:               %s\n", input_dvl_file);
    fprintf(stderr, "dbg2       input_rov_file:               %s\n", input_rov_file);
    fprintf(stderr, "dbg2       output_file:                  %s\n", output_file);
    fprintf(stderr, "dbg2       output time interval:         %f\n", interval);
    fprintf(stderr, "dbg2       rov_dive_start_time_set:      %d\n", rov_dive_start_time_set);
    if (rov_dive_start_time_set)
      fprintf(stderr, "dbg2       rov_dive_start_time_i:        %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
          rov_dive_start_time_i[0], rov_dive_start_time_i[1], rov_dive_start_time_i[2],
          rov_dive_start_time_i[3], rov_dive_start_time_i[4], rov_dive_start_time_i[5],
          rov_dive_start_time_i[6]);
    fprintf(stderr, "dbg2       rov_dive_end_time_set:        %d\n", rov_dive_end_time_set);
    if (rov_dive_end_time_set)
      fprintf(stderr, "dbg2       rov_dive_end_time_i:          %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
          rov_dive_end_time_i[0], rov_dive_end_time_i[1], rov_dive_end_time_i[2],
          rov_dive_end_time_i[3], rov_dive_end_time_i[4], rov_dive_end_time_i[5],
          rov_dive_end_time_i[6]);
    fprintf(stderr, "dbg2       utm_zone_set:                 %d\n", utm_zone_set);
    if (utm_zone_set) {
      fprintf(stderr, "dbg2       utm_zone:                     %d\n", utm_zone);
      fprintf(stderr, "dbg2       projection_id:                %s\n", projection_id);
    }
    fprintf(stderr, "dbg2       interpolate_position:         %d\n", interpolate_position);
  }

  /* print starting verbose */
  if (verbose == 1) {
    int error = MB_ERROR_NO_ERROR;
    char user[256], host[256], date[32];
    int status = mb_user_host_date(verbose, user, host, date, &error);
    fprintf(stdout, "Run by user <%s> on cpu <%s> at <%s>\n", user, host, date);
    fprintf(stdout, "Control Parameters:\n");
    fprintf(stdout, "\tverbose:                      %d\n", verbose);
    fprintf(stdout, "\tinput_nav_file:               %s\n", input_nav_file);
    fprintf(stdout, "\tinput_ctd_file:               %s\n", input_ctd_file);
    fprintf(stdout, "\tinput_dvl_file:               %s\n", input_dvl_file);
    fprintf(stdout, "\tinput_rov_file:               %s\n", input_rov_file);
    fprintf(stdout, "\toutput_file:                  %s\n", output_file);
    fprintf(stdout, "\toutput time interval:         %f\n", interval);
    fprintf(stdout, "\trov_dive_start_time_set:      %d\n", rov_dive_start_time_set);
    if (rov_dive_start_time_set)
      fprintf(stdout, "\trov_dive_start_time_i:        %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
          rov_dive_start_time_i[0], rov_dive_start_time_i[1], rov_dive_start_time_i[2],
          rov_dive_start_time_i[3], rov_dive_start_time_i[4], rov_dive_start_time_i[5],
          rov_dive_start_time_i[6]);
    fprintf(stdout, "\trov_dive_end_time_set:        %d\n", rov_dive_end_time_set);
    if (rov_dive_end_time_set)
      fprintf(stdout, "\trov_dive_end_time_i:          %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
          rov_dive_end_time_i[0], rov_dive_end_time_i[1], rov_dive_end_time_i[2],
          rov_dive_end_time_i[3], rov_dive_end_time_i[4], rov_dive_end_time_i[5],
          rov_dive_end_time_i[6]);
    fprintf(stdout, "\tutm_zone_set:                 %d\n", utm_zone_set);
    if (utm_zone_set) {
      fprintf(stdout, "\tutm_zone:                     %d\n", utm_zone);
      fprintf(stdout, "\tprojection_id:                %s\n", projection_id);
    }
    fprintf(stdout, "\tinterpolate_position:         %d\n", interpolate_position);
  }

  /*-------------------------------------------------------------------*/
  /* load input nav data */

  double *nav_time_d = NULL;
  double *nav_lon = NULL;
  double *nav_lat = NULL;
  int num_nav = 0;

  double time_d;
  double start_time_d = 0.0;
  double end_time_d = 0.0;
  double reference_lon = 0.0;
  double reference_lat = 0.0;

  int status = MB_SUCCESS;
  int error = MB_ERROR_NO_ERROR;

  /* count the records */
  FILE *fp = fopen(input_nav_file, "r");
  if (fp != NULL) {
    const int num_nav_alloc = GetNumRecords(fp);
    if (num_nav_alloc) {
      const size_t size = num_nav_alloc * sizeof(double);
      status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&nav_time_d, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&nav_lon, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&nav_lat, &error);
    }

    /* read the records */
    if (status == MB_SUCCESS) {
      /* loop over reading the records */
      char buffer[MB_PATH_MAXLINE];
      char *result = NULL;
      while ((result = fgets(buffer, (MB_PATH_MAXLINE - 1), fp)) == buffer) {
        double rawlat;
        double rawlon;
        double dummydouble;
        mb_path dummystring;
        const int nget = sscanf(buffer, "%lf,$GPGLL,%lf,%c,%lf,%c,%lf,%1023s",
                &time_d, &rawlat, &NorS, &rawlon, &EorW, &dummydouble, dummystring);
        if (nget >= 5) {
          if (start_time_d <= 0.0)
            start_time_d = time_d;
          if (time_d > 0.0 && time_d < start_time_d)
            start_time_d = time_d;
          if (time_d > end_time_d)
            end_time_d = time_d;
          if (num_nav < num_nav_alloc) {
            nav_time_d[num_nav] = time_d;
            double ldegrees = floor(rawlat / 100.0);
            double lminutes = rawlat - ldegrees * 100;
            nav_lat[num_nav] = ldegrees + (lminutes / 60.0);
            if (NorS == 'S' || NorS == 's')
              nav_lat[num_nav] *= -1;
            ldegrees = floor(rawlon / 100.0);
            lminutes = rawlon - ldegrees * 100;
            nav_lon[num_nav] = ldegrees + (lminutes / 60.0);
            if (EorW == 'W' || EorW == 'w')
              nav_lon[num_nav] *= -1;

            if (!interpolate_position
                || num_nav <= 1
                || nav_lon[num_nav] != nav_lon[num_nav-1]
                || nav_lat[num_nav] != nav_lat[num_nav-1]) {
              reference_lon += nav_lon[num_nav];
              reference_lat += nav_lat[num_nav];
              num_nav++;
            }
          }
        }
      }

      /* calculate average longitude for UTM zone calcuation */
      if (num_nav > 0) {
        reference_lon /= num_nav;
        reference_lat /= num_nav;
      }
      if (reference_lon < 180.0)
        reference_lon += 360.0;
      if (reference_lon >= 180.0)
        reference_lon -= 360.0;
    }

    fclose(fp);
  } else {
    if (verbose) {
      fprintf(stderr, "\nUnable to open NAV file: %s\n", input_nav_file);
    }
  }

  /*-------------------------------------------------------------------*/
  /* load input ctd data */

  int num_ctd = 0;
  double *ctd_time_d = NULL;
  double *ctd_depth = NULL;

  error = MB_ERROR_NO_ERROR;
  if ((fp = fopen(input_ctd_file, "r")) != NULL) {
    const int num_ctd_alloc = GetNumRecords(fp);
    if (num_ctd_alloc) {
      const size_t size = num_ctd_alloc * sizeof(double);
      status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&ctd_time_d, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&ctd_depth, &error);
    }

    /* loop over reading the records */
    if (status == MB_SUCCESS) {
      char buffer[MB_PATH_MAXLINE];
      char *result = NULL;
      while ((result = fgets(buffer, (MB_PATH_MAXLINE - 1), fp)) == buffer) {
        double ctd_C;
        double ctd_D;
        double ctd_S;
        double ctd_T;
        double ctd_O2uM;
        double ctd_O2raw;
        double ctd_DGH_T;
        double ctd_C2_T;
        double ctd_C2_C;
        const int nget = sscanf(buffer, "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf",
                &time_d, &ctd_C, &ctd_T, &ctd_D, &ctd_S,
                &ctd_O2uM, &ctd_O2raw, &ctd_DGH_T, &ctd_C2_T, &ctd_C2_C);
        if (nget >= 4) {
          if (start_time_d <= 0.0)
            start_time_d = time_d;
          if (time_d > 0.0 && time_d < start_time_d)
            start_time_d = time_d;
          if (time_d > end_time_d)
            end_time_d = time_d;

          if (num_ctd < num_ctd_alloc) {
            ctd_time_d[num_ctd] = time_d;
            ctd_depth[num_ctd] = ctd_D;
            num_ctd++;
          }
        }
      }
    }

    fclose(fp);
  }

  else {
    if (verbose) {
      fprintf(stderr, "\nUnable to open CTD file: %s\n", input_ctd_file);
    }
  }

  /*-------------------------------------------------------------------*/
  /* load input rov data */

  int num_rov = 0;
  double *rov_time_d = NULL;
  double *rov_heading = NULL;
  double *rov_roll = NULL;
  double *rov_pitch = NULL;

  /* count the records */
  error = MB_ERROR_NO_ERROR;
  if ((fp = fopen(input_rov_file, "r")) != NULL) {
    const int num_rov_alloc = GetNumRecords(fp);
    if (num_rov_alloc) {
      const size_t size = num_rov_alloc * sizeof(double);
      status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&rov_time_d, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&rov_heading, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&rov_roll, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&rov_pitch, &error);
    }

    /* read the records */
    if (status == MB_SUCCESS) {
      /* loop over reading the records */
      char buffer[MB_PATH_MAXLINE];
      char *result = NULL;
      while ((result = fgets(buffer, (MB_PATH_MAXLINE - 1), fp)) == buffer) {
        double rov_x;
        double rov_y;
        double rov_z;
        double rov_yaw;
        double rov_magna_amps;
        double rov_F1;
        double rov_F2;
        double rov_F3;
        double rov_F4;
        double rov_F5;
        double rov_Heading;
        double rov_Pitch;
        double rov_Roll;
        const int nget = sscanf(buffer, "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf",
                &time_d, &rov_x, &rov_y, &rov_z, &rov_yaw, &rov_magna_amps,
                &rov_F1, &rov_F2, &rov_F3, &rov_F4, &rov_F5,
                &rov_Heading, &rov_Pitch, &rov_Roll);
        if (nget == 14) {
          if (start_time_d <= 0.0)
            start_time_d = time_d;
          if (time_d > 0.0 && time_d < start_time_d)
            start_time_d = time_d;
          if (time_d > end_time_d)
            end_time_d = time_d;

          if (num_rov < num_rov_alloc) {
            rov_time_d[num_rov] = time_d;
            rov_heading[num_rov] = rov_Heading;
            rov_roll[num_rov] = rov_Roll;
            rov_pitch[num_rov] = rov_Pitch;
            num_rov++;
          }
        }
      }
    }

    fclose(fp);
  }

  else {
    if (verbose) {
      fprintf(stderr, "\nUnable to open ROV file: %s\n", input_rov_file);
    }
  }

  int num_dvl = 0;
  double *dvl_time_d = NULL;
  double *dvl_altitude = NULL;
  double *dvl_stime = NULL;
  double *dvl_vx = NULL;
  double *dvl_vy = NULL;
  double *dvl_vz = NULL;
  double *dvl_status = NULL;

  /*-------------------------------------------------------------------*/
  /* load input dvl data */

  /* count the records */
  error = MB_ERROR_NO_ERROR;
  if ((fp = fopen(input_dvl_file, "r")) != NULL) {
    const int num_dvl_alloc = GetNumRecords(fp);
    if (status == MB_SUCCESS && num_dvl_alloc) {
      const size_t size = num_dvl_alloc * sizeof(double);
      status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&dvl_time_d, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&dvl_altitude, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&dvl_stime, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&dvl_vx, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&dvl_vy, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&dvl_vz, &error);
      if (status == MB_SUCCESS)
        status &= mb_mallocd(verbose, __FILE__, __LINE__, size, (void **)&dvl_status, &error);
    }

    /* read the records */
    if (status == MB_SUCCESS) {
      /* loop over reading the records - handle the different formats */
      char buffer[MB_PATH_MAXLINE];
      char *result = NULL;
      while ((result = fgets(buffer, (MB_PATH_MAXLINE - 1), fp)) == buffer) {
        double dvl_Altitude;
        double dvl_Stime;
        double dvl_Vx;
        double dvl_Vy;
        double dvl_Vz;
        double dvl_Status;
        const int nget = sscanf(buffer, "%lf,%lf,%lf,%lf,%lf,%lf,%lf",
                &time_d, &dvl_Altitude, &dvl_Stime, &dvl_Vx, &dvl_Vy, &dvl_Vz, &dvl_Status);
        if (nget == 7) {
          if (start_time_d <= 0.0)
            start_time_d = time_d;
          if (time_d > 0.0 && time_d < start_time_d)
            start_time_d = time_d;
          if (time_d > end_time_d)
            end_time_d = time_d;

          if (num_dvl < num_dvl_alloc) {
            dvl_time_d[num_dvl] = time_d;
            dvl_altitude[num_dvl] = dvl_Altitude;
            dvl_stime[num_dvl] = dvl_Stime;
            dvl_vx[num_dvl] = dvl_Vx;
            dvl_vy[num_dvl] = dvl_Vy;
            dvl_vz[num_dvl] = dvl_Vz;
            dvl_status[num_dvl] = dvl_Status;
            num_dvl++;
          }
        }
      }

      fclose(fp);
    }
  }

  else {
    if (verbose) {
      fprintf(stderr, "\nUnable to open DVL file: %s\n", input_dvl_file);
    }
  }

  /*-------------------------------------------------------------------*/

  /* get time range of output based on max bounds of any input data
    or use the specified time interval */
  if (rov_dive_start_time_set) {
    start_time_d = rov_dive_start_time_d;
  }
  if (rov_dive_end_time_set) {
    end_time_d = rov_dive_end_time_d;
  }
  start_time_d = floor(start_time_d);
  const int num_output = (int)(ceil((end_time_d - start_time_d) / interval));
  end_time_d = start_time_d + num_output * interval;

  /* get UTM projection for easting and northing fields */
  if (utm_zone_set) {
    if (utm_zone < 0)
      snprintf(projection_id, sizeof(projection_id), "UTM%2.2dS", abs(utm_zone));
    else
      snprintf(projection_id, sizeof(projection_id), "UTM%2.2dN", utm_zone);
  }
  else {
    utm_zone = (int)(((reference_lon + 183.0) / 6.0) + 0.5);
    if (reference_lat >= 0.0)
      snprintf(projection_id, sizeof(projection_id), "UTM%2.2dN", utm_zone);
    else
      snprintf(projection_id, sizeof(projection_id), "UTM%2.2dS", utm_zone);
  }

  int jnav = 0;
  int jctd = 0;
  int jdvl = 0;
  int jrov = 0;

  /* write the MiniROV navigation data */
  int num_position_valid = 0;
  int num_depth_valid = 0;
  int num_heading_valid = 0;
  int num_attitude_valid = 0;
  int num_altitude_valid = 0;

  if (status == MB_SUCCESS && num_nav > 0 && num_rov > 0) {
    if ((fp = fopen(output_file, "w")) == NULL) {
      error = MB_ERROR_OPEN_FAIL;
      status = MB_FAILURE;
    } else {
      int interp_error = MB_ERROR_NO_ERROR;

      bool onav_position_flag = false;
      bool onav_pressure_flag = false;
      bool onav_heading_flag = false;
      bool onav_altitude_flag = false;
      bool onav_attitude_flag = false;
      double onav_lon = 0.0;
      double onav_lat = 0.0;
      double onav_easting = 0.0;
      double onav_northing = 0.0;
      double onav_depth = 0.0;
      double onav_altitude = 0.0;
      double onav_roll = 0.0;
      double onav_pitch = 0.0;
      double onav_pressure;
      double onav_heading;

      int onav_time_i[7];
      int onav_time_j[5];

      void *pjptr = NULL;
      /* int proj_status = */ mb_proj_init(verbose, projection_id, &(pjptr), &error);

      /* loop over defined intervals (1 second by default) from start time to end time */
      for (int ioutput = 0; ioutput < num_output; ioutput++) {
        /* set the output time */
        const double onav_time_d = start_time_d + ioutput * interval;
        mb_get_date(verbose, onav_time_d, onav_time_i);
        const int onav_year = onav_time_i[0];
        const int onav_timetag = 10000 * onav_time_i[3] + 100 * onav_time_i[4] + onav_time_i[5];
        mb_get_jtime(verbose, onav_time_i, onav_time_j);
        const int onav_jday = onav_time_j[1];

        // int interp_status = MB_SUCCESS;
        if (num_nav > 0) {
          /* interp_status &= */ mb_linear_interp_longitude(verbose, nav_time_d - 1, nav_lon - 1, num_nav, onav_time_d, &onav_lon, &jnav, &interp_error);
          /* interp_status &= */ mb_linear_interp_latitude(verbose, nav_time_d - 1, nav_lat - 1, num_nav, onav_time_d, &onav_lat, &jnav, &interp_error);

          // if not interpolating navigation, then actually use the most recent
          // navigation values even if those are repeated, as identified by the
          // jnav value returned by the interpolation function
          if (interpolate_position) {
            onav_lon = nav_lon[jnav-1];
            onav_lat = nav_lat[jnav-1];
          }

          if (onav_lon != 0.0 && onav_lat != 0.0) {
            onav_position_flag = true;
            mb_proj_forward(verbose, pjptr, onav_lon, onav_lat, &onav_easting, &onav_northing, &error);
          }
        }
        if (num_ctd > 0) {
          /* interp_status &= */ mb_linear_interp(verbose, ctd_time_d - 1, ctd_depth - 1, num_ctd, onav_time_d, &onav_depth, &jctd, &interp_error);
          if (onav_depth != 0.0) {
            onav_pressure_flag = true;
            onav_pressure = onav_depth * (1.0052405 * (1 + 5.28E-3 * sin(DTR * onav_lat) * sin(DTR * onav_lat)));
          }
        }
        if (num_dvl > 0) {
          /* interp_status &= */ mb_linear_interp(verbose, dvl_time_d - 1, dvl_altitude - 1, num_dvl, onav_time_d, &onav_altitude, &jdvl, &interp_error);
          if (onav_altitude != 0.0) {
            onav_altitude_flag = true;
          }
        }
        if (num_rov > 0) {
          /* interp_status &= */ mb_linear_interp_heading(verbose, rov_time_d - 1, rov_heading - 1, num_rov, onav_time_d, &onav_heading, &jrov, &interp_error);
          if (onav_heading != 0.0) {
            onav_heading_flag = true;
          }
          /* interp_status &= */ mb_linear_interp(verbose, rov_time_d - 1, rov_roll - 1, num_rov, onav_time_d, &onav_roll, &jrov, &interp_error);
          /* interp_status &= */ mb_linear_interp(verbose, rov_time_d - 1, rov_pitch - 1, num_rov, onav_time_d, &onav_pitch, &jrov, &interp_error);
          if (onav_roll != 0.0 && onav_pitch != 0.0) {
            onav_attitude_flag = true;
          }
        }

        if (verbose >= 4) {
          fprintf(stderr, "\ndbg4  Data to be written in MBIO function <%s>\n", program_name);
          fprintf(stderr, "dbg4  Values,read:\n");
          fprintf(stderr, "dbg4       onav_time_d:         %f\n", onav_time_d);
          fprintf(stderr, "dbg4       onav_lat:            %f\n", onav_lat);
          fprintf(stderr, "dbg4       onav_lon:            %f\n", onav_lon);
          fprintf(stderr, "dbg4       onav_easting:        %f\n", onav_easting);
          fprintf(stderr, "dbg4       onav_northing:       %f\n", onav_northing);
          fprintf(stderr, "dbg4       onav_depth:          %f\n", onav_depth);
          fprintf(stderr, "dbg4       onav_pressure:       %f\n", onav_pressure);
          fprintf(stderr, "dbg4       onav_heading:        %f\n", onav_heading);
          fprintf(stderr, "dbg4       onav_altitude:       %f\n", onav_altitude);
          fprintf(stderr, "dbg4       onav_pitch:          %f\n", onav_pitch);
          fprintf(stderr, "dbg4       onav_roll:           %f\n", onav_roll);
          fprintf(stderr, "dbg4       onav_position_flag:  %d\n", onav_position_flag);
          fprintf(stderr, "dbg4       onav_pressure_flag:  %d\n", onav_pressure_flag);
          fprintf(stderr, "dbg4       onav_heading_flag:   %d\n", onav_heading_flag);
          fprintf(stderr, "dbg4       onav_altitude_flag:  %d\n", onav_altitude_flag);
          fprintf(stderr, "dbg4       onav_attitude_flag:  %d\n", onav_attitude_flag);
          fprintf(stderr, "dbg4       error:               %d\n", error);
          fprintf(stderr, "dbg4       status:              %d\n", status);
        }

        /* write the record */
        fprintf(fp, "%4.4d,%3.3d,%6.6d,%9.0f,%10.6f,%11.6f,%7.0f,%7.0f,%7.2f,%5.1f,%6.2f,%4.1f,%4.1f,%d,%d,%d,%d,%d\n",
            onav_year, onav_jday, onav_timetag, onav_time_d,
            onav_lat, onav_lon, onav_easting, onav_northing,
            onav_pressure, onav_heading, onav_altitude, onav_pitch, onav_roll,
            onav_position_flag, onav_pressure_flag, onav_heading_flag,
            onav_altitude_flag, onav_attitude_flag);
        num_position_valid += onav_position_flag;
        num_depth_valid += onav_pressure_flag;
        num_heading_valid += onav_heading_flag;
        num_attitude_valid += onav_attitude_flag;
        num_altitude_valid += onav_altitude_flag;

      }

      fclose(fp);
      /* int proj_status = */ mb_proj_free(verbose, &(pjptr), &error);
    }
  }

  if (verbose) {
    int time_i[7];
    fprintf(stdout,"Input data:\n\tNavigation:     %5d\n\tCTD:            %5d\n\tAttitude:       %5d\n\tDVL:            %5d\n",
        num_nav, num_ctd, num_rov, num_dvl);
    fprintf(stdout, "Output file: %s\n", output_file);
    fprintf(stdout, "\tOutput records: %d\n", num_output);
    mb_get_date(verbose, start_time_d, time_i);
    fprintf(stdout, "\tStart time:     %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
            time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6]);
    mb_get_date(verbose, end_time_d, time_i);
    fprintf(stdout, "\tEnd time:       %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
            time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6]);
    fprintf(stdout,"Valid output data:\n\tPosition:       %5d\n\tDepth:          %5d\n\tHeading:        %5d\n\tAttitude:       %5d\n\tAltitude:       %5d\n\n",
        num_position_valid, num_depth_valid, num_heading_valid, num_attitude_valid, num_altitude_valid);
  }


  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&nav_time_d, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&nav_lon, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&nav_lat, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&ctd_time_d, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&ctd_depth, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&rov_time_d, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&rov_heading, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&rov_roll, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&rov_pitch, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&dvl_time_d, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&dvl_altitude, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&dvl_stime, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&dvl_vx, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&dvl_vy, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&dvl_vz, &error);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&dvl_status, &error);

  Return(status);
}
/*--------------------------------------------------------------------*/
