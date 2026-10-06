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
 * GMT-module port of src/utilities/mbdatalist.cc: options from GMT's option list (the program's
 * long options are GMT long options through module_kw, its lower-case aliases are kept --
 * GMT_Parse_Common only parses the common options named in THIS_MODULE_OPTIONS; the argument-less
 * -Y and -p, which GMT would complete from its history, go through mb_gmt_shorthand_guard), and
 * the listing is written as text records through the GMT API (mb_gmt_text.c).
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
#include "mb_gmt_opts.h"
#include "mb_gmt_text.h"

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

/* Translation table from the program's long options to its short ones (each one has a short twin) */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'C', "copy",                               "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'D', "report",                             "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'F', "format",                             "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",                               "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",                              "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'N', "make-ancillary|make-ancilliary",     "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "update-ancillary|update-ancilliary", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'P', "processed",                          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'Q', "problem",                            "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'R', "bounds",                             "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'S', "status",                             "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'U', "raw",                                "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'y', "unlock",                             "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'Z', "datalistp",                          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'v', "verbose",                            "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

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
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
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
	            "\t-H Print help and exit.\n"
	            "Every option also has the program's lower-case form.\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
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
		case 'P': case 'p':	/* -p arrives as -P: mb_gmt_shorthand_guard */
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
		case 'U': case 'u':
			Ctrl->U.active = true;
			Ctrl->look_processed = MB_DATALIST_LOOK_NO;
			break;
		case 'Y': case 'y':	/* -Y arrives as -y: mb_gmt_shorthand_guard */
			Ctrl->Y.active = true;
			break;
		case 'Z': case 'z':
			Ctrl->Z.active = true;
			break;
		case 'v':	/* the program's -v: verbosity, as -V */
			GMT->current.setting.verbose = GMT_MSG_INFORMATION;
			GMT->common.V.active = true;
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}
	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

/*--------------------------------------------------------------------*/
/* Copy a swath file and its ancillary files (every file in its directory
 * whose name starts with the swath file name, as "cp file* ." did) into
 * the current directory. Done in-process: there is no "cp" on Windows.
 * Nothing is copied when the file already lives in the current directory. */
