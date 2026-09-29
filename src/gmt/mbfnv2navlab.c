/*--------------------------------------------------------------------
 *    The MB-system:  mbfnv2navlab.c  6/4/2026
 *
 *    Copyright (c) 2026-2026 by
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
 * mbfnv2navlab converts an MB-System fast navigation (fnv) text file to a
 * Kongsberg Navlab smoothed navigation binary file (navlab_smooth.bin).
 *
 * The fnv format is a tab-separated ASCII text file. Lines beginning with
 * '#' are treated as comments and skipped. Each data line has the columns:
 *
 *   yyyy mm dd hh mm ss.ssssss  epoch_s  longitude  latitude
 *   heading  speed_kmhr  draft_m  roll  pitch  heave
 *   [portlon  portlat  stbdlon  stbdlat]
 *
 * The navlab_smooth.bin format consists of records each containing 21
 * IEEE 754 double-precision (64-bit) floating-point values in little-endian
 * byte order with no file header. The mapping from fnv columns is:
 *
 *   field  0: time_d    <- epoch_s
 *   field  1: latitude  <- latitude
 *   field  2: longitude <- longitude
 *   field  3: depth     <- draft_m
 *   field  4: 0.0       (not available in fnv)
 *   field  5: 0.0       (not available in fnv)
 *   field  6: heading   <- heading
 *   field  7: roll      <- roll
 *   field  8: pitch     <- pitch
 *   field  9: 0.0       (vertical velocity, not in fnv)
 *   field 10: speed_ms  <- speed_kmhr / 3.6
 *   field 11: speed_ms  <- speed_kmhr / 3.6 (copy of field 10)
 *   fields 12-20: 0.0   (not available in fnv)
 *
 * Author:	D. W. Caress
 * 					Substantially written using Claude AI
 * Date:	June 4, 2026
 *
 *--------------------------------------------------------------------*/
/*
 * GMT-module port of src/utilities/mbfnv2navlab.cc. The program's getopt_long() option loop
 * is kept as it is, running on the reentrant mb_getopt_long() (the state
 * lives in a local structure, so the module can run any number of times in
 * one GMT session), and main() becomes GMT_mbfnv2navlab(), with every exit()
 * turned into Return().
 */

#define THIS_MODULE_NAME "mbfnv2navlab"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Convert MB-System fnv navigation to a Kongsberg Navlab smoothed navigation file"
/* Primary input is the fnv file given with -I; the Navlab file is written by the module itself. */
#define THIS_MODULE_KEYS "ID{"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_getopt.h"

/*--------------------------------------------------------------------*/
/* Program name and version */
static const char program_name[] = "mbfnv2navlab";
static const char help_message[] =
    "mbfnv2navlab converts an MB-System fast navigation (fnv) text\n"
    "file to a Kongsberg Navlab smoothed navigation binary file\n"
    "(navlab_smooth.bin).\n\n"
    "The output binary format contains records of 21 little-endian\n"
    "IEEE 754 double-precision floats per record with no file header.\n"
    "Fields populated from the fnv input:\n"
    "  field  0: time (Unix epoch seconds)\n"
    "  field  1: latitude (decimal degrees, positive north)\n"
    "  field  2: longitude (decimal degrees, positive east)\n"
    "  field  3: depth (meters, positive down) - from fnv draft column\n"
    "  field  6: heading (degrees, 0-360)\n"
    "  field  7: roll (degrees, positive starboard up)\n"
    "  field  8: pitch (degrees, positive bow up)\n"
    "  field 10: speed through water (m/s) - converted from km/hr\n"
    "  field 11: speed through water (m/s) - copy of field 10\n"
    "Fields not available in fnv are set to 0.0:\n"
    "  fields 4, 5, 9, 12-20";

static const char usage_message[] =
    "mbfnv2navlab [-I input.fnv] [-O navlab_smooth.bin] [-V] [-H]";

/*--------------------------------------------------------------------*/
/* Navlab binary record layout */
#define NAVLAB_RECORD_NFIELDS  21
#define NAVLAB_RECORD_SIZE     (NAVLAB_RECORD_NFIELDS * sizeof(double))

#define NAVLAB_FIELD_TIME      0
#define NAVLAB_FIELD_LAT       1
#define NAVLAB_FIELD_LON       2
#define NAVLAB_FIELD_DEPTH     3
#define NAVLAB_FIELD_HEADING   6
#define NAVLAB_FIELD_ROLL      7
#define NAVLAB_FIELD_PITCH     8
#define NAVLAB_FIELD_SPEED     10
#define NAVLAB_FIELD_SPEED2    11

