/*--------------------------------------------------------------------
 *    The MB-system:	mbdumpesf.c	3/20/2008
 *
 *    Copyright (c) 2008-2025 by
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
 * mbdumpesf reads an MB-System edit save file and dumps the contents
 * as an ascii table to stdout. This is primarily used for debugging
 * bathymetry editing tools such as mbedit and mbeditviz.
 *
 * Author:	D. W. Caress
 * Date:	March 20, 2008
 */
/*
 * GMT-module port of src/utilities/mbdumpesf.cc: options from GMT's option list (long options
 * through module_kw, the long-only ones given short letters), the dump written as text records
 * through the GMT API (mb_gmt_text.c), and every exit() a Return() with a GMT error code.
 */

#define THIS_MODULE_NAME "mbdumpesf"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Dump an MB-System edit save file as an ascii table"
/* Primary input is the edit save file given with -I; the table goes to stdout. */
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
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _MSC_VER
#include "unistd_w.h"
#else
#include <unistd.h>
#endif
#include "mb_define.h"
#include "mb_format.h"
#include "mb_process.h"
#include "mb_status.h"
#include "mb_swap.h"
#include "mb_gmt_text.h"

typedef enum {
    OUTPUT_TEXT = 0,
    OUTPUT_ESF = 1,
} omode_t;

static const char program_name[] = "mbdumpesf";
static const char help_message[] =
    "mbdumpesf reads an MB-System edit save file and dumps the\n"
    "contents as an ascii table to stdout.";
static const char usage_message[] =
    "mbdumpesf --input=esffile\n"
    "\t[--output=esffile --ignore-unflag --ignore-flag\n"
    "\t--ignore-filter --ignore-zero\n"
    "\t--verbose --help]";

/*--------------------------------------------------------------------*/


/* --- GMT front end ---------------------------------------------------- */

