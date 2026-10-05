/*--------------------------------------------------------------------
 *    The MB-system:	mbgetesf.c	6/15/93
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
 * mbgetesf reads a multibeam data file and writes out
 * an edit save file which can be applied to other data files
 * containing the same data (but presumably in a different
 * state of processing).  This allows editing of one data file to
 * be transferred to another with ease.  The programs mbedit and
 * mbprocess can be used to apply the edit events to another file.
 *
 * Author:	D. W. Caress
 * Date:	January 24, 2001
 *
 * GMT-module port of src/utilities/mbgetesf.cc: the getopt_long loop
 * is replaced by the GMT option parser (long options are rewritten onto
 * their short forms before GMT sees them) and main() becomes
 * GMT_mbgetesf(), with every exit() turned into Return().
 */

#define THIS_MODULE_NAME "mbgetesf"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Extract beam edit flags from swath data into an edit save file"
/* Primary input is the swath file given with -I. The edit save file is
 * written by the module itself (-O or binary stdout), so no output key. */
#define THIS_MODULE_KEYS "ID{"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
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

typedef enum {
    MBGETESF_FLAGONLY = 1,
    MBGETESF_FLAGNULL = 2,
    MBGETESF_ALL = 3,
    MBGETESF_IMPLICITBEST = 4,
    MBGETESF_IMPLICITNULL = 5,
    MBGETESF_IMPLICITGOOD = 6,
} getesf_mode_t;

static const char program_name[] = "mbgetesf";
static const char help_message[] =
    "mbgetesf reads a multibeam data file and writes out\n"
    "an edit save file which can be applied to other data files\n"
    "containing the same data (but presumably in a different\n"
    "state of processing).  This allows editing of one data file to\n"
    "be transferred to another with ease.  The programs mbedit and\n"
    "mbprocess can be used to apply the edit events to another file.";
static const char usage_message[] =
    "mbgetesf\n"
    "\t--begin-time=yr/mo/da/hr/mn/sc {-Byr/mo/da/hr/mn/sc}\n"
    "\t--end-time=yr/mo/da/hr/mn/sc {-Eyr/mo/da/hr/mn/sc}\n"
    "\t--format=format_id {-Fformat_id}\n"
    "\t--help {-H}\n"
    "\t--input=infile {-Iinfile}\n"
    "\t--kluge=value {-Kvalue}\n"
    "\t--mode=value {-Mvalue}\n"
    "\t--output=esffile {-Oesffile}\n"
    "\t--verbose {-V}\n\n";

/* --- Control structure ---------------------------------------------- */

struct MBGETESF_CTRL {
	struct mbge_B { bool active; char value[MB_PATH_MAXLINE]; } B;
	struct mbge_E { bool active; char value[MB_PATH_MAXLINE]; } E;
	struct mbge_F { bool active; int format; } F;
	struct mbge_H { bool active; } H;
	struct mbge_I { bool active; char file[MB_PATH_MAXLINE]; } I;
	struct mbge_K { bool active; int kluge; } K;
	struct mbge_M { bool active; int mode; } M;
	struct mbge_O { bool active; char file[MB_PATH_MAXLINE]; } O;
};

static void *New_mbgetesf_Ctrl(struct GMT_CTRL *GMT) {
	struct MBGETESF_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBGETESF_CTRL);
	return Ctrl;
}

static void Free_mbgetesf_Ctrl(struct GMT_CTRL *GMT, struct MBGETESF_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_PARSE_ERROR;
	GMT_Message(API, GMT_TIME_NONE, "%s\n\n", help_message);
	GMT_Message(API, GMT_TIME_NONE,
	            "\t-B Begin time as year/month/day/hour/minute/second.\n"
	            "\t-E End time as year/month/day/hour/minute/second.\n"
	            "\t-F Input MBIO format.\n"
	            "\t-I Input swath file [stdin].\n"
	            "\t-K Select a processing kluge (1: shift EM300/EM3000 HDCS beam flags).\n"
	            "\t-M Edit extraction mode (1-6).\n"
	            "\t-O Output edit save file [binary stdout].\n"
	            "\t-H Print help and exit.\n");
	GMT_Option(API, "V");
	return GMT_PARSE_ERROR;
}

