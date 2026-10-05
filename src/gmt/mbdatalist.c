/*--------------------------------------------------------------------
 *    The MB-system:	mbdatalist.c	10/10/2001
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
 * MBdatalist parses recursive datalist files and outputs the
 * complete list of data files and formats.
 * The results are dumped to stdout.
 *
 * Author:	D. W. Caress
 * Date:	October 10, 2001
 *
 * GMT-module port of src/utilities/mbdatalist.cc: the getopt_long loop
 * is replaced by the GMT option parser (long options are rewritten onto
 * their short forms before GMT sees them) and main() becomes
 * GMT_mbdatalist(), with every exit() turned into Return().
 */

#define THIS_MODULE_NAME "mbdatalist"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Parse recursive MB-System datalists and report their contents"
/* Primary input is the swath file or datalist given with -I; the listing goes to stdout. */
#define THIS_MODULE_KEYS "ID{,>D}"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include "unistd_w.h"
#else
#include <unistd.h>
#endif

#include <sys/stat.h>
#ifdef _MSC_VER
#include "dirent_w.h"
#else
#include <dirent.h>
#endif
#include "mb_define.h"
#include "mb_format.h"
#include "mb_process.h"
#include "mb_status.h"

static const char program_name[] = "mbdatalist";
static const char help_message[] =
    "mbdatalist parses recursive datalist files and outputs the\n"
    "complete list of data files and formats. The results are dumped to stdout.";
static const char usage_message[] =
    "mbdatalist\n\t[\n\t--verbose {-V}\n\t--help {-H}\n\t--copy {-C}\n\t--report {-D}\n"
    "\t--format=format_id {-Fformat_id}\n\t--input=file {-Ifile}\n\t--make-ancillary {-N}\n"
    "\t--update-ancillary {-O}\n\t--processed {-P}\n\t--problem {-Q}\n"
    "\t--bounds=w/e/s/n {-Rw/e/s/n}\n\t--status\n"
    "\t--raw {-U}\n\t--unlock {-Y}\n\t--datalistp {-Z}\n";

/* --- Control structure ---------------------------------------------- */

struct MBDATALIST_CTRL {
	struct mbdl_C { bool active; } C;
	struct mbdl_D { bool active; } D;
	struct mbdl_F { bool active; int format; } F;
	struct mbdl_H { bool active; } H;
	struct mbdl_I { bool active; char file[MB_PATH_MAXLINE]; } I;
	struct mbdl_N { bool active; } N;
	struct mbdl_O { bool active; } O;
	struct mbdl_P { bool active; } P;
	struct mbdl_Q { bool active; } Q;
	struct mbdl_R { bool active; char bounds[MB_PATH_MAXLINE]; } R;
	struct mbdl_S { bool active; } S;
	struct mbdl_U { bool active; } U;
	struct mbdl_Y { bool active; } Y;
	struct mbdl_Z { bool active; } Z;
	/* -P and -U both set look_processed; as with getopt the last one given wins */
	int look_processed;
};

static void *New_mbdatalist_Ctrl(struct GMT_CTRL *GMT) {
	struct MBDATALIST_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBDATALIST_CTRL);
	Ctrl->look_processed = MB_DATALIST_LOOK_UNSET;
	return Ctrl;
}

static void Free_mbdatalist_Ctrl(struct GMT_CTRL *GMT, struct MBDATALIST_CTRL *Ctrl) {
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
	            "\t-C Copy listed data files into the current directory.\n"
	            "\t-D Report recursive datalists rather than data files.\n"
	            "\t-F Set or override the MBIO format.\n"
	            "\t-I Input swath file or datalist [datalist.mb-1].\n"
	            "\t-N Force creation of ancillary files.\n"
	            "\t-O Create or update ancillary files.\n"
	            "\t-P Prefer processed files.\n"
	            "\t-Q Report parameter and data problems.\n"
	            "\t-R Only list files intersecting w/e/s/n.\n"
	            "\t-S Append processing and lock status.\n"
	            "\t-U Prefer raw files.\n"
	            "\t-Y Remove lock files.\n"
	            "\t-Z Create a processed-file convenience datalist.\n"
	            "\t-H Print help and exit.\n");
	GMT_Option(API, "V");
	return GMT_PARSE_ERROR;
}

