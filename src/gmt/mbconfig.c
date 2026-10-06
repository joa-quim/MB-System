/*--------------------------------------------------------------------
 *    The MB-system:	mbconfig.c	5/5/2017
 *
 *    Copyright (c) 2014-2025 by
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
 * mbconfig provides command line access to the MB-System version and to the
 * locations of the levitus database and the OTPS tidal correction software.
 *
 * Author:	D. W. Caress
 * Date:	May 5, 2017
 *
 * GMT-module port of src/utilities/mbconfig.cc. The program has long options only (--prefix,
 * --cflags, --libs, --version, --version-id, --version-major, --version-minor, --version-archive,
 * --levitus, --otps, --verbose, --help); they come from GMT's option list through
 * mb_gmt_mark_long_options() / mb_gmt_long_option(), exactly as the program takes them. The answers
 * are printed through the GMT API (mb_gmt_text.c); every Return() is a GMT error code.
 */

#define THIS_MODULE_NAME		"mbconfig"
#define THIS_MODULE_LIB			"mbsystem"
#define THIS_MODULE_PURPOSE		"Command line access to MB-System version, install prefix, compile/link flags, and database locations"
#define THIS_MODULE_KEYS		">D}"
#define THIS_MODULE_NEEDS		""
#define THIS_MODULE_OPTIONS		"->V"

#include "gmt_dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_define.h"
#include "mb_format.h"
#include "mb_status.h"
#include "mb_gmt_opts.h"
#include "mb_gmt_text.h"

#ifndef MBSYSTEM_INSTALL_PREFIX
#define MBSYSTEM_INSTALL_PREFIX "/usr/local"
#endif
#ifndef ENV_LEVITUSANNUAL82
#define ENV_LEVITUSANNUAL82 "share/mbsystem/LevitusAnnual82.dat"
#endif
#ifndef ENV_OTPSDIR
#define ENV_OTPSDIR "/usr/local/src/otps"
#endif

static const char help_message[] =
    "mbconfig provides command line access to the MB-System installation location, "
    "the compile and libs flags needed to compile and link programs using MB-System "
    "libraries, and the locations of the levitus database and the OTPS tidal "
    "correction software.\n";
static const char usage_message[] =
    "mbconfig [--verbose --help --prefix --cflags --libs --version --version-id --version-major "
    "--version-minor --version-archive --levitus --otps]";

EXTERN_MSC int GMT_mbconfig(void *API, int mode, void *args);

/* The program's long options, in the order of mbc_mode_t (it has no short ones) */
typedef enum {
	MBC_MODE_VERBOSE         = 0,
	MBC_MODE_HELP            = 1,
	MBC_MODE_PREFIX          = 2,
	MBC_MODE_CFLAGS          = 3,
	MBC_MODE_LIBS            = 4,
	MBC_MODE_VERSION         = 5,
	MBC_MODE_VERSION_ID      = 6,
	MBC_MODE_VERSION_MAJOR   = 7,
	MBC_MODE_VERSION_MINOR   = 8,
	MBC_MODE_VERSION_ARCHIVE = 9,
	MBC_MODE_LEVITUS         = 10,
	MBC_MODE_OTPS            = 11,
	MBC_MODE_COUNT           = 12
} mbc_mode_t;

static const struct MB_GMT_LONGOPT_DEF long_options[] = {
	{"verbose", false},
	{"help", false},
	{"prefix", false},
	{"cflags", false},
	{"libs", false},
	{"version", false},
	{"version-id", false},
	{"version-major", false},
	{"version-minor", false},
	{"version-archive", false},
	{"levitus", false},
	{"otps", false},
	{NULL, false}};

/* --- Control structure ---------------------------------------------- */

struct MBCONFIG_CTRL {
	int verbose;
	bool mode_set;
	bool flag[MBC_MODE_COUNT];
};

static void *New_mbconfig_Ctrl(struct GMT_CTRL *GMT) {
	struct MBCONFIG_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBCONFIG_CTRL);
	return Ctrl;
}