/*--------------------------------------------------------------------*/
/* fnv tab-field indices (0-based after splitting on '\t') */
#define FNV_TAB_DATETIME   0    /* "yyyy mm dd hh mm ss.ssssss" */
#define FNV_TAB_EPOCH      1    /* decimal epoch seconds        */
#define FNV_TAB_LON        2    /* longitude, decimal degrees   */
#define FNV_TAB_LAT        3    /* latitude,  decimal degrees   */
#define FNV_TAB_HEADING    4    /* heading, degrees             */
#define FNV_TAB_SPEED      5    /* speed, km/hr                 */
#define FNV_TAB_DRAFT      6    /* draft / depth, meters        */
#define FNV_TAB_ROLL       7    /* roll, degrees                */
#define FNV_TAB_PITCH      8    /* pitch, degrees               */
#define FNV_TAB_HEAVE      9    /* heave, meters (discarded)    */
#define FNV_NFIELDS_MIN    10   /* minimum required tab fields  */

/*--------------------------------------------------------------------*/
/* Conversion constants */
#define KMHR_TO_MS  (1.0 / 3.6)

/*--------------------------------------------------------------------*/
/* Check host byte order.
 * navlab_smooth.bin is always little-endian. */
static int host_is_bigendian(void) {
    const uint16_t test = 0x0001;
    return (*((const uint8_t *)&test) == 0x00);
}

static double swap_double(double val) {
    double out;
    const uint8_t *src = (const uint8_t *)&val;
    uint8_t *dst = (uint8_t *)&out;
    dst[0] = src[7];
    dst[1] = src[6];
    dst[2] = src[5];
    dst[3] = src[4];
    dst[4] = src[3];
    dst[5] = src[2];
    dst[6] = src[1];
    dst[7] = src[0];
    return out;
}

/*--------------------------------------------------------------------*/
/* Write one navlab record.  Applies byte-swapping when the host is
 * big-endian so the output is always little-endian. */