static int parse_mbgetesf(struct GMT_CTRL *GMT, struct MBGETESF_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt;
	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case 'A': case 'B': case 'b':	/* -B arrives as -A, see preparse_long_options() */
			if (opt->arg && opt->arg[0]) {
				snprintf(Ctrl->B.value, sizeof(Ctrl->B.value), "%s", opt->arg);
				Ctrl->B.active = true;
			}
			else n_errors++;
			break;
		case 'E': case 'e':
			if (opt->arg && opt->arg[0]) {
				snprintf(Ctrl->E.value, sizeof(Ctrl->E.value), "%s", opt->arg);
				Ctrl->E.active = true;
			}
			else n_errors++;
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
		case 'K': case 'k':
			if (opt->arg && sscanf(opt->arg, "%d", &Ctrl->K.kluge) == 1) Ctrl->K.active = true;
			else n_errors++;
			break;
		case 'M': case 'm':
			if (opt->arg && sscanf(opt->arg, "%d", &Ctrl->M.mode) == 1) Ctrl->M.active = true;
			else n_errors++;
			break;
		case 'O': case 'o':
			if (opt->arg && opt->arg[0]) {
				snprintf(Ctrl->O.file, sizeof(Ctrl->O.file), "%s", opt->arg);
				Ctrl->O.active = true;
			}
			else n_errors++;
			break;
		default:
			n_errors += gmt_default_error(GMT, opt->option);
			break;
		}
	}
	return n_errors ? GMT_PARSE_ERROR : GMT_OK;
}

static char *join_args(int mode, void *args) {
	char **argv = (char **)args, *joined;
	size_t total = 1;
	int i;
	if (mode <= 0 || !args) return NULL;
	for (i = 0; i < mode; i++) total += strlen(argv[i]) + 1;
	joined = (char *)calloc(total, 1);
	if (!joined) return NULL;
	for (i = 0; i < mode; i++) { if (i) strcat(joined, " "); strcat(joined, argv[i]); }
	return joined;
}

/* Rewrite the getopt_long options of mbgetesf.cc onto short options.
 * GMT reserves -B, so the begin time is carried as -A. */
static char *preparse_long_options(bool *help, const char *args) {
	size_t length = args ? strlen(args) : 0, out = 0;
	char *copy = (char *)calloc(length + 2, 1), *result = (char *)calloc(2 * length + 8, 1);
	char *token, *saveptr = NULL, pending = '\0';
	if (!copy || !result) { free(copy); free(result); return NULL; }
	memcpy(copy, args, length);
	for (token = strtok_r(copy, " \t", &saveptr); token; token = strtok_r(NULL, " \t", &saveptr)) {
		char emit = '\0', *equals;
		const char *value = NULL;
		if (pending) {
			if (out) result[out++] = ' ';
			result[out++] = '-'; result[out++] = pending;
			memcpy(result + out, token, strlen(token)); out += strlen(token); pending = '\0'; continue;
		}
		if (strncmp(token, "--", 2) != 0) {
			if (token[0] == '-' && (token[1] == 'B' || token[1] == 'b')) token[1] = 'A';
			if (out) result[out++] = ' ';
			memcpy(result + out, token, strlen(token)); out += strlen(token); continue;
		}
		equals = strchr(token + 2, '=');
		if (equals) { *equals = '\0'; value = equals + 1; }
		if (!strcmp(token + 2, "help")) { *help = true; continue; }
		if (!strcmp(token + 2, "verbose")) emit = 'V';
		else if (!strcmp(token + 2, "begin-time")) emit = 'A';
		else if (!strcmp(token + 2, "end-time")) emit = 'E';
		else if (!strcmp(token + 2, "format")) emit = 'F';
		else if (!strcmp(token + 2, "input")) emit = 'I';
		else if (!strcmp(token + 2, "kluge")) emit = 'K';
		else if (!strcmp(token + 2, "mode")) emit = 'M';
		else if (!strcmp(token + 2, "output")) emit = 'O';
		if (!emit) {
			if (equals) *equals = '=';
			if (out) result[out++] = ' ';
			memcpy(result + out, token, strlen(token)); out += strlen(token);
		} else if (emit == 'V') {
			if (out) result[out++] = ' ';
			result[out++] = '-'; result[out++] = emit;
		} else if (value) {
			if (out) result[out++] = ' ';
			result[out++] = '-'; result[out++] = emit;
			memcpy(result + out, value, strlen(value)); out += strlen(value);
		} else pending = emit;
	}
	free(copy);
	return result;
}