static void Free_mbconfig_Ctrl(struct GMT_CTRL *GMT, struct MBCONFIG_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "\n%s\n\n", help_message);
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse(struct GMT_CTRL *GMT, struct MBCONFIG_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (struct GMT_OPTION *opt = options; opt; opt = opt->next) {
		const char *value;
		int k;
		switch (opt->option) {
		case 'V':	/* GMT's -V is the program's --verbose */
			Ctrl->verbose++;
			break;
		case MB_GMT_LONGOPT:
			if ((k = mb_gmt_long_option(opt, long_options, &value)) < 0) {
				GMT_Report(API, GMT_MSG_ERROR, "Unrecognized option --%s\n", opt->arg);
				n_errors++;
			}
			else if (k == MBC_MODE_VERBOSE)
				Ctrl->verbose++;
			else {
				Ctrl->flag[k] = true;
				Ctrl->mode_set = true;
			}
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}

	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code)  { gmt_M_free_options(mode); return code; }
#define Return(code)   { if (T) mb_gmt_text_end(T); Free_mbconfig_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }

/*--------------------------------------------------------------------*/
int GMT_mbconfig(void *V_API, int mode, void *args) {
	int error = MB_ERROR_NO_ERROR;
	int gmt_error;

	struct MBCONFIG_CTRL *Ctrl = NULL;
	struct MB_GMT_TEXT   *T = NULL;
	struct GMT_CTRL      *GMT  = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION    *options = NULL;
	struct GMTAPI_CTRL   *API = gmt_get_api_ptr(V_API);

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program (it prints the version) */
	if ((gmt_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(gmt_error);

	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS, NULL, &options, &GMT_cpy)) == NULL) bailout(API->error);
	/* the program's long options kept out of GMT's --PAR=value handling */
	mb_gmt_mark_long_options(API, &options, long_options);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	Ctrl = New_mbconfig_Ctrl(GMT);
	if ((gmt_error = parse(GMT, Ctrl, options)) != GMT_NOERROR) Return(gmt_error);

	const int verbose = Ctrl->verbose;

	const bool mode_help           = Ctrl->flag[MBC_MODE_HELP];
	const bool mode_prefix         = Ctrl->flag[MBC_MODE_PREFIX];
	const bool mode_cflags         = Ctrl->flag[MBC_MODE_CFLAGS];
	const bool mode_libs           = Ctrl->flag[MBC_MODE_LIBS];
	bool mode_version              = Ctrl->flag[MBC_MODE_VERSION];
	const bool mode_version_id     = Ctrl->flag[MBC_MODE_VERSION_ID];
	const bool mode_version_major  = Ctrl->flag[MBC_MODE_VERSION_MAJOR];
	const bool mode_version_minor  = Ctrl->flag[MBC_MODE_VERSION_MINOR];
	const bool mode_version_archive = Ctrl->flag[MBC_MODE_VERSION_ARCHIVE];
	const bool mode_levitus        = Ctrl->flag[MBC_MODE_LEVITUS];
	const bool mode_otps           = Ctrl->flag[MBC_MODE_OTPS];

	/* if no mode specified then just do version */
	if (!Ctrl->mode_set)
		mode_version = true;

	mb_path version_string;
	int version_id;
	int version_major;
	int version_minor;
	int version_archive;

	int status = mb_version(verbose, version_string, &version_id, &version_major, &version_minor, &version_archive, &error);

	if (verbose == 1 || mode_help) {
		fprintf(stderr, "\n# Program %s\n", THIS_MODULE_NAME);
		fprintf(stderr, "# MB-system Version %s\n", version_string);
	}

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s>\n", THIS_MODULE_NAME);
		fprintf(stderr, "dbg2  MB-system Version %s\n", version_string);
		fprintf(stderr, "dbg2  Default MB-System Parameters:\n");
		fprintf(stderr, "dbg2       verbose:                    %d\n", verbose);
		fprintf(stderr, "dbg2       mode_set:                   %d\n", Ctrl->mode_set);
		fprintf(stderr, "dbg2       mode_help:                  %d\n", mode_help);
		fprintf(stderr, "dbg2       mode_prefix:                %d\n", mode_prefix);
		fprintf(stderr, "dbg2       mode_cflags:                %d\n", mode_cflags);
		fprintf(stderr, "dbg2       mode_libs:                  %d\n", mode_libs);
		fprintf(stderr, "dbg2       mode_version:               %d\n", mode_version);
		fprintf(stderr, "dbg2       mode_version_id:            %d\n", mode_version_id);
		fprintf(stderr, "dbg2       mode_version_major:         %d\n", mode_version_major);
		fprintf(stderr, "dbg2       mode_version_minor:         %d\n", mode_version_minor);
		fprintf(stderr, "dbg2       mode_version_archive:       %d\n", mode_version_archive);
		fprintf(stderr, "dbg2       mode_levitus:               %d\n", mode_levitus);
		fprintf(stderr, "dbg2       mode_otps:                  %d\n", mode_otps);
	}

	/* --help prints the description and goes on with any other request, as the program does */
	if (mode_help)
		usage(API, GMT_USAGE);

	/* the answers: what the program prints on stdout, through the GMT API */
	if ((T = mb_gmt_text_begin(GMT, options)) == NULL) Return(API->error);

	if (mode_prefix) {
		if (verbose > 0)
			mb_gmt_text_put(T, "\n# MB-System install prefix:\n");
		mb_gmt_text_put(T, "%s\n", MBSYSTEM_INSTALL_PREFIX);
	}

	if (mode_cflags) {
		if (verbose > 0)
			mb_gmt_text_put(T, "\n# MB-System compile flags:\n");
		mb_gmt_text_put(T, "-I%s/include\n", MBSYSTEM_INSTALL_PREFIX);
	}

	if (mode_libs) {
		if (verbose > 0)
			mb_gmt_text_put(T, "\n# MB-System link flags:\n");
		mb_gmt_text_put(T, "-L%s/lib -lmbaux -lmbsapi -lmbbsio -lmbview -lmbgsf -lmbxgr -lmbio\n",
		        MBSYSTEM_INSTALL_PREFIX);
	}

	if (mode_version) {
		if (verbose > 0)
			mb_gmt_text_put(T, "\n# MB-System version:\n");
		mb_gmt_text_put(T, "%s\n", version_string);
	}

	if (mode_version_id) {
		if (verbose > 0)
			mb_gmt_text_put(T, "\n# MB-System version id:\n");
		mb_gmt_text_put(T, "%d\n", version_id);
	}

	if (mode_version_major) {
		if (verbose > 0)
			mb_gmt_text_put(T, "\n# MB-System major version:\n");
		mb_gmt_text_put(T, "%d\n", version_major);
	}

	if (mode_version_minor) {
		if (verbose > 0)
			mb_gmt_text_put(T, "\n# MB-System minor version:\n");
		mb_gmt_text_put(T, "%d\n", version_minor);
	}

	if (mode_version_archive) {
		if (verbose > 0)
			mb_gmt_text_put(T, "\n# MB-System archive version:\n");
		mb_gmt_text_put(T, "%d\n", version_archive);
	}

	if (mode_levitus) {
		if (verbose > 0)
			mb_gmt_text_put(T, "# MB-System Levitus database location:\n");
		mb_gmt_text_put(T, "%s/%s\n", MBSYSTEM_INSTALL_PREFIX, ENV_LEVITUSANNUAL82);
	}

	if (mode_otps) {
		if (verbose > 0)
			mb_gmt_text_put(T, "\n# OTPS tide modeling package location:\n");
		mb_gmt_text_put(T, "%s\n", ENV_OTPSDIR);
	}

	const int output_failed = mb_gmt_text_end(T);
	T = NULL;
	if (output_failed) Return(GMT_RUNTIME_ERROR);

	if (verbose >= 4)
		status = mb_memory_list(verbose, &error);

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s> completed\n", THIS_MODULE_NAME);
		fprintf(stderr, "dbg2  Ending status:\n");
		fprintf(stderr, "dbg2       status:  %d\n", status);
	}

	if (error != MB_ERROR_NO_ERROR) {
		char *message;
		mb_error(verbose, error, &message);
		GMT_Report(API, GMT_MSG_ERROR, "%s\n", message);
		Return(GMT_RUNTIME_ERROR);
	}
	Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