static void mbdatalist_copy_file_family(struct MB_GMT_TEXT *output, const char *file) {
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
		mb_gmt_text_put(output,"Unable to open directory %s\n", dir);
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
			mb_gmt_text_put(output,"Unable to copy %s\n", src);
			fclose(in);
			continue;
		}
		char buf[65536];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
			if (fwrite(buf, 1, n, out) != n) {
				mb_gmt_text_put(output,"Unable to copy %s\n", src);
				break;
			}
		}
		fclose(out);
		fclose(in);
	}
	closedir(dp);
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code) { if (output) mb_gmt_text_end(output); Free_mbdatalist_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbdatalist(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbdatalist(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MBDATALIST_CTRL *Ctrl = NULL;
	struct MB_GMT_TEXT *output = NULL;	/* the listing, as GMT records */
	int parse_error;

	if (!API) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no arguments is a valid run -- mbdatalist lists ./datalist.mb-1 */
	if ((parse_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(parse_error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	mb_gmt_shorthand_guard(options, "Yp");	/* -Y unlock, -p processed: no argument */
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = (struct MBDATALIST_CTRL *)New_mbdatalist_Ctrl(GMT);
	if ((parse_error = parse_mbdatalist(GMT, Ctrl, options)) != GMT_NOERROR) Return(parse_error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

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

		/* The listing: text records through the GMT API (the program put it on stderr at -V2;
		   as a module the caller receives it as data, and the debug lines below go to stderr) */
		if ((output = mb_gmt_text_begin(GMT, options)) == NULL)
			Return(API->error);

		GMT_Report(API, GMT_MSG_INFORMATION, "Program %s\n", program_name);
		GMT_Report(API, GMT_MSG_INFORMATION, "MB-system Version %s\n", MB_VERSION);

		if (verbose >= 2) {
			fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
			fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(stderr, "dbg2  Control Parameters:\n");
			fprintf(stderr, "dbg2       verbose:             %d\n", verbose);
			fprintf(stderr, "dbg2       help:                %d\n", help);
			fprintf(stderr, "dbg2       file:                %s\n", read_file);
			fprintf(stderr, "dbg2       format:              %d\n", format);
			fprintf(stderr, "dbg2       look_processed:      %d\n", look_processed);
			fprintf(stderr, "dbg2       copyfiles:           %d\n", copyfiles);
			fprintf(stderr, "dbg2       reportdatalists:     %d\n", reportdatalists);
			fprintf(stderr, "dbg2       make_inf:            %d\n", make_inf);
			fprintf(stderr, "dbg2       force_update:        %d\n", force_update);
			fprintf(stderr, "dbg2       status_report:       %d\n", status_report);
			fprintf(stderr, "dbg2       problem_report:      %d\n", problem_report);
			fprintf(stderr, "dbg2       make_datalistp:      %d\n", make_datalistp);
			fprintf(stderr, "dbg2       remove_locks:        %d\n", remove_locks);
			fprintf(stderr, "dbg2       pings:               %d\n", pings);
			fprintf(stderr, "dbg2       lonflip:             %d\n", lonflip);
			fprintf(stderr, "dbg2       bounds[0]:           %f\n", bounds[0]);
			fprintf(stderr, "dbg2       bounds[1]:           %f\n", bounds[1]);
			fprintf(stderr, "dbg2       bounds[2]:           %f\n", bounds[2]);
			fprintf(stderr, "dbg2       bounds[3]:           %f\n", bounds[3]);
			fprintf(stderr, "dbg2       btime_i[0]:          %d\n", btime_i[0]);
			fprintf(stderr, "dbg2       btime_i[1]:          %d\n", btime_i[1]);
			fprintf(stderr, "dbg2       btime_i[2]:          %d\n", btime_i[2]);
			fprintf(stderr, "dbg2       btime_i[3]:          %d\n", btime_i[3]);
			fprintf(stderr, "dbg2       btime_i[4]:          %d\n", btime_i[4]);
			fprintf(stderr, "dbg2       btime_i[5]:          %d\n", btime_i[5]);
			fprintf(stderr, "dbg2       btime_i[6]:          %d\n", btime_i[6]);
			fprintf(stderr, "dbg2       etime_i[0]:          %d\n", etime_i[0]);
			fprintf(stderr, "dbg2       etime_i[1]:          %d\n", etime_i[1]);
			fprintf(stderr, "dbg2       etime_i[2]:          %d\n", etime_i[2]);
			fprintf(stderr, "dbg2       etime_i[3]:          %d\n", etime_i[3]);
			fprintf(stderr, "dbg2       etime_i[4]:          %d\n", etime_i[4]);
			fprintf(stderr, "dbg2       etime_i[5]:          %d\n", etime_i[5]);
			fprintf(stderr, "dbg2       etime_i[6]:          %d\n", etime_i[6]);
			fprintf(stderr, "dbg2       speedmin:            %f\n", speedmin);
			fprintf(stderr, "dbg2       timegap:             %f\n", timegap);
		}

		(void)help;	/* -H returned usage() before this point */
	}

	int error = MB_ERROR_NO_ERROR;

	/* output stream for basic stuff (stdout if verbose <= 1,
	    output if verbose > 1) */

	if (make_datalistp) {
		/* figure out data format and fileroot if possible */
		char fileroot[MB_PATH_MAXLINE] = {0};
		status = mb_get_format(verbose, read_file, fileroot, &format, &error);
		if (strlen(fileroot) >= MB_PATH_MAXLINE - 6) {
			GMT_Report(API, GMT_MSG_ERROR, "File root too long: %s\n", fileroot);
			Return(GMT_RUNTIME_ERROR);
		}
		char file[MB_PATH_MAXLINE+10];
		snprintf(file, sizeof(file), "%sp.mb-1", fileroot);

		FILE *fp = fopen(file, "w");
		if (fp == NULL) {
			GMT_Report(API, GMT_MSG_ERROR, "Unable to open output file %s\n", file);
			Return(GMT_ERROR_ON_FOPEN);
		}
		fprintf(fp, "$PROCESSED\n%s %d\n", read_file, format);
		fclose(fp);
		if (verbose > 0)
			mb_gmt_text_put(output,"Convenience datalist file %s created...\n", file);

		/* exit unless building ancillary files has also been requested */
		if (!make_inf)
			Return(GMT_NOERROR);
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
					mb_gmt_text_put(output,"%s %d %f\n", read_file, format, file_weight);
				else
					mb_gmt_text_put(output,"%s %d %f", read_file, format, file_weight);

				/* check status if desired */
				if (status_report) {
					status = mb_pr_checkstatus(verbose, read_file, &prstatus, &error);
					if (verbose > 0) {
						if (prstatus == MB_PR_FILE_UP_TO_DATE)
							mb_gmt_text_put(output,"\tStatus: up to date\n");
						else if (prstatus == MB_PR_FILE_NEEDS_PROCESSING)
							mb_gmt_text_put(output,"\tStatus: out of date - needs processing\n");
						else if (prstatus == MB_PR_FILE_NOT_EXIST)
							mb_gmt_text_put(output,"\tStatus: file does not exist\n");
						else if (prstatus == MB_PR_NO_PARAMETER_FILE)
							mb_gmt_text_put(output,"\tStatus: no parameter file - processing undefined\n");
					}
					else {
						if (prstatus == MB_PR_FILE_UP_TO_DATE)
							mb_gmt_text_put(output,"\t<Up-to-date>");
						else if (prstatus == MB_PR_FILE_NEEDS_PROCESSING)
							mb_gmt_text_put(output,"\t<Needs-processing>");
						else if (prstatus == MB_PR_FILE_NOT_EXIST)
							mb_gmt_text_put(output,"\t<Does-not-exist>");
						else if (prstatus == MB_PR_NO_PARAMETER_FILE)
							mb_gmt_text_put(output,"\t<No-parameter-file>");
					}
				}

				/* check locks if desired */
				if (status_report || remove_locks) {
					/* int lock_status = */ mb_pr_lockinfo(verbose, read_file, &locked, &lock_purpose, lock_program, lock_user, lock_cpu,
					                             lock_date, &lock_error);
					if (locked && status_report) {
						if (verbose > 0)
							mb_gmt_text_put(output,"\tLocked by program <%s> run by <%s> on <%s> at <%s>\n", lock_program, lock_user,
							        lock_cpu, lock_date);
						else
							mb_gmt_text_put(output,"\t<Locked>");
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
					mb_gmt_text_put(output,"\n");
			}
		}
	}

	/* else parse datalist */
	else {
		if (mb_datalist_open(verbose, &datalist, read_file, look_processed, &error) != MB_SUCCESS) {
			GMT_Report(API, GMT_MSG_ERROR, "Unable to open data list file: %s\n", read_file);
			Return(GMT_ERROR_ON_FOPEN);
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
					mb_gmt_text_put(output,"Copying %s %d %f\n", file, format, file_weight);
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
						mb_gmt_text_put(output,"%s %d %f\n", file, format, file_weight);
					else
						mb_gmt_text_put(output,"%s %d %f", file, format, file_weight);

					/* check status if desired */
					if (status_report) {
						status = mb_pr_checkstatus(verbose, file, &prstatus, &error);
						if (verbose > 0) {
							if (prstatus == MB_PR_FILE_UP_TO_DATE)
								mb_gmt_text_put(output,"\tStatus: up to date\n");
							else if (prstatus == MB_PR_FILE_NEEDS_PROCESSING)
								mb_gmt_text_put(output,"\tStatus: out of date - needs processing\n");
							else if (prstatus == MB_PR_FILE_NOT_EXIST)
								mb_gmt_text_put(output,"\tStatus: file does not exist\n");
							else if (prstatus == MB_PR_NO_PARAMETER_FILE)
								mb_gmt_text_put(output,"\tStatus: no parameter file - processing undefined\n");
						}
						else {
							if (prstatus == MB_PR_FILE_UP_TO_DATE)
								mb_gmt_text_put(output,"\t<Up-to-date>");
							else if (prstatus == MB_PR_FILE_NEEDS_PROCESSING)
								mb_gmt_text_put(output,"\t<Needs-processing>");
							else if (prstatus == MB_PR_FILE_NOT_EXIST)
								mb_gmt_text_put(output,"\t<Does-not-exist>");
							else if (prstatus == MB_PR_NO_PARAMETER_FILE)
								mb_gmt_text_put(output,"\t<No-parameter-file>");
						}
					}

					/* check locks if desired */
					if (status_report || remove_locks) {
						/* int lock_status = */ mb_pr_lockinfo(verbose, file, &locked, &lock_purpose, lock_program, lock_user, lock_cpu,
						                             lock_date, &lock_error);
						if (locked && status_report) {
							if (verbose > 0)
								mb_gmt_text_put(output,"\tLocked by program <%s> run by <%s> on <%s> at <%s>\n", lock_program, lock_user,
								        lock_cpu, lock_date);
							else
								mb_gmt_text_put(output,"\t<Locked>");
						}
						if (locked && remove_locks) {
							if (strlen(file) >= MB_PATH_MAXLINE - 4) {
								fprintf(stderr, "\nFilename too long to construct lock file name: %s\n", file);
							} else {
								snprintf(lockfile, sizeof(lockfile), "%s.lck", file);
								mb_gmt_text_put(output,"\tRemoving lock file %s\n", lockfile);
								/* shellstatus = */ remove(lockfile);
							}
						}
					}

					if (verbose == 0)
						mb_gmt_text_put(output,"\n");
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
		mb_gmt_text_put(output,"\nTotal swath files:         %d\n", nfile);
		if (problem_report) {
			mb_gmt_text_put(output,"Total files with problems: %d\n", nproblemfiles);
			mb_gmt_text_put(output,"Total parameter problems:  %d\n", nparproblemtot);
			mb_gmt_text_put(output,"Total data problems:       %d\n", ndataproblemtot);
		}
	}

	/* check memory */
	if ((status = mb_memory_list(verbose, &error)) == MB_FAILURE) {
		fprintf(stderr, "Program %s completed but failed to deallocate all allocated memory - the code has a memory leak somewhere!\n", program_name);
	}

	const int output_failed = mb_gmt_text_end(output);
	output = NULL;	/* closed: Return() must not close it again */
	if (output_failed) {
		GMT_Report(API, GMT_MSG_ERROR, "Writing the listing failed\n");
		Return(GMT_RUNTIME_ERROR);
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