static int parse_mbdatalist(struct GMT_CTRL *GMT, struct MBDATALIST_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt;
	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case 'C': case 'c':
			Ctrl->C.active = true;
			break;
		case 'D': case 'd':
			Ctrl->D.active = true;
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
		case 'N': case 'n':
			Ctrl->N.active = true;
			break;
		case 'O': case 'o':
			Ctrl->O.active = true;
			break;
		case 'P': case 'p': case 'A':	/* -P arrives as -A, see preparse_long_options() */
			Ctrl->P.active = true;
			Ctrl->look_processed = MB_DATALIST_LOOK_YES;
			break;
		case 'Q': case 'q':
			Ctrl->Q.active = true;
			break;
		case 'R': case 'r':
			if (opt->arg && opt->arg[0]) {
				snprintf(Ctrl->R.bounds, sizeof(Ctrl->R.bounds), "%s", opt->arg);
				Ctrl->R.active = true;
			}
			else n_errors++;
			break;
		case 'S': case 's':
			Ctrl->S.active = true;
			break;
		case 'U': case 'u': case 'E':	/* -U arrives as -E, see preparse_long_options() */
			Ctrl->U.active = true;
			Ctrl->look_processed = MB_DATALIST_LOOK_NO;
			break;
		case 'Y': case 'y':
			Ctrl->Y.active = true;
			break;
		case 'Z': case 'z':
			Ctrl->Z.active = true;
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

/* Rewrite the getopt_long options of mbdatalist.cc onto short options.
 * GMT reserves -P and -U, so those are carried as -A and -E (not -B,
 * which GMT also intercepts: "Found no history for option -B"). */
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
			if (token[0] == '-' && (token[1] == 'P' || token[1] == 'p')) token[1] = 'A';
			if (token[0] == '-' && (token[1] == 'U' || token[1] == 'u')) token[1] = 'E';
			if (out) result[out++] = ' ';
			memcpy(result + out, token, strlen(token)); out += strlen(token); continue;
		}
		equals = strchr(token + 2, '=');
		if (equals) { *equals = '\0'; value = equals + 1; }
		if (!strcmp(token + 2, "help")) { *help = true; continue; }
		if (!strcmp(token + 2, "verbose")) emit = 'V';
		else if (!strcmp(token + 2, "copy")) emit = 'C';
		else if (!strcmp(token + 2, "report")) emit = 'D';
		else if (!strcmp(token + 2, "format")) emit = 'F';
		else if (!strcmp(token + 2, "input")) emit = 'I';
		else if (!strcmp(token + 2, "make-ancillary") || !strcmp(token + 2, "make-ancilliary")) emit = 'N';
		else if (!strcmp(token + 2, "update-ancillary") || !strcmp(token + 2, "update-ancilliary")) emit = 'O';
		else if (!strcmp(token + 2, "processed")) emit = 'A';
		else if (!strcmp(token + 2, "problem")) emit = 'Q';
		else if (!strcmp(token + 2, "bounds")) emit = 'R';
		else if (!strcmp(token + 2, "status")) emit = 'S';
		else if (!strcmp(token + 2, "raw")) emit = 'E';
		else if (!strcmp(token + 2, "unlock")) emit = 'Y';
		else if (!strcmp(token + 2, "datalistp")) emit = 'Z';
		if (!emit) {
			if (equals) *equals = '=';
			if (out) result[out++] = ' ';
			memcpy(result + out, token, strlen(token)); out += strlen(token);
		} else if (strchr("VCDNOAQSEYZ", emit)) {
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
/* Copy a swath file and its ancillary files (every file in its directory
 * whose name starts with the swath file name, as "cp file* ." did) into
 * the current directory. Done in-process: there is no "cp" on Windows.
 * Nothing is copied when the file already lives in the current directory. */
static void mbdatalist_copy_file_family(FILE *output, const char *file) {
	const char *slash = strrchr(file, '/');
#ifdef _WIN32
	const char *bslash = strrchr(file, '\\');
	if (bslash != NULL && (slash == NULL || bslash > slash))
		slash = bslash;
#endif
	if (slash == NULL)
		return;
	char dir[MB_PATH_MAXLINE];
	const size_t dirlen = (size_t)(slash - file);
	if (dirlen == 0 || dirlen >= sizeof(dir))
		return;
	memcpy(dir, file, dirlen);
	dir[dirlen] = '\0';
	if (strcmp(dir, ".") == 0)
		return;
	const char *root = slash + 1;
	const size_t rootlen = strlen(root);
	DIR *dp = opendir(dir);
	if (dp == NULL) {
		fprintf(output, "Unable to open directory %s\n", dir);
		return;
	}
	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		if (strncmp(de->d_name, root, rootlen) != 0)
			continue;
		char src[2 * MB_PATH_MAXLINE];
		snprintf(src, sizeof(src), "%s/%s", dir, de->d_name);
		struct stat st;
		if (stat(src, &st) != 0 || (st.st_mode & S_IFMT) != S_IFREG)
			continue;
		FILE *in = fopen(src, "rb");
		if (in == NULL)
			continue;
		FILE *out = fopen(de->d_name, "wb");
		if (out == NULL) {
			fprintf(output, "Unable to copy %s\n", src);
			fclose(in);
			continue;
		}
		char buf[65536];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
			if (fwrite(buf, 1, n, out) != n) {
				fprintf(output, "Unable to copy %s\n", src);
				break;
			}
		}
		fclose(out);
		fclose(in);
	}
	closedir(dp);
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code) { free(remaining_args); Free_mbdatalist_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbdatalist(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbdatalist(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MBDATALIST_CTRL *Ctrl = NULL;
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
	/* no arguments is a valid run: mbdatalist lists datalist.mb-1 */
	if (options && options->option == GMT_OPT_USAGE) { free(remaining_args); bailout(usage(API, GMT_USAGE)); }
	if (options && options->option == GMT_OPT_SYNOPSIS) { free(remaining_args); bailout(usage(API, GMT_SYNOPSIS)); }
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, NULL, &options, &GMT_cpy)) == NULL) { free(remaining_args); bailout(API->error); }
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = (struct MBDATALIST_CTRL *)New_mbdatalist_Ctrl(GMT);
	Ctrl->H.active = staged_help;
	if ((parse_error = parse_mbdatalist(GMT, Ctrl, options)) != GMT_OK) Return(parse_error);

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

	/* set default input to stdin */
	char read_file[MB_PATH_MAXLINE] = "datalist.mb-1";

	bool copyfiles = false;
	bool force_update = false;
	bool make_inf = false;
	int look_processed = MB_DATALIST_LOOK_UNSET;
	bool problem_report = false;
	bool look_bounds = false;
	bool status_report = false;
	bool remove_locks = false;
	bool make_datalistp = false;
	bool reportdatalists = false;
	FILE *output = NULL;

	{
		bool help = Ctrl->H.active;

		/* short options (deprecated) */
		if (Ctrl->C.active)
			copyfiles = true;
		if (Ctrl->D.active)
			reportdatalists = true;
		if (Ctrl->F.active)
			format = Ctrl->F.format;
		if (Ctrl->I.active)
			sscanf(Ctrl->I.file, "%1023s", read_file);
		if (Ctrl->N.active) {
			force_update = true;
			make_inf = true;
		}
		if (Ctrl->O.active)
			make_inf = true;
		if (Ctrl->P.active || Ctrl->U.active)
			look_processed = Ctrl->look_processed;
		if (Ctrl->Q.active)
			problem_report = true;
		if (Ctrl->R.active) {
			mb_get_bounds(Ctrl->R.bounds, bounds);
			look_bounds = true;
		}
		if (Ctrl->S.active)
			status_report = true;
		if (Ctrl->Y.active)
			remove_locks = true;
		if (Ctrl->Z.active)
			make_datalistp = true;

		if (verbose <= 1)
			output = stdout;
		else
			output = stderr;

		if (verbose == 1 || help) {
			fprintf(output, "\nProgram %s\n", program_name);
			fprintf(output, "MB-system Version %s\n", MB_VERSION);
		}

		if (verbose >= 2) {
			fprintf(output, "\ndbg2  Program <%s>\n", program_name);
			fprintf(output, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(output, "dbg2  Control Parameters:\n");
			fprintf(output, "dbg2       verbose:             %d\n", verbose);
			fprintf(output, "dbg2       help:                %d\n", help);
			fprintf(output, "dbg2       file:                %s\n", read_file);
			fprintf(output, "dbg2       format:              %d\n", format);
			fprintf(output, "dbg2       look_processed:      %d\n", look_processed);
			fprintf(output, "dbg2       copyfiles:           %d\n", copyfiles);
			fprintf(output, "dbg2       reportdatalists:     %d\n", reportdatalists);
			fprintf(output, "dbg2       make_inf:            %d\n", make_inf);
			fprintf(output, "dbg2       force_update:        %d\n", force_update);
			fprintf(output, "dbg2       status_report:       %d\n", status_report);
			fprintf(output, "dbg2       problem_report:      %d\n", problem_report);
			fprintf(output, "dbg2       make_datalistp:      %d\n", make_datalistp);
			fprintf(output, "dbg2       remove_locks:        %d\n", remove_locks);
			fprintf(output, "dbg2       pings:               %d\n", pings);
			fprintf(output, "dbg2       lonflip:             %d\n", lonflip);
			fprintf(output, "dbg2       bounds[0]:           %f\n", bounds[0]);
			fprintf(output, "dbg2       bounds[1]:           %f\n", bounds[1]);
			fprintf(output, "dbg2       bounds[2]:           %f\n", bounds[2]);
			fprintf(output, "dbg2       bounds[3]:           %f\n", bounds[3]);
			fprintf(output, "dbg2       btime_i[0]:          %d\n", btime_i[0]);
			fprintf(output, "dbg2       btime_i[1]:          %d\n", btime_i[1]);
			fprintf(output, "dbg2       btime_i[2]:          %d\n", btime_i[2]);
			fprintf(output, "dbg2       btime_i[3]:          %d\n", btime_i[3]);
			fprintf(output, "dbg2       btime_i[4]:          %d\n", btime_i[4]);
			fprintf(output, "dbg2       btime_i[5]:          %d\n", btime_i[5]);
			fprintf(output, "dbg2       btime_i[6]:          %d\n", btime_i[6]);
			fprintf(output, "dbg2       etime_i[0]:          %d\n", etime_i[0]);
			fprintf(output, "dbg2       etime_i[1]:          %d\n", etime_i[1]);
			fprintf(output, "dbg2       etime_i[2]:          %d\n", etime_i[2]);
			fprintf(output, "dbg2       etime_i[3]:          %d\n", etime_i[3]);
			fprintf(output, "dbg2       etime_i[4]:          %d\n", etime_i[4]);
			fprintf(output, "dbg2       etime_i[5]:          %d\n", etime_i[5]);
			fprintf(output, "dbg2       etime_i[6]:          %d\n", etime_i[6]);
			fprintf(output, "dbg2       speedmin:            %f\n", speedmin);
			fprintf(output, "dbg2       timegap:             %f\n", timegap);
		}

		if (help) {
			fprintf(output, "\n%s\n", help_message);
			fprintf(output, "\nusage: %s\n", usage_message);
			Return(MB_ERROR_NO_ERROR);
		}
	}

	int error = MB_ERROR_NO_ERROR;

	/* output stream for basic stuff (stdout if verbose <= 1,
	    output if verbose > 1) */

	if (make_datalistp) {
		/* figure out data format and fileroot if possible */
		char fileroot[MB_PATH_MAXLINE] = {0};
		status = mb_get_format(verbose, read_file, fileroot, &format, &error);
		if (strlen(fileroot) >= MB_PATH_MAXLINE - 6) {
			fprintf(stderr, "\nFile root too long: %s\n", fileroot);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			Return(MB_ERROR_BAD_PARAMETER);
		}
		char file[MB_PATH_MAXLINE+10];
		snprintf(file, sizeof(file), "%sp.mb-1", fileroot);

		FILE *fp = fopen(file, "w");
		if (fp == NULL) {
			fprintf(stderr, "\nUnable to open output file %s\n", file);
			fprintf(stderr, "Program %s aborted!\n", program_name);
			Return(MB_ERROR_OPEN_FAIL);
		}
		fprintf(fp, "$PROCESSED\n%s %d\n", read_file, format);
		fclose(fp);
		if (verbose > 0)
			fprintf(output, "Convenience datalist file %s created...\n", file);

		/* exit unless building ancillary files has also been requested */
		if (!make_inf)
			Return(error);
	}

	/* get format if required */
	if (format == 0)
		mb_get_format(verbose, read_file, NULL, &format, &error);

	void *datalist;
	double file_weight = 1.0;
	mb_command command;
	int nfile = 0;
	int nparproblem;
	int ndataproblem;
	int nparproblemtot = 0;
	int ndataproblemtot = 0;
	int nproblemfiles = 0;
	int recursion = -1;

	int prstatus = MB_PR_FILE_UP_TO_DATE;
	int lock_error = MB_ERROR_NO_ERROR;
	int lock_purpose = MBP_LOCK_NONE;
	mb_path lock_program = "";
	mb_path lock_cpu = "";
	mb_path lock_user = "";
	char lock_date[25] = "";
	char lockfile[MB_PATH_MAXLINE+10] = "";
	bool file_in_bounds = false;
	bool locked = false;

	/* if not a datalist just output filename format and weight */
	if (format > 0) {
		nfile++;

		if (make_inf) {
			status = mb_make_info(verbose, force_update, read_file, format, &error);
		}
		else if (problem_report) {
			status = mb_pr_check(verbose, read_file, &nparproblem, &ndataproblem, &error);
			if (nparproblem + ndataproblem > 0)
				nproblemfiles++;
			nparproblemtot += nparproblem;
			ndataproblemtot += ndataproblem;
		}
		else {
			/* check for mbinfo file if bounds checking enabled */
			if (look_bounds) {
				status = mb_check_info(verbose, read_file, lonflip, bounds, &file_in_bounds, &error);
				if (status == MB_FAILURE) {
					file_in_bounds = true;
					status = MB_SUCCESS;
					error = MB_ERROR_NO_ERROR;
				}
			}

			/* output file if no bounds checking or in bounds */
			if (!look_bounds || file_in_bounds) {
				if (verbose > 0)
					fprintf(output, "%s %d %f\n", read_file, format, file_weight);
				else
					fprintf(output, "%s %d %f", read_file, format, file_weight);

				/* check status if desired */
				if (status_report) {
					status = mb_pr_checkstatus(verbose, read_file, &prstatus, &error);
					if (verbose > 0) {
						if (prstatus == MB_PR_FILE_UP_TO_DATE)
							fprintf(output, "\tStatus: up to date\n");
						else if (prstatus == MB_PR_FILE_NEEDS_PROCESSING)
							fprintf(output, "\tStatus: out of date - needs processing\n");
						else if (prstatus == MB_PR_FILE_NOT_EXIST)
							fprintf(output, "\tStatus: file does not exist\n");
						else if (prstatus == MB_PR_NO_PARAMETER_FILE)
							fprintf(output, "\tStatus: no parameter file - processing undefined\n");
					}
					else {
						if (prstatus == MB_PR_FILE_UP_TO_DATE)
							fprintf(output, "\t<Up-to-date>");
						else if (prstatus == MB_PR_FILE_NEEDS_PROCESSING)
							fprintf(output, "\t<Needs-processing>");
						else if (prstatus == MB_PR_FILE_NOT_EXIST)
							fprintf(output, "\t<Does-not-exist>");
						else if (prstatus == MB_PR_NO_PARAMETER_FILE)
							fprintf(output, "\t<No-parameter-file>");
					}
				}

				/* check locks if desired */
				if (status_report || remove_locks) {
					/* int lock_status = */ mb_pr_lockinfo(verbose, read_file, &locked, &lock_purpose, lock_program, lock_user, lock_cpu,
					                             lock_date, &lock_error);
					if (locked && status_report) {
						if (verbose > 0)
							fprintf(output, "\tLocked by program <%s> run by <%s> on <%s> at <%s>\n", lock_program, lock_user,
							        lock_cpu, lock_date);
						else
							fprintf(output, "\t<Locked>");
					}
					if (locked && remove_locks) {
						if (strlen(read_file) >= MB_PATH_MAXLINE - 4) {
							fprintf(stderr, "\nFilename too long to construct lock file name: %s\n", read_file);
						} else {
							snprintf(lockfile, sizeof(lockfile), "%s.lck", read_file);
							remove(lockfile);
						}
					}
				}

				if (verbose == 0)
					fprintf(output, "\n");
			}
		}
	}

	/* else parse datalist */
	else {
		if (mb_datalist_open(verbose, &datalist, read_file, look_processed, &error) != MB_SUCCESS) {
			fprintf(stderr, "\nUnable to open data list file: %s\n", read_file);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			Return(MB_ERROR_OPEN_FAIL);
		}
		mb_path file = "";
		mb_path dfile = "";
		mb_path dfilelast = "";
		char *copied_list = NULL;
		size_t copied_len = 0;
		while (mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight, &error) == MB_SUCCESS) {
			nfile++;
			mb_path pwd = "";
			if (getcwd(pwd, MB_PATH_MAXLINE) == NULL)
				pwd[0] = '\0';
			mb_get_relative_path(verbose, file, pwd, &error);
			mb_get_relative_path(verbose, dfile, pwd, &error);

			/* generate inf fnv fbt files */
			if (make_inf) {
				status = mb_make_info(verbose, force_update, file, format, &error);
			}

			/* or generate problem reports */
			else if (problem_report) {
				status = mb_pr_check(verbose, file, &nparproblem, &ndataproblem, &error);
				if (nparproblem + ndataproblem > 0)
					nproblemfiles++;
				nparproblemtot += nparproblem;
				ndataproblemtot += ndataproblem;
			}

			/* or copy files */
			else if (copyfiles) {
				/* check for mbinfo file if bounds checking enabled */
				if (look_bounds) {
					status = mb_check_info(verbose, file, lonflip, bounds, &file_in_bounds, &error);
					if (status == MB_FAILURE) {
						file_in_bounds = true;
						status = MB_SUCCESS;
						error = MB_ERROR_NO_ERROR;
					}
				}

				/* copy file if no bounds checking or in bounds */
				if (!look_bounds || file_in_bounds) {
					fprintf(output, "Copying %s %d %f\n", file, format, file_weight);
					mbdatalist_copy_file_family(output, file);
					char *filename = strrchr(file, '/');
					if (filename != NULL)
						filename++;
					else
						filename = file;
					/* the new datalist.mb-1 is written once the input datalist
					   is closed - it may be that very file, and on Windows an
					   open file can be neither removed nor safely appended to */
					snprintf(command, sizeof(command), "%s %d %f\n", filename, format, file_weight);
					const size_t addlen = strlen(command);
					char *grown = (char *)realloc(copied_list, copied_len + addlen + 1);
					if (grown != NULL) {
						copied_list = grown;
						memcpy(copied_list + copied_len, command, addlen + 1);
						copied_len += addlen;
					}
				}
			}

			/* or list the datalists parsed through the recursive datalist structure */
			else if (reportdatalists) {
				if (strcmp(dfile, dfilelast) != 0)
					status = mb_datalist_recursion(verbose, datalist, true, &recursion, &error);
				strcpy(dfilelast, dfile);
			}

			/* or list the files returned from parsing the recursive datalist
			    structure, with bounds checking if desired */
			else {
				/* check for mbinfo file if bounds checking enabled */
				if (look_bounds) {
					status = mb_check_info(verbose, file, lonflip, bounds, &file_in_bounds, &error);
					if (status == MB_FAILURE) {
						file_in_bounds = true;
						status = MB_SUCCESS;
						error = MB_ERROR_NO_ERROR;
					}
				}

				/* output file if no bounds checking or in bounds */
				if (!look_bounds || file_in_bounds) {
					if (verbose > 0)
						fprintf(output, "%s %d %f\n", file, format, file_weight);
					else
						fprintf(output, "%s %d %f", file, format, file_weight);

					/* check status if desired */
					if (status_report) {
						status = mb_pr_checkstatus(verbose, file, &prstatus, &error);
						if (verbose > 0) {
							if (prstatus == MB_PR_FILE_UP_TO_DATE)
								fprintf(output, "\tStatus: up to date\n");
							else if (prstatus == MB_PR_FILE_NEEDS_PROCESSING)
								fprintf(output, "\tStatus: out of date - needs processing\n");
							else if (prstatus == MB_PR_FILE_NOT_EXIST)
								fprintf(output, "\tStatus: file does not exist\n");
							else if (prstatus == MB_PR_NO_PARAMETER_FILE)
								fprintf(output, "\tStatus: no parameter file - processing undefined\n");
						}
						else {
							if (prstatus == MB_PR_FILE_UP_TO_DATE)
								fprintf(output, "\t<Up-to-date>");
							else if (prstatus == MB_PR_FILE_NEEDS_PROCESSING)
								fprintf(output, "\t<Needs-processing>");
							else if (prstatus == MB_PR_FILE_NOT_EXIST)
								fprintf(output, "\t<Does-not-exist>");
							else if (prstatus == MB_PR_NO_PARAMETER_FILE)
								fprintf(output, "\t<No-parameter-file>");
						}
					}

					/* check locks if desired */
					if (status_report || remove_locks) {
						/* int lock_status = */ mb_pr_lockinfo(verbose, file, &locked, &lock_purpose, lock_program, lock_user, lock_cpu,
						                             lock_date, &lock_error);
						if (locked && status_report) {
							if (verbose > 0)
								fprintf(output, "\tLocked by program <%s> run by <%s> on <%s> at <%s>\n", lock_program, lock_user,
								        lock_cpu, lock_date);
							else
								fprintf(output, "\t<Locked>");
						}
						if (locked && remove_locks) {
							if (strlen(file) >= MB_PATH_MAXLINE - 4) {
								fprintf(stderr, "\nFilename too long to construct lock file name: %s\n", file);
							} else {
								snprintf(lockfile, sizeof(lockfile), "%s.lck", file);
								fprintf(output, "\tRemoving lock file %s\n", lockfile);
								/* shellstatus = */ remove(lockfile);
							}
						}
					}

					if (verbose == 0)
						fprintf(output, "\n");
				}
			}
		}
		mb_datalist_close(verbose, &datalist, &error);

		/* write the datalist of the copied files */
		if (copyfiles && copied_list != NULL) {
			FILE *dfp = fopen("datalist.mb-1", "w");
			if (dfp != NULL) {
				fputs(copied_list, dfp);
				fclose(dfp);
			}
			else
				fprintf(stderr, "\nUnable to write datalist.mb-1\n");
		}
		free(copied_list);
	}

	/* set program status */
	// status = MB_SUCCESS;

	/* output counts */
	if (verbose > 0) {
		fprintf(output, "\nTotal swath files:         %d\n", nfile);
		if (problem_report) {
			fprintf(output, "Total files with problems: %d\n", nproblemfiles);
			fprintf(output, "Total parameter problems:  %d\n", nparproblemtot);
			fprintf(output, "Total data problems:       %d\n", ndataproblemtot);
		}
	}

	/* check memory */
	if ((status = mb_memory_list(verbose, &error)) == MB_FAILURE) {
		fprintf(stderr, "Program %s completed but failed to deallocate all allocated memory - the code has a memory leak somewhere!\n", program_name);
	}

	Return(error);
}
/*--------------------------------------------------------------------*/