/* Translation table from the program's long options to the module's short ones. The output and
   ignore-* options are long-only in the program; -O, -U, -F, -L and -Z are their short letters here. */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'v', "verbose",       "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "output",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'U', "ignore-unflag", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'F', "ignore-flag",   "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'L', "ignore-filter", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'Z', "ignore-zero",   "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n", help_message);
	GMT_Message(API, GMT_TIME_NONE, "Short forms: -I (--input), -O (--output), -U (--ignore-unflag), -F (--ignore-flag),\n"
	            "-L (--ignore-filter), -Z (--ignore-zero), -H (--help), -v (--verbose).\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

#define bailout(code) { gmt_M_free_options(mode); return code; }
#define Return(code) { if (T) mb_gmt_text_end(T); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbdumpesf(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbdumpesf(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MB_GMT_TEXT *T = NULL;	/* the text dump, as records */
	int gmt_error;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program, which reports the missing input itself */
	if ((gmt_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(gmt_error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	int verbose = GMT->common.V.active ? 1 : 0;

	/* MBIO read and write control parameters */
	char iesffile[MB_PATH_MAXLINE] = "";
	char oesffile[MB_PATH_MAXLINE] = "";
	bool input_specified = false;
	omode_t omode = OUTPUT_TEXT;
	FILE *iesffp = NULL;
	FILE *oesffp = NULL;

	bool ignore_unflag = false;
	bool ignore_flag = false;
	bool ignore_filter = false;
	bool ignore_zero = false;

	/* process argument list: the program's options, from GMT's option list (lower-case aliases
	   kept -- GMT_Parse_Common only parses the common options in THIS_MODULE_OPTIONS) */
	{
		bool errflg = false;
		bool help = false;
		for (struct GMT_OPTION *opt = options; opt; opt = opt->next) {
			switch (opt->option) {
			case 'H':
			case 'h':
				help = true;
				break;
			case 'V':	/* GMT's -V, already applied */
				break;
			case 'v':
				verbose++;
				break;
			case 'I':
			case 'i':
				sscanf(opt->arg, "%1023s", iesffile);
				input_specified = true;
				break;
			case 'O':
				snprintf(oesffile, sizeof (oesffile), "%s", opt->arg);
				omode = OUTPUT_ESF;
				break;
			case 'U':
				ignore_unflag = true;
				break;
			case 'F':
				ignore_flag = true;
				break;
			case 'L':
				ignore_filter = true;
				break;
			case 'Z':
				ignore_zero = true;
				break;
			default:
				errflg |= (gmt_default_option_error(GMT, opt) != 0);
				break;
			}
		}

		if (errflg)
			Return(GMT_PARSE_ERROR);

		if (help)
			Return(usage(API, GMT_USAGE));

		if (verbose == 1 || help) {
			fprintf(stderr, "\nProgram %s\n", program_name);
			fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
		}

		if (verbose >= 2) {
			fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
			fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(stderr, "dbg2  Control Parameters:\n");
			fprintf(stderr, "dbg2       verbose:          %d\n", verbose);
			fprintf(stderr, "dbg2       help:             %d\n", help);
			fprintf(stderr, "dbg2       input esf file:   %s\n", iesffile);
			fprintf(stderr, "dbg2       omode:            %d\n", omode);
			if (omode == OUTPUT_ESF)
				fprintf(stderr, "dbg2       output esf file:  %s\n", oesffile);
			fprintf(stderr, "dbg2       ignore_unflag:    %d\n", ignore_unflag);
			fprintf(stderr, "dbg2       ignore_flag:      %d\n", ignore_flag);
			fprintf(stderr, "dbg2       ignore_filter:    %d\n", ignore_filter);
			fprintf(stderr, "dbg2       ignore_zero:      %d\n", ignore_zero);
		}
	}

	double time_d;
	int beam;
	int action;

	int beam_flag = 0;
	int beam_unflag = 0;
	int beam_zero = 0;
	int beam_filter = 0;
	int beam_flag_ignore = 0;
	int beam_unflag_ignore = 0;
	int beam_zero_ignore = 0;
	int beam_filter_ignore = 0;

	const int byteswapped = mb_swap_check();

	int error = MB_ERROR_NO_ERROR;

	if (!input_specified) {
		GMT_Report(API, GMT_MSG_ERROR, "No input edit save file specified\n");
		GMT_Report(API, GMT_MSG_ERROR, "usage: %s\n", usage_message);
		Return(GMT_PARSE_ERROR);
	}

	/* the text dump goes out as records (with -O the esf file gets the edits instead) */
	if (omode == OUTPUT_TEXT && (T = mb_gmt_text_begin(GMT, options)) == NULL)
		Return(API->error);

	/* check that esf file exists */
	struct stat file_status;
	const int fstat = stat(iesffile, &file_status);
	if (fstat == 0 && (file_status.st_mode & S_IFMT) != S_IFDIR) {
		/* open the input esf file */
		if ((iesffp = fopen(iesffile, "rb")) == NULL) {
			GMT_Report(API, GMT_MSG_ERROR, "Unable to edit save file <%s> for reading\n", iesffile);
			Return(GMT_ERROR_ON_FOPEN);
		}

		/* open the output esf file */
		if (omode == OUTPUT_ESF && (oesffp = fopen(oesffile, "wb")) == NULL) {
			GMT_Report(API, GMT_MSG_ERROR, "Unable to edit save file <%s> for reading\n", iesffile);
			fclose(iesffp);
			Return(GMT_ERROR_ON_FOPEN);
		}

		/* read file header to discern the format */
		int status = MB_SUCCESS;
		int nedit = 0;
		char esf_header[MB_PATH_MAXLINE * 3];  /* Need more space than an mb_path */
		if (fread(esf_header, MB_PATH_MAXLINE, 1, iesffp) == 1 && strncmp(esf_header, "ESFVERSION", 10) == 0) {
			nedit = (file_status.st_size - MB_PATH_MAXLINE) / (sizeof(double) + 2 * sizeof(int));

			if (omode == OUTPUT_ESF && oesffp != NULL) {
				memset(esf_header, 0, MB_PATH_MAXLINE);
				const int esf_mode = MB_ESF_MODE_EXPLICIT;
        char user[256], host[256], date[32];
        status = mb_user_host_date(verbose, user, host, date, &error);
				snprintf(esf_header, sizeof(esf_header),
				        "ESFVERSION03\nESF Mode: %d\nMB-System Version %s\nProgram: %s\nUser: %s\nCPU: %s\nDate: %s\n",
				        esf_mode, MB_VERSION, program_name, user, host, date);
				if (fwrite(esf_header, MB_PATH_MAXLINE, 1, oesffp) != 1) {
					status = MB_FAILURE;
					error = MB_ERROR_WRITE_FAIL;
				}
			}
		}
		else {
			rewind(iesffp);
			nedit = file_status.st_size / (sizeof(double) + 2 * sizeof(int));
		}

		/* loop over reading edit events and printing them out */
		for (int i = 0; i < nedit && error == MB_ERROR_NO_ERROR; i++) {
			bool ignore = false;
			if (fread(&(time_d), sizeof(double), 1, iesffp) != 1 || fread(&(beam), sizeof(int), 1, iesffp) != 1 ||
			    fread(&(action), sizeof(int), 1, iesffp) != 1) {
				ignore = true;
				status = MB_FAILURE;
				error = MB_ERROR_EOF;
			}
			else if (strncmp(((char *)&time_d), "ESFVERSI", 8) == 0) {
				ignore = true;
				if (fread(esf_header, MB_PATH_MAXLINE-16, 1, iesffp) != 1) {
					status = MB_FAILURE;
					error = MB_ERROR_EOF;
				}
			}
			else if (byteswapped) {
				mb_swap_double(&(time_d));
				beam = mb_swap_int(beam);
				action = mb_swap_int(action);
			}

			if (!ignore) {
				if (action == MBP_EDIT_FLAG) {
					beam_flag++;
					if (ignore_flag) {
						ignore = true;
						beam_flag_ignore++;
					}
				}
				else if (action == MBP_EDIT_UNFLAG) {
					beam_unflag++;
					if (ignore_unflag) {
						ignore = true;
						beam_unflag_ignore++;
					}
				}
				else if (action == MBP_EDIT_ZERO) {
					beam_zero++;
					if (ignore_zero) {
						ignore = true;
						beam_zero_ignore++;
					}
				}
				else if (action == MBP_EDIT_FILTER) {
					beam_filter++;
					if (ignore_filter) {
						ignore = true;
						beam_filter_ignore++;
					}
				}
			}

			/* write out the edit if not ignored */
			if (!ignore) {
				if (omode == OUTPUT_TEXT) {
					int time_i[7];
					mb_get_date(verbose, time_d, time_i);
					mb_gmt_text_put(T,"EDITS READ: i:%d time: %f %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d beam:%d action:%d\n", i,
					        time_d, time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6], beam, action);
				}
				else {
					if (byteswapped) {
						mb_swap_double(&time_d);
						beam = mb_swap_int(beam);
						action = mb_swap_int(action);
					}
					if (fwrite(&time_d, sizeof(double), 1, oesffp) != 1) {
						status = MB_FAILURE;
						error = MB_ERROR_WRITE_FAIL;
					}
					if (status == MB_SUCCESS && fwrite(&beam, sizeof(int), 1, oesffp) != 1) {
						status = MB_FAILURE;
						error = MB_ERROR_WRITE_FAIL;
					}
					if (status == MB_SUCCESS && fwrite(&action, sizeof(int), 1, oesffp) != 1) {
						status = MB_FAILURE;
						error = MB_ERROR_WRITE_FAIL;
					}
				}
			}
      else {
				if (omode == OUTPUT_TEXT) {
					int time_i[7];
					mb_get_date(verbose, time_d, time_i);
					mb_gmt_text_put(T,"** EDITS READ BUT IGNORED **: i:%d time: %f %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d beam:%d action:%d\n", i,
					        time_d, time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6], beam, action);
				}
      }
		}

		fclose(iesffp);
		if (omode == OUTPUT_ESF)
			fclose(oesffp);
	}

	/* give the statistics */
	if (verbose >= 1) {
		fprintf(stderr, "\nBeam flag read totals:\n");
		fprintf(stderr, "\t%d beams flagged manually\n", beam_flag);
		fprintf(stderr, "\t%d beams unflagged\n", beam_unflag);
		fprintf(stderr, "\t%d beams zeroed\n", beam_zero);
		fprintf(stderr, "\t%d beams flagged by filter\n", beam_filter);
		if (ignore_flag || ignore_unflag || ignore_zero || ignore_filter) {
			fprintf(stderr, "\nBeam flag ignore totals:\n");
			fprintf(stderr, "\t%d beams flagged manually (ignored in output)\n", beam_flag_ignore);
			fprintf(stderr, "\t%d beams unflagged (ignored in output)\n", beam_unflag_ignore);
			fprintf(stderr, "\t%d beams zeroed (ignored in output)\n", beam_zero_ignore);
			fprintf(stderr, "\t%d beams flagged by filter (ignored in output)\n", beam_filter_ignore);
		}
	}

	if (T) {
		const int output_failed = mb_gmt_text_end(T);
		T = NULL;	/* closed: Return() must not close it again */
		if (output_failed) Return(GMT_RUNTIME_ERROR);
	}
	/* The program exits with MBIO's error; as a module that is a GMT error code, never an MBIO one */
	if (error != MB_ERROR_NO_ERROR) {
		char *message;
		mb_error(verbose, error, &message);
		GMT_Report(API, GMT_MSG_ERROR, "%s\n", message);
		Return(GMT_RUNTIME_ERROR);
	}
	Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
