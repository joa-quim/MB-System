/*--------------------------------------------------------------------
 *    The MB-system:  mbnavlab2fnv.c  6/4/2026
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
 * mbnavlab2fnv converts a Kongsberg Navlab smoothed navigation binary file
 * (navlab_smooth.bin) to an MB-System fast navigation (fnv) text file.
 *
 * The navlab_smooth.bin format consists of records each containing 21
 * IEEE 754 double-precision (64-bit) floating-point values in little-endian
 * byte order with no file header. The fields used are:
 *
 *   field  0: time (Unix epoch seconds)
 *   field  1: latitude (decimal degrees, positive north)
 *   field  2: longitude (decimal degrees, positive east)
 *   field  3: depth (meters, positive down) - used as draft
 *   field  6: heading (degrees, 0-360, clockwise from north)
 *   field  7: roll (degrees, positive starboard up)
 *   field  8: pitch (degrees, positive bow up)
 *   field 10: speed through water (m/s) - converted to km/hr for fnv
 *
 * Heave is not available in navlab_smooth.bin and is output as 0.0.
 *
 * The fnv output format is a tab-separated ASCII text file with the
 * following columns:
 *
 *   yyyy mm dd hh mm ss.ssssss  epoch_seconds  longitude  latitude
 *   heading  speed_kmhr  draft_m  roll  pitch  heave
 *
 * Author:	D. W. Caress
 * Date:	2026
 *
 *--------------------------------------------------------------------*/
/*
 * GMT-module port of src/utilities/mbnavlab2fnv.cc: options from GMT's option list (the program's
 * long options are GMT long options through module_kw), the fnv text written to -O's file or, without
 * it, as text records through the GMT API (mb_gmt_text.c), and every exit() a Return() with a GMT
 * error code.
 */

#define THIS_MODULE_NAME "mbnavlab2fnv"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Convert Kongsberg Navlab smoothed navigation to MB-System fnv navigation"
/* Primary input is the Navlab file given with -I; the fnv text goes to -O, else out as records. */
#define THIS_MODULE_KEYS "ID{,>D}"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mb_gmt_text.h"

/*--------------------------------------------------------------------*/
/* Program name and version */
static const char program_name[] = "mbnavlab2fnv";
static const char help_message[] =
    "mbnavlab2fnv converts a Kongsberg Navlab smoothed navigation binary\n"
    "file (navlab_smooth.bin) to an MB-System fast navigation (fnv)\n"
    "text file.\n\n"
    "The navlab_smooth.bin format contains records of 21 little-endian\n"
    "IEEE 754 double-precision floats per record with no file header:\n"
    "  field  0: time (Unix epoch seconds)\n"
    "  field  1: latitude (decimal degrees, positive north)\n"
    "  field  2: longitude (decimal degrees, positive east)\n"
    "  field  3: depth (meters, positive down) - output as draft\n"
    "  field  6: heading (degrees, 0-360)\n"
    "  field  7: roll (degrees, positive starboard up)\n"
    "  field  8: pitch (degrees, positive bow up)\n"
    "  field 10: speed through water (m/s)\n"
    "Heave is not available and is output as 0.0.";

static const char usage_message[] =
    "mbnavlab2fnv [-I navlab_smooth.bin] [-O output.fnv] [-V] [-H]";

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

/*--------------------------------------------------------------------*/
/* Conversion constants */
#define MS_TO_KMHR             3.6

/*--------------------------------------------------------------------*/
/* Check host byte order and swap a double if needed.
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
/* Break a Unix epoch time (seconds) into calendar components.
 * Uses gmtime_r for thread safety; falls back to gmtime on platforms
 * that lack gmtime_r. */
static void epoch_to_calendar(double time_d,
                               int *year, int *month, int *day,
                               int *hour, int *minute, double *second) {
    time_t t = (time_t)time_d;
    double frac = time_d - (double)t;
    struct tm ts;
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 1
    gmtime_r(&t, &ts);
#else
    ts = *gmtime(&t);
#endif
    *year   = ts.tm_year + 1900;
    *month  = ts.tm_mon + 1;
    *day    = ts.tm_mday;
    *hour   = ts.tm_hour;
    *minute = ts.tm_min;
    *second = (double)ts.tm_sec + frac;
}

/*--------------------------------------------------------------------*/

/* --- GMT front end ---------------------------------------------------- */

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'H', "help",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'V', "verbose", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",   "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "output",  "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

struct MBNAVLAB2FNV_CTRL {
	struct mbnf_H { bool active; } H;
	struct mbnf_I { bool active; char file[4096]; } I;
	struct mbnf_O { bool active; char file[4096]; } O;
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n", help_message);
	GMT_Message(API, GMT_TIME_NONE, "Long options: --input= (-I), --output= (-O), --help (-H), --verbose (-V).\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse(struct GMT_CTRL *GMT, struct MBNAVLAB2FNV_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	for (struct GMT_OPTION *opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case 'H':
			Ctrl->H.active = true;
			break;
		case 'I':
			Ctrl->I.active = true;
			strncpy(Ctrl->I.file, opt->arg, sizeof (Ctrl->I.file) - 1);
			break;
		case 'O':
			Ctrl->O.active = true;
			strncpy(Ctrl->O.file, opt->arg, sizeof (Ctrl->O.file) - 1);
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}
	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code) { gmt_M_free_options(mode); return code; }