static int write_navlab_record(FILE *ofp, double rec[NAVLAB_RECORD_NFIELDS],
                                int swap) {
    double out[NAVLAB_RECORD_NFIELDS];
    if (swap) {
        for (int i = 0; i < NAVLAB_RECORD_NFIELDS; i++)
            out[i] = swap_double(rec[i]);
    } else {
        for (int i = 0; i < NAVLAB_RECORD_NFIELDS; i++)
            out[i] = rec[i];
    }
    return (int)fwrite(out, sizeof(double), NAVLAB_RECORD_NFIELDS, ofp);
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
EXTERN_MSC int GMT_mbfnv2navlab(void *V_API, int gmt_mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbfnv2navlab(void *V_API, int gmt_mode, void *args) {
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


    /* --- option defaults --- */
    char input_file[4096]  = "";   /* read from stdin if empty  */
    char output_file[4096] = "";   /* write to stdout if empty  */
    int  verbose           = 0;

    /* --- long option table --- */
    static struct mb_getopt_option long_options[] = {
        { "help",    mb_no_argument,       NULL, 'H' },
        { "verbose", mb_no_argument,       NULL, 'V' },
        { "input",   mb_required_argument, NULL, 'I' },
        { "output",  mb_required_argument, NULL, 'O' },
        { NULL,      0,                 NULL,  0  }
    };

    /* --- parse command-line options --- */
    int opt;
    while ((opt = mb_getopt_long(&getopt_state, argc, argv, "HVI:O:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'H':
            fprintf(stdout, "\nProgram %s\n\n", program_name);
            fprintf(stdout, "Usage:  %s\n\n", usage_message);
            fprintf(stdout, "%s\n\n", help_message);
            Return(EXIT_SUCCESS);
            break;
        case 'V':
            verbose = 1;
            break;
        case 'I':
            strncpy(input_file, getopt_state.optarg, sizeof(input_file) - 1);
            input_file[sizeof(input_file) - 1] = '\0';
            break;
        case 'O':
            strncpy(output_file, getopt_state.optarg, sizeof(output_file) - 1);
            output_file[sizeof(output_file) - 1] = '\0';
            break;
        default:
            fprintf(stderr, "Usage:  %s\n", usage_message);
            Return(EXIT_FAILURE);
        }
    }

    /* --- verbose banner --- */
    if (verbose) {
        fprintf(stderr, "\nProgram %s\n", program_name);
        fprintf(stderr, "Input file:  %s\n",
                strlen(input_file)  > 0 ? input_file  : "(stdin)");
        fprintf(stderr, "Output file: %s\n",
                strlen(output_file) > 0 ? output_file : "(stdout)");
    }

    /* --- open input --- */
    FILE *ifp;
    if (strlen(input_file) > 0) {
        ifp = fopen(input_file, "r");
        if (ifp == NULL) {
            fprintf(stderr, "\n%s: ERROR - cannot open input file '%s': %s\n",
                    program_name, input_file, strerror(errno));
            Return(EXIT_FAILURE);
        }
    }
    else {
        ifp = stdin;
    }

    /* --- open output --- */
    FILE *ofp;
    if (strlen(output_file) > 0) {
        ofp = fopen(output_file, "wb");
        if (ofp == NULL) {
            fprintf(stderr, "\n%s: ERROR - cannot open output file '%s': %s\n",
                    program_name, output_file, strerror(errno));
            if (ifp != stdin)
                fclose(ifp);
            Return(EXIT_FAILURE);
        }
    }
    else {
        ofp = stdout;
    }

    /* --- byte-swap flag --- */
    const int swap = host_is_bigendian();

    /* --- main read/write loop --- */
    char  line[4096];
    long  nread    = 0;   /* lines read (excluding comments/blanks) */
    long  nwritten = 0;   /* records written successfully           */
    long  nskipped = 0;   /* lines skipped (bad parse or retrograde)*/
    double prev_time_d = -1.0;

    while (fgets(line, sizeof(line), ifp) != NULL) {

        /* skip comment lines (starting with '#') and blank lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        nread++;

        /* --- split line on tabs --- */
        /* We need at least FNV_NFIELDS_MIN tab-separated fields.
         * The first field is itself space-separated date-time; we
         * do not need to parse it because we use the epoch seconds
         * field directly. */
        char *tabs[20];
        int   ntabs = 0;
        char *p = line;
        while (ntabs < 20) {
            tabs[ntabs++] = p;
            char *next = strchr(p, '\t');
            if (next == NULL)
                break;
            *next = '\0';
            p = next + 1;
        }

        if (ntabs < FNV_NFIELDS_MIN) {
            if (verbose)
                fprintf(stderr,
                    "%s: WARNING - skipping line %ld: only %d tab fields "
                    "(need %d)\n",
                    program_name, nread, ntabs, FNV_NFIELDS_MIN);
            nskipped++;
            continue;
        }

        /* --- parse required fields --- */
        const double time_d  = strtod(tabs[FNV_TAB_EPOCH],   NULL);
        const double lon     = strtod(tabs[FNV_TAB_LON],     NULL);
        const double lat     = strtod(tabs[FNV_TAB_LAT],     NULL);
        const double heading = strtod(tabs[FNV_TAB_HEADING], NULL);
        const double speed   = strtod(tabs[FNV_TAB_SPEED],   NULL) * KMHR_TO_MS;
        const double depth   = strtod(tabs[FNV_TAB_DRAFT],   NULL);
        const double roll    = strtod(tabs[FNV_TAB_ROLL],    NULL);
        const double pitch   = strtod(tabs[FNV_TAB_PITCH],   NULL);

        /* sanity-check epoch (must be positive and monotonically increasing) */
        if (time_d <= 0.0) {
            if (verbose)
                fprintf(stderr,
                    "%s: WARNING - skipping line %ld: invalid epoch %.6f\n",
                    program_name, nread, time_d);
            nskipped++;
            continue;
        }
        if (time_d <= prev_time_d) {
            if (verbose)
                fprintf(stderr,
                    "%s: WARNING - skipping line %ld: retrograde timestamp "
                    "%.6f (previous %.6f)\n",
                    program_name, nread, time_d, prev_time_d);
            nskipped++;
            continue;
        }
        prev_time_d = time_d;

        /* --- build navlab record --- */
        double rec[NAVLAB_RECORD_NFIELDS];
        for (int i = 0; i < NAVLAB_RECORD_NFIELDS; i++)
            rec[i] = 0.0;

        rec[NAVLAB_FIELD_TIME]    = time_d;
        rec[NAVLAB_FIELD_LAT]     = lat;
        rec[NAVLAB_FIELD_LON]     = lon;
        rec[NAVLAB_FIELD_DEPTH]   = depth;
        rec[NAVLAB_FIELD_HEADING] = heading;
        rec[NAVLAB_FIELD_ROLL]    = roll;
        rec[NAVLAB_FIELD_PITCH]   = pitch;
        rec[NAVLAB_FIELD_SPEED]   = speed;
        rec[NAVLAB_FIELD_SPEED2]  = speed;   /* copy: best estimate */
        /* fields 4, 5, 9, 12-20 remain 0.0 */

        /* --- write record --- */
        if (write_navlab_record(ofp, rec, swap) != NAVLAB_RECORD_NFIELDS) {
            fprintf(stderr, "\n%s: ERROR writing output at line %ld: %s\n",
                    program_name, nread, strerror(errno));
            break;
        }
        nwritten++;
    }

    /* --- check for read errors --- */
    if (ferror(ifp)) {
        fprintf(stderr, "\n%s: ERROR reading input: %s\n",
                program_name, strerror(errno));
    }

    /* --- close files --- */
    if (ifp != stdin)
        fclose(ifp);
    if (ofp != stdout)
        fclose(ofp);

    /* --- final status --- */
    if (verbose) {
        fprintf(stderr, "\n%s: %ld lines read, %ld records written",
                program_name, nread, nwritten);
        if (nskipped > 0)
            fprintf(stderr, ", %ld lines skipped", nskipped);
        fprintf(stderr, "\n");
    }

    Return(EXIT_SUCCESS);
}
/*--------------------------------------------------------------------*/