/*--------------------------------------------------------------------*/
static int mbgetesf_save_edit(int verbose, FILE *sofp, double time_d, int beam, int action, int *error) {
	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
		fprintf(stderr, "dbg2  Input arguments:\n");

		fprintf(stderr, "dbg2       sofp:            %p\n", (void *)sofp);
		fprintf(stderr, "dbg2       time_d:          %f\n", time_d);
		fprintf(stderr, "dbg2       beam:            %d\n", beam);
		fprintf(stderr, "dbg2       action:          %d\n", action);
	}

	int status = MB_SUCCESS;

	/* write out the edit */
	if (sofp != NULL) {
#ifdef BYTESWAPPED
		mb_swap_double(&time_d);
		beam = mb_swap_int(beam);
		action = mb_swap_int(action);
#endif
		if (fwrite(&time_d, sizeof(double), 1, sofp) != 1) {
			status = MB_FAILURE;
			*error = MB_ERROR_WRITE_FAIL;
		}
		if (status == MB_SUCCESS && fwrite(&beam, sizeof(int), 1, sofp) != 1) {
			status = MB_FAILURE;
			*error = MB_ERROR_WRITE_FAIL;
		}
		if (status == MB_SUCCESS && fwrite(&action, sizeof(int), 1, sofp) != 1) {
			status = MB_FAILURE;
			*error = MB_ERROR_WRITE_FAIL;
		}
	}

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
		fprintf(stderr, "dbg2  Return values:\n");
		fprintf(stderr, "dbg2       error:       %d\n", *error);
		fprintf(stderr, "dbg2  Return status:\n");
		fprintf(stderr, "dbg2       status:      %d\n", status);
	}

	return (status);
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code) { free(remaining_args); Free_mbgetesf_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbgetesf(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbgetesf(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MBGETESF_CTRL *Ctrl = NULL;
	char *remaining_args = NULL;
	bool staged_help = false;
	int parse_error;

	if (!API) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	{
		char *joined = join_args(mode, args);
		const char *text = joined ? joined : (mode == GMT_MODULE_CMD ? (const char *)args : NULL);
		if (text) remaining_args = preparse_long_options(&staged_help, text);
		free(joined);
	}
	options = GMT_Create_Options(API, remaining_args ? GMT_MODULE_CMD : mode, remaining_args ? (void *)remaining_args : args);
	if (API->error) { free(remaining_args); return API->error; }
	if (!options || options->option == GMT_OPT_USAGE) { free(remaining_args); bailout(usage(API, GMT_USAGE)); }
	if (options->option == GMT_OPT_SYNOPSIS) { free(remaining_args); bailout(usage(API, GMT_SYNOPSIS)); }
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, NULL, &options, &GMT_cpy)) == NULL) { free(remaining_args); bailout(API->error); }
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = (struct MBGETESF_CTRL *)New_mbgetesf_Ctrl(GMT);
	Ctrl->H.active = staged_help;
	if ((parse_error = parse_mbgetesf(GMT, Ctrl, options)) != GMT_OK) Return(parse_error);

	int verbose = GMT->common.V.active;
	int format;
	int pings;
	int lonflip;
	double bounds[4];
	int btime_i[7];
	int etime_i[7];
	double speedmin;
	double timegap;
	int status = mb_defaults(verbose, &format, &pings, &lonflip, bounds, btime_i, etime_i, &speedmin, &timegap);

	/* reset all defaults but the format and lonflip */
	pings = 1;
	bounds[0] = -360.;
	bounds[1] = 360.;
	bounds[2] = -90.;
	bounds[3] = 90.;
	btime_i[0] = 1962;
	btime_i[1] = 2;
	btime_i[2] = 21;
	btime_i[3] = 10;
	btime_i[4] = 30;
	btime_i[5] = 0;
	btime_i[6] = 0;
	etime_i[0] = 2062;
	etime_i[1] = 2;
	etime_i[2] = 21;
	etime_i[3] = 10;
	etime_i[4] = 30;
	etime_i[5] = 0;
	etime_i[6] = 0;
	speedmin = 0.0;
	timegap = 1000000000.0;

	getesf_mode_t getesf_mode = MBGETESF_FLAGONLY;
	char ifile[MB_PATH_MAXLINE] = "stdin";
	int kluge = 0;
	bool sofile_set = false;
	mb_path sofile = "";

	{
		bool help = Ctrl->H.active;

		if (Ctrl->B.active) {
			sscanf(Ctrl->B.value, "%d/%d/%d/%d/%d/%d", &btime_i[0], &btime_i[1], &btime_i[2], &btime_i[3], &btime_i[4], &btime_i[5]);
			btime_i[6] = 0;
		}
		if (Ctrl->E.active) {
			sscanf(Ctrl->E.value, "%d/%d/%d/%d/%d/%d", &etime_i[0], &etime_i[1], &etime_i[2], &etime_i[3], &etime_i[4], &etime_i[5]);
			etime_i[6] = 0;
		}
		if (Ctrl->F.active)
			format = Ctrl->F.format;
		if (Ctrl->I.active)
			sscanf(Ctrl->I.file, "%1023s", ifile);
		if (Ctrl->K.active)
			kluge = Ctrl->K.kluge;
		if (Ctrl->M.active)
			getesf_mode = (getesf_mode_t)Ctrl->M.mode;  // TODO(schwehr): Range check
		if (Ctrl->O.active) {
			sscanf(Ctrl->O.file, "%1023s", sofile);
			sofile_set = true;
		}

		if (verbose == 1 || help) {
			fprintf(stderr, "\nProgram %s\n", program_name);
			fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
		}

		if (verbose >= 2) {
			fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
			fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(stderr, "dbg2  Control Parameters:\n");
			fprintf(stderr, "dbg2       verbose:        %d\n", verbose);
			fprintf(stderr, "dbg2       help:           %d\n", help);
			fprintf(stderr, "dbg2       data format:    %d\n", format);
			fprintf(stderr, "dbg2       pings:          %d\n", pings);
			fprintf(stderr, "dbg2       lonflip:        %d\n", lonflip);
			fprintf(stderr, "dbg2       bounds[0]:      %f\n", bounds[0]);
			fprintf(stderr, "dbg2       bounds[1]:      %f\n", bounds[1]);
			fprintf(stderr, "dbg2       bounds[2]:      %f\n", bounds[2]);
			fprintf(stderr, "dbg2       bounds[3]:      %f\n", bounds[3]);
			fprintf(stderr, "dbg2       btime_i[0]:     %d\n", btime_i[0]);
			fprintf(stderr, "dbg2       btime_i[1]:     %d\n", btime_i[1]);
			fprintf(stderr, "dbg2       btime_i[2]:     %d\n", btime_i[2]);
			fprintf(stderr, "dbg2       btime_i[3]:     %d\n", btime_i[3]);
			fprintf(stderr, "dbg2       btime_i[4]:     %d\n", btime_i[4]);
			fprintf(stderr, "dbg2       btime_i[5]:     %d\n", btime_i[5]);
			fprintf(stderr, "dbg2       btime_i[6]:     %d\n", btime_i[6]);
			fprintf(stderr, "dbg2       etime_i[0]:     %d\n", etime_i[0]);
			fprintf(stderr, "dbg2       etime_i[1]:     %d\n", etime_i[1]);
			fprintf(stderr, "dbg2       etime_i[2]:     %d\n", etime_i[2]);
			fprintf(stderr, "dbg2       etime_i[3]:     %d\n", etime_i[3]);
			fprintf(stderr, "dbg2       etime_i[4]:     %d\n", etime_i[4]);
			fprintf(stderr, "dbg2       etime_i[5]:     %d\n", etime_i[5]);
			fprintf(stderr, "dbg2       etime_i[6]:     %d\n", etime_i[6]);
			fprintf(stderr, "dbg2       speedmin:       %f\n", speedmin);
			fprintf(stderr, "dbg2       timegap:        %f\n", timegap);
			fprintf(stderr, "dbg2       input file:     %s\n", ifile);
			fprintf(stderr, "dbg2       mode:	   %d\n", getesf_mode);
			fprintf(stderr, "dbg2       kluge:	   %d\n", kluge);
		}

		if (help) {
			fprintf(stderr, "\n%s\n", help_message);
			fprintf(stderr, "\nusage: %s\n", usage_message);
			Return(MB_ERROR_NO_ERROR);
		}
	}

	int error = MB_ERROR_NO_ERROR;

	if (format == 0)
		mb_get_format(verbose, ifile, NULL, &format, &error);

	void *imbio_ptr = NULL;
	double btime_d;
	double etime_d;
	int beams_bath;
	int beams_amp;
	int pixels_ss;

	if (mb_read_init(verbose, ifile, format, pings, lonflip, bounds, btime_i, etime_i, speedmin, timegap, &imbio_ptr,
	                           &btime_d, &etime_d, &beams_bath, &beams_amp, &pixels_ss, &error) != MB_SUCCESS) {
		char *message = NULL;
		mb_error(verbose, error, &message);
		fprintf(stderr, "\nMBIO Error returned from function <mb_read_init>:\n%s\n", message);
		fprintf(stderr, "\nMultibeam File <%s> not initialized for reading\n", ifile);
		fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
		Return(error);
	}

	/* allocate memory for data arrays */
	char *beamflag = NULL;
	if (error == MB_ERROR_NO_ERROR)
		status &= mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(char), (void **)&beamflag, &error);
	double *bath = NULL;
	if (error == MB_ERROR_NO_ERROR)
		status &= mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bath, &error);
	double *amp = NULL;
	if (error == MB_ERROR_NO_ERROR)
		status &= mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_AMPLITUDE, sizeof(double), (void **)&amp, &error);
	double *bathacrosstrack = NULL;
	if (error == MB_ERROR_NO_ERROR)
		status &= mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathacrosstrack, &error);
	double *bathalongtrack = NULL;
	if (error == MB_ERROR_NO_ERROR)
		status &= mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathalongtrack, &error);
	double *ss = NULL;
	if (error == MB_ERROR_NO_ERROR)
		status &= mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ss, &error);
	double *ssacrosstrack = NULL;
	if (error == MB_ERROR_NO_ERROR)
		status &= mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssacrosstrack, &error);
	double *ssalongtrack = NULL;
	if (error == MB_ERROR_NO_ERROR)
		status &= mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssalongtrack, &error);

	/* if error initializing memory then quit */
	if (error != MB_ERROR_NO_ERROR) {
		char *message = NULL;
		mb_error(verbose, error, &message);
		fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
		fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
		mb_close(verbose, &imbio_ptr, &error);
		Return(error);
	}

	/* save file control variables */
	FILE *sofp = NULL;

	/* now deal with new edit save file */
	if (status == MB_SUCCESS) {
		/* get edit save file */
		if (!sofile_set) {
			/* the edit save file is binary: bypass the C runtime's CRLF translation */
#ifdef _WIN32
			_setmode(_fileno(stdout), _O_BINARY);
#endif
			sofp = stdout;
		}
		else if ((sofp = fopen(sofile, "wb")) == NULL) {
			error = MB_ERROR_OPEN_FAIL;
			char *message = NULL;
			mb_error(verbose, error, &message);
			fprintf(stderr, "\nEdit Save File <%s> not initialized for writing\n", sofile);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			mb_close(verbose, &imbio_ptr, &error);
			Return(MB_ERROR_OPEN_FAIL);
		}
	}

	mb_path esf_header;
	int esf_mode = MB_ESF_MODE_EXPLICIT;

	/* put version header at beginning */
	if (status == MB_SUCCESS) {
		memset(esf_header, 0, MB_PATH_MAXLINE);
		if (getesf_mode == MBGETESF_IMPLICITBEST) {
			if (format == MBF_3DWISSLR || format == MBF_3DWISSLP) {
				esf_mode = MB_ESF_MODE_IMPLICIT_NULL;
			}
			else {
				esf_mode = MB_ESF_MODE_IMPLICIT_GOOD;
			}
		}
		else if (getesf_mode == MBGETESF_IMPLICITNULL)
			esf_mode = MB_ESF_MODE_IMPLICIT_NULL;
		else if (getesf_mode == MBGETESF_IMPLICITGOOD)
			esf_mode = MB_ESF_MODE_IMPLICIT_GOOD;
		else
			esf_mode = MB_ESF_MODE_EXPLICIT;
		char user[256], host[256], date[32];
		status = mb_user_host_date(verbose, user, host, date, &error);
		snprintf(esf_header, sizeof(esf_header),
				"ESFVERSION03\nESF Mode: %d\nMB-System Version %s\nProgram: %s\nUser: %s\nCPU: %s\nDate: %s\n",
				esf_mode, MB_VERSION, program_name, user, host, date);
		if (fwrite(esf_header, MB_PATH_MAXLINE, 1, sofp) != 1) {
			status = MB_FAILURE;
			error = MB_ERROR_WRITE_FAIL;
		}
	}

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
	int nbath;
	int namp;
	int nss;
	int idata = 0;
	int beam_ok = 0;
	int beam_null = 0;
	int beam_ok_write = 0;
	int beam_null_write = 0;
	int beam_flag = 0;
	int beam_flag_manual = 0;
	int beam_flag_filter = 0;
	int beam_flag_sonar = 0;
	char comment[MB_COMMENT_MAXLINE];

	/* read and write */
	while (error <= MB_ERROR_NO_ERROR) {
		/* read some data */
		error = MB_ERROR_NO_ERROR;
		status = MB_SUCCESS;
		status = mb_get_all(verbose, imbio_ptr, &store_ptr, &kind, time_i, &time_d, &navlon, &navlat, &speed, &heading, &distance,
		                    &altitude, &sensordepth, &nbath, &namp, &nss, beamflag, bath, amp, bathacrosstrack, bathalongtrack, ss,
		                    ssacrosstrack, ssalongtrack, comment, &error);

		/* increment counter */
		if (error <= MB_ERROR_NO_ERROR && kind == MB_DATA_DATA)
			idata = idata + pings;

		/* time gaps do not matter to mbgetesf */
		if (error == MB_ERROR_TIME_GAP) {
			status = MB_SUCCESS;
			error = MB_ERROR_NO_ERROR;
		}

		/* time bounds do not matter to mbgetesf */
		if (error == MB_ERROR_OUT_TIME) {
			status = MB_SUCCESS;
			error = MB_ERROR_NO_ERROR;
		}

		/* space bounds do not matter to mbgetesf */
		if (error == MB_ERROR_OUT_BOUNDS) {
			status = MB_SUCCESS;
			error = MB_ERROR_NO_ERROR;
		}

		/* output error messages */
		if (verbose >= 1 && error < MB_ERROR_NO_ERROR && error >= MB_ERROR_OTHER && error != MB_ERROR_COMMENT) {
			char *message = NULL;
			mb_error(verbose, error, &message);
			fprintf(stderr, "\nNonfatal MBIO Error:\n%s\n", message);
			fprintf(stderr, "Input Record: %d\n", idata);
			fprintf(stderr, "Time: %d %d %d %d %d %d %d\n", time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5],
			        time_i[6]);
		}
		else if (verbose >= 1 && error < MB_ERROR_NO_ERROR) {
			char *message = NULL;
			mb_error(verbose, error, &message);
			fprintf(stderr, "\nNonfatal MBIO Error:\n%s\n", message);
			fprintf(stderr, "Number of good records so far: %d\n", idata);
		}
		else if (verbose >= 1 && error != MB_ERROR_NO_ERROR && error != MB_ERROR_EOF) {
			char *message = NULL;
			mb_error(verbose, error, &message);
			fprintf(stderr, "\nFatal MBIO Error:\n%s\n", message);
			fprintf(stderr, "Last Good Time: %d %d %d %d %d %d %d\n", time_i[0], time_i[1], time_i[2], time_i[3], time_i[4],
			        time_i[5], time_i[6]);
		}

		/* deal with data without errors */
		if (status == MB_SUCCESS && kind == MB_DATA_DATA) {
			/* fix a problem with EM300/EM3000 data in HDCS format */
			if (format == 151 && kluge == 1 && nbath > 0) {
				for (int i = 0; i < nbath - 1; i++)
					beamflag[i] = beamflag[i + 1];
				beamflag[nbath - 1] = MB_FLAG_FLAG;
			}

			/* count and write the flags */
			for (int i = 0; i < nbath; i++) {
				if (mb_beam_ok(beamflag[i])) {
					beam_ok++;
					if (getesf_mode == MBGETESF_ALL
						|| esf_mode == MB_ESF_MODE_IMPLICIT_NULL) {
						mbgetesf_save_edit(verbose, sofp, time_d, i, MBP_EDIT_UNFLAG, &error);
						beam_ok_write++;
					}
				}
				else if (mb_beam_check_flag_unusable(beamflag[i])) {
					beam_null++;
					if (getesf_mode == MBGETESF_ALL || getesf_mode == MBGETESF_FLAGNULL
						|| esf_mode == MB_ESF_MODE_IMPLICIT_GOOD) {
						mbgetesf_save_edit(verbose, sofp, time_d, i, MBP_EDIT_ZERO, &error);
						beam_null_write++;
					}
				}
				else {
					beam_flag++;
					if (mb_beam_check_flag_manual(beamflag[i])) {
						beam_flag_manual++;
						mbgetesf_save_edit(verbose, sofp, time_d, i, MBP_EDIT_FLAG, &error);
					}
					if (mb_beam_check_flag_filter(beamflag[i])) {
						beam_flag_filter++;
						mbgetesf_save_edit(verbose, sofp, time_d, i, MBP_EDIT_FILTER, &error);
					}
					if (mb_beam_check_flag_sonar(beamflag[i])) {
						beam_flag_sonar++;
						mbgetesf_save_edit(verbose, sofp, time_d, i, MBP_EDIT_SONAR, &error);
					}
				}
			}
		}
	}

	status = mb_close(verbose, &imbio_ptr, &error);
	/* close edit save file - but never the host's stdout, which the
	 * GMT session and any later module still need */
	if (sofp == stdout)
		fflush(sofp);
	else if (sofp != NULL)
		fclose(sofp);

	/* check memory */
	if (verbose >= 4)
		status = mb_memory_list(verbose, &error);

	/* give the statistics */
	if (verbose >= 1) {
		if (getesf_mode == MBGETESF_FLAGONLY)
			fprintf(stderr, "\nMBgetesf mode: Output beam flags of flagged beams\n");
		else if (getesf_mode == MBGETESF_FLAGNULL)
			fprintf(stderr, "\nMBgetesf mode: Output beam flags of flagged and null beams\n");
		else if (getesf_mode == MBGETESF_ALL)
			fprintf(stderr, "\nMBgetesf mode: Output beam flags of all beams\n");
		else if (getesf_mode == MBGETESF_IMPLICITBEST)
			fprintf(stderr, "\nMBgetesf mode: Output beam flags of flagged and good or null beams with null or good beams implicit (according to format)\n");
		else if (getesf_mode == MBGETESF_IMPLICITNULL)
			fprintf(stderr, "\nMBgetesf mode: Output beam flags of flagged and good beams with null beams implicit\n");
		else if (getesf_mode == MBGETESF_IMPLICITGOOD)
			fprintf(stderr, "\nMBgetesf mode: Output beam flags of flagged and null beams with good beams implicitbeams\n");
		fprintf(stderr, "\nData records:\n");
		fprintf(stderr, "\t%d input data records\n", idata);
		fprintf(stderr, "\nBeam flag read totals:\n");
		fprintf(stderr, "\t%d beams ok\n", beam_ok);
		fprintf(stderr, "\t%d beams null\n", beam_null);
		fprintf(stderr, "\t%d beams flagged\n", beam_flag);
		fprintf(stderr, "\t\t%d beams flagged manually\n", beam_flag_manual);
		fprintf(stderr, "\t\t%d beams flagged by filter\n", beam_flag_filter);
		fprintf(stderr, "\t\t%d beams flagged by sonar\n", beam_flag_sonar);
		if (esf_mode == MB_ESF_MODE_IMPLICIT_NULL)
			fprintf(stderr, "\nESF mode: implicit NULL beams\n");
		else if (esf_mode == MB_ESF_MODE_IMPLICIT_GOOD)
			fprintf(stderr, "\nESF mode: implicit GOOD beams\n");
		else
			fprintf(stderr, "\nESF mode: no implicit beams\n");
		fprintf(stderr, "Beam flag write totals:\n");
		fprintf(stderr, "\t%d beams ok\n", beam_ok_write);
		fprintf(stderr, "\t%d beams null\n", beam_null_write);
		fprintf(stderr, "\t%d beams flagged\n", beam_flag);
		fprintf(stderr, "\t\t%d beams flagged manually\n", beam_flag_manual);
		fprintf(stderr, "\t\t%d beams flagged by filter\n", beam_flag_filter);
		fprintf(stderr, "\t\t%d beams flagged by sonar\n", beam_flag_sonar);
	}

	Return(error);
}
/*--------------------------------------------------------------------*/