#define Return(code) { if (ofp) mb_gmt_text_end(ofp); gmt_M_free(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbnavlab2fnv(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbnavlab2fnv(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MBNAVLAB2FNV_CTRL *Ctrl = NULL;
	struct MB_GMT_TEXT *ofp = NULL;	/* the fnv text: -O's file, else records */
	int error;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a valid run -- the program converts stdin */
	if ((error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBNAVLAB2FNV_CTRL);
	if ((error = parse(GMT, Ctrl, options)) != GMT_NOERROR) Return(error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

    /* --- options --- */
    char input_file[4096]  = "";   /* read from stdin if empty */
    char output_file[4096] = "";   /* records if empty */
    const int verbose = GMT->common.V.active ? 1 : 0;
    if (Ctrl->I.active) strncpy(input_file, Ctrl->I.file, sizeof (input_file) - 1);
    if (Ctrl->O.active) strncpy(output_file, Ctrl->O.file, sizeof (output_file) - 1);

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
        ifp = fopen(input_file, "rb");
        if (ifp == NULL) {
            fprintf(stderr, "\n%s: ERROR - cannot open input file '%s': %s\n",
                    program_name, input_file, strerror(errno));
            Return(GMT_ERROR_ON_FOPEN);
        }
    }
    else {
        ifp = stdin;
    }

    /* --- open output: -O's file, else text records through the GMT API --- */
    if (strlen(output_file) > 0) {
        ofp = mb_gmt_text_file(GMT, output_file);
        if (ofp == NULL) {
            fprintf(stderr, "\n%s: ERROR - cannot open output file '%s': %s\n",
                    program_name, output_file, strerror(errno));
            if (ifp != stdin)
                fclose(ifp);
            Return(GMT_ERROR_ON_FOPEN);
        }
    }
    else if ((ofp = mb_gmt_text_begin(GMT, options)) == NULL) {
        if (ifp != stdin)
            fclose(ifp);
        Return(API->error);
    }

    /* --- determine whether byte-swapping is needed --- */
    const int swap = host_is_bigendian();

    /* --- write fnv header --- */
    mb_gmt_text_put(ofp,
        "## <yyyy mm dd hh mm ss.ssssss> <epoch seconds> "
        "<longitude (deg)> <latitude (deg)> "
        "<heading (deg)> <speed (km/hr)> <draft (m)> "
        "<roll (deg)> <pitch (deg)> <heave (m)>\n");

    /* --- main read/write loop --- */
    double rec[NAVLAB_RECORD_NFIELDS];
    long   nread    = 0;
    long   nwritten = 0;
    double prev_time_d = -1.0;

    while (fread(rec, sizeof(double), NAVLAB_RECORD_NFIELDS, ifp)
               == (size_t)NAVLAB_RECORD_NFIELDS) {
        nread++;

        /* byte-swap if host is big-endian */
        if (swap) {
            for (int i = 0; i < NAVLAB_RECORD_NFIELDS; i++)
                rec[i] = swap_double(rec[i]);
        }

        /* extract fields */
        const double time_d  = rec[NAVLAB_FIELD_TIME];
        const double lat     = rec[NAVLAB_FIELD_LAT];
        const double lon     = rec[NAVLAB_FIELD_LON];
        const double depth   = rec[NAVLAB_FIELD_DEPTH];
        const double heading = rec[NAVLAB_FIELD_HEADING];
        const double roll    = rec[NAVLAB_FIELD_ROLL];
        const double pitch   = rec[NAVLAB_FIELD_PITCH];
        const double speed   = rec[NAVLAB_FIELD_SPEED] * MS_TO_KMHR;
        const double heave   = 0.0;

        /* skip records with retrograde timestamps */
        if (time_d <= prev_time_d) {
            if (verbose)
                fprintf(stderr,
                    "%s: WARNING - skipping retrograde timestamp "
                    "%.6f (previous %.6f) at record %ld\n",
                    program_name, time_d, prev_time_d, nread);
            continue;
        }
        prev_time_d = time_d;

        /* decompose epoch time to calendar */
        int    year, month, day, hour, minute;
        double second;
        epoch_to_calendar(time_d, &year, &month, &day,
                          &hour, &minute, &second);

        /* write fnv record - all fields tab-separated:
         *
         *   yyyy mm dd hh mm ss.ssssss <TAB>
         *   epoch_seconds <TAB>
         *   longitude <TAB>
         *   latitude <TAB>
         *   heading <TAB>
         *   speed_kmhr <TAB>
         *   draft_m <TAB>
         *   roll <TAB>
         *   pitch <TAB>
         *   heave
         */
        mb_gmt_text_put(ofp,
            "%4d %02d %02d %02d %02d %09.6f\t"   /* date and time      */
            "%.6f\t"                               /* epoch seconds      */
            "%15.10f\t"                            /* longitude          */
            "%15.10f\t"                            /* latitude           */
            "%.3f\t"                               /* heading            */
            "%.3f\t"                               /* speed (km/hr)      */
            "%.4f\t"                               /* draft / depth      */
            "%.3f\t"                               /* roll               */
            "%.3f\t"                               /* pitch              */
            "%7.4f\n",                             /* heave              */
            year, month, day, hour, minute, second,
            time_d,
            lon,
            lat,
            heading,
            speed,
            depth,
            roll,
            pitch,
            heave);

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
    const int output_failed = mb_gmt_text_end(ofp);
    ofp = NULL;	/* closed: Return() must not close it again */
    if (output_failed) Return(GMT_RUNTIME_ERROR);

    /* --- final status --- */
    if (verbose) {
        fprintf(stderr, "\n%s: %ld records read, %ld records written\n",
                program_name, nread, nwritten);
        if (nread != nwritten)
            fprintf(stderr, "%s: %ld records skipped (retrograde timestamps)\n",
                    program_name, nread - nwritten);
    }

    Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
