/*--------------------------------------------------------------------
 *    The MB-system:	mbsegypsd.c	11/2/2009
 *
 *    Copyright (c) 2009-2025 by
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
 * mbsegypsd calculates the power spectral densisty function of each trace in a
 * segy file, outputting the PSD as a GMT grid file with trace number along
 * the x axis and frequency along the y axis.
 *
 * Author:	D. W. Caress
 * Date:	November 2, 2009
 */
/*
 * GMT-module port of src/utilities/mbsegypsd.cc: options from GMT's option list (long options
 * through module_kw, lower-case aliases kept), main() becomes GMT_mbsegypsd() and every exit() a
 * Return() with a GMT error code. Its results are the program's own files; its messages go to stderr.
 */

#define THIS_MODULE_NAME "mbsegypsd"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Calculate the power spectral density of each trace of a segy file as a grid"
/* Primary input is the segy file given with -I; the grid is written by the module itself. */
#define THIS_MODULE_KEYS "ID{"
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
#include "fftw3.h"
#include "mb_aux.h"
#include "mb_define.h"
#include "mb_format.h"
#include "mb_segy.h"
#include "mb_status.h"
#include "mb_gmt_opts.h"

typedef enum {
    MBSEGYPSD_USESHOT = 0,
    MBSEGYPSD_USECMP = 1,
} tracemode_t;

typedef enum {
    MBSEGYPSD_WINDOW_OFF = 0,
    MBSEGYPSD_WINDOW_ON = 1,
    MBSEGYPSD_WINDOW_SEAFLOOR = 2,
    MBSEGYPSD_WINDOW_DEPTH = 3,
} windowmode_t;

/* output stream for basic stuff (stdout if verbose <= 1,
    stderr if verbose > 1) */
static FILE *outfp = NULL;

static const char program_name[] = "mbsegypsd";
static const char help_message[] =
    "mbsegypsd calculates the power spectral density function of each trace in a segy data file,\n"
    "outputting the results as a GMT grid file.";
static const char usage_message[] =
    "mbsegypsd\n"
    "\t--decimate=decimatex {-Ddecimatex}\n"
    "\t--fft-length=nfft {-Nnfft}\n"
    "\t--help {-H}\n"
    "\t--input=file {-Ifile}\n"
    "\t--log-scale {-L}\n"
    "\t--output=root {-Oroot}\n"
    "\t--reserved {-P}\n"
    "\t--shot-scale=shotscale[/frequencyscale] {-Ashotscale[/frequencyscale]}\n"
    "\t--time-sweep=sweep[/delay] {-Tsweep[/delay]}\n"
    "\t--trace-range=mode[/start/end[/schan/echan]] {-Smode[/start/end[/schan/echan]]}\n"
    "\t--verbose {-V}\n"
    "\t--window=mode/start/end {-Wmode/start/end}\n";

/*--------------------------------------------------------------------*/
/*
 * function get_segy_limits gets info for default segy gridding
 */
static int get_segy_limits(int verbose, char *segyfile, tracemode_t *tracemode,
                    int *tracestart, int *traceend, int *chanstart,
                    int *chanend, double *timesweep, double *timedelay,
                    int *error) {
	if (verbose >= 2) {
		fprintf(outfp, "\ndbg2  Function <%s> called\n", __func__);
		fprintf(outfp, "dbg2  Input arguments:\n");
		fprintf(outfp, "dbg2       verbose:    %d\n", verbose);
		fprintf(outfp, "dbg2       segyfile:   %s\n", segyfile);
	}

	/* set sinf filename */
	char sinffile[MB_PATH_MAXLINE] = "";
	snprintf(sinffile, sizeof(sinffile), "%s.sinf", segyfile);

	/* check status of segy and sinf file */
	int datmodtime = 0;
	int sinfmodtime = 0;
	struct stat file_status;
	int fstat = stat(segyfile, &file_status);
	if (fstat == 0 && (file_status.st_mode & S_IFMT) != S_IFDIR) {
		datmodtime = file_status.st_mtime;
	}
        fstat = stat(sinffile, &file_status);
	if (fstat == 0 && (file_status.st_mode & S_IFMT) != S_IFDIR) {
		sinfmodtime = file_status.st_mtime;
	}

	/* if sinf file is missing or out of date, make it */
	if (datmodtime > 0 && datmodtime > sinfmodtime) {
		if (verbose >= 1)
			fprintf(stderr, "\nGenerating sinf file for %s\n", segyfile);
		char command[MB_PATH_MAXLINE] = "";
		snprintf(command, sizeof(command), "mbsegyinfo -I %s -O", segyfile);
		/* int shellstatus = */ system(command);
	}

	double delay0 = 0.0;
	double delaydel = 0.0;
	int shot0 = 0;
	int shot1 = 0;
	int shottrace0 = 0;
	int shottrace1 = 0;
	int rp0 = 0;
	int rp1 = 0;
	int rpdel = 0;
	int rptrace0 = 0;
	int rptrace1 = 0;

	/* read sinf file if possible */
	snprintf(sinffile, sizeof(sinffile), "%s.sinf", segyfile);
	FILE *sfp = fopen(sinffile, "r");
	if (sfp != NULL) {
		/* read the sinf file */
		char line[MB_PATH_MAXLINE] = "";
		while (fgets(line, MB_PATH_MAXLINE, sfp) != NULL) {
			if (strncmp(line, "  Trace length (sec):", 21) == 0) {
				/* nscan = */ sscanf(line, "  Trace length (sec):%lf", timesweep);
			}
			else if (strncmp(line, "    Delay (sec):", 16) == 0) {
				double delay1 = 0.0;
				/* nscan = */ sscanf(line, "    Delay (sec): %lf %lf %lf", &delay0, &delay1, &delaydel);
			}
			else if (strncmp(line, "    Shot number:", 16) == 0) {
				int shotdel = 0;
				/* nscan = */ sscanf(line, "    Shot number: %d %d %d", &shot0, &shot1, &shotdel);
			}
			else if (strncmp(line, "    Shot trace:", 15) == 0) {
				int shottracedel = 0;
				/* nscan = */ sscanf(line, "    Shot trace: %d %d %d", &shottrace0, &shottrace1, &shottracedel);
			}
			else if (strncmp(line, "    RP number:", 14) == 0) {
				/* nscan = */ sscanf(line, "    RP number: %d %d %d", &rp0, &rp1, &rpdel);
			}
			else if (strncmp(line, "    RP trace:", 13) == 0) {
				int rptracedel = 0;
				/* nscan = */ sscanf(line, "    RP trace: %d %d %d", &rptrace0, &rptrace1, &rptracedel);
			}
		}
		fclose(sfp);
	}

	/* set the trace mode */
	if (rpdel > 1) {
		*tracemode = MBSEGYPSD_USECMP;
		*tracestart = rp0;
		*traceend = rp1;
		*chanstart = rptrace0;
		*chanend = rptrace1;
	}
	else {
		*tracemode = MBSEGYPSD_USESHOT;
		*tracestart = shot0;
		*traceend = shot1;
		*chanstart = shottrace0;
		*chanend = shottrace1;
	}

	/* set the sweep and delay */
	if (delaydel > 0.0) {
		*timesweep += delaydel;
	}
	*timedelay = delay0;

	const int status = MB_SUCCESS;

	if (verbose >= 2) {
		fprintf(outfp, "\ndbg2  MBIO function <%s> completed\n", __func__);
		fprintf(outfp, "dbg2  Return values:\n");
		fprintf(outfp, "dbg2       tracemode:  %d\n", *tracemode);
		fprintf(outfp, "dbg2       tracestart: %d\n", *tracestart);
		fprintf(outfp, "dbg2       traceend:   %d\n", *traceend);
		fprintf(outfp, "dbg2       chanstart:  %d\n", *chanstart);
		fprintf(outfp, "dbg2       chanend:    %d\n", *chanend);
		fprintf(outfp, "dbg2       timesweep:  %f\n", *timesweep);
		fprintf(outfp, "dbg2       timedelay:  %f\n", *timedelay);
		fprintf(outfp, "dbg2       error:      %d\n", *error);
		fprintf(outfp, "dbg2  Return status:\n");
		fprintf(outfp, "dbg2       status:     %d\n", status);
	}

	return (status);
}

/*--------------------------------------------------------------------*/


/* --- GMT front end ---------------------------------------------------- */

/* Translation table from the program's long options to its short ones (each one has a short twin) */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'v', "verbose",     "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'D', "decimate",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'N', "fft-length",  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",       "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'L', "log-scale",   "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "output",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'A', "shot-scale",  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'T', "time-sweep",  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'S', "trace-range", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'W', "window",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n", help_message);
	GMT_Message(API, GMT_TIME_NONE, "Every option also has the program's lower-case and long forms.\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

#define bailout(code) { gmt_M_free_options(mode); return code; }
#define Return(code) { gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbsegypsd(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbsegypsd(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
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

	int verbose = 0;
	double shotscale = 1.0;
	double frequencyscale = 1.0;
	bool scale2distance = false;
	int decimatex = 1;
	char segyfile[MB_PATH_MAXLINE] = "";
	bool logscale = false;
	int nfft = 1024;
	char fileroot[MB_PATH_MAXLINE] = "";
	tracemode_t tracemode = MBSEGYPSD_USESHOT;
	int tracestart = 0;
	int traceend = 0;
	int chanstart = 0;
	int chanend = -1;
	double timesweep = 0.0;
	double timedelay = 0.0;
	windowmode_t windowmode = MBSEGYPSD_WINDOW_OFF;
	double windowstart;
	double windowend;
	int ngridx = 0;
	int ngridy = 0;
	int ngridxy = 0;

	{
		bool errflg = false;
		bool help = false;
		/* the program's options from GMT's option list (long options come in as their short twins
		   through module_kw; lower-case aliases kept) */
		for (struct GMT_OPTION *opt = options; opt; opt = opt->next)
			switch (opt->option) {
			case 'H':
			case 'h':
				help = true;
				break;
			case 'V':
			case 'v':
				verbose++;
				break;
			case 'A':
			case 'a':
			{
				const int n = sscanf(opt->arg, "%lf/%lf", &shotscale, &frequencyscale);
				if (n == 2)
					scale2distance = true;
				break;
			}
			case 'D':
			case 'd':
				sscanf(opt->arg, "%d", &decimatex);
				break;
			case 'I':
			case 'i':
				sscanf(opt->arg, "%1023s", segyfile);
				break;
			case 'L':
			case 'l':
				logscale = true;
				break;
			case 'N':
			case 'n':
				sscanf(opt->arg, "%d", &nfft);
				break;
			case 'G':
			case 'O':
			case 'o':
				sscanf(opt->arg, "%1023s", fileroot);
				break;
			case 'S':
			case 's':
			{
				int tracemode_tmp;
				const int n = sscanf(opt->arg, "%d/%d/%d/%d/%d", &tracemode_tmp, &tracestart, &traceend, &chanstart, &chanend);
				tracemode = (tracemode_t)tracemode_tmp;  // TODO(Schwehr): Range check.
				if (n < 5) {
					chanstart = 0;
					chanend = -1;
				}
				if (n < 3) {
					tracestart = 0;
					traceend = 0;
				}
				if (n < 1) {
					tracemode = MBSEGYPSD_USESHOT;
				}
				break;
			}
			case 'T':
			case 't':
			{
				const int n = sscanf(opt->arg, "%lf/%lf", &timesweep, &timedelay);
				if (n < 2)
					timedelay = 0.0;
				break;
			}
			case 'W':
			case 'w':
			{
				int windowmode_tmp;
				sscanf(opt->arg, "%d/%lf/%lf", &windowmode_tmp, &windowstart, &windowend);
				windowmode = (windowmode_t)windowmode_tmp;  // TODO(Schwehr): Range check.
				break;
			}
			default:
				errflg |= (gmt_default_option_error(GMT, opt) != 0);
				break;
			}

		/* The program's messages: on stdout below -V2 in the program, but as a module stdout is
		   data, and these are diagnostics. The results are the program's own files. */
		outfp = stderr;

		if (errflg)
			Return(GMT_PARSE_ERROR);

		if (verbose == 1 || help) {
			fprintf(outfp, "\nProgram %s\n", program_name);
			fprintf(outfp, "MB-system Version %s\n", MB_VERSION);
		}

		if (verbose >= 2) {
			fprintf(outfp, "\ndbg2  Program <%s>\n", program_name);
			fprintf(outfp, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(outfp, "dbg2  Control Parameters:\n");
			fprintf(outfp, "dbg2       verbose:        %d\n", verbose);
			fprintf(outfp, "dbg2       help:           %d\n", help);
			fprintf(outfp, "dbg2       segyfile:       %s\n", segyfile);
			fprintf(outfp, "dbg2       fileroot:       %s\n", fileroot);
			fprintf(outfp, "dbg2       nfft:           %d\n", nfft);
			fprintf(outfp, "dbg2       decimatex:      %d\n", decimatex);
			fprintf(outfp, "dbg2       tracemode:      %d\n", tracemode);
			fprintf(outfp, "dbg2       tracestart:     %d\n", tracestart);
			fprintf(outfp, "dbg2       traceend:       %d\n", traceend);
			fprintf(outfp, "dbg2       chanstart:      %d\n", chanstart);
			fprintf(outfp, "dbg2       chanend:        %d\n", chanend);
			fprintf(outfp, "dbg2       timesweep:      %f\n", timesweep);
			fprintf(outfp, "dbg2       timedelay:      %f\n", timedelay);
			fprintf(outfp, "dbg2       ngridx:         %d\n", ngridx);
			fprintf(outfp, "dbg2       ngridy:         %d\n", ngridy);
			fprintf(outfp, "dbg2       ngridxy:        %d\n", ngridxy);
			fprintf(outfp, "dbg2       windowmode:     %d\n", windowmode);
			fprintf(outfp, "dbg2       windowstart:    %f\n", windowstart);
			fprintf(outfp, "dbg2       windowend:      %f\n", windowend);
			fprintf(outfp, "dbg2       scale2distance: %d\n", scale2distance);
			fprintf(outfp, "dbg2       shotscale:      %f\n", shotscale);
			fprintf(outfp, "dbg2       frequencyscale: %f\n", frequencyscale);
			fprintf(outfp, "dbg2       logscale:       %d\n", logscale);
		}

		if (help)
			Return(usage(API, GMT_USAGE));
	}

	if (decimatex <= 0) {
		GMT_Report(API, GMT_MSG_ERROR, "Bad trace decimation: %d specified...\n", decimatex);
		Return(GMT_PARSE_ERROR);
	}

	int error = MB_ERROR_NO_ERROR;

	tracemode_t sinftracemode = MBSEGYPSD_USESHOT;
	int sinftracestart = 0;
	int sinftraceend = 0;
	int sinfchanstart = 0;
	int sinfchanend = -1;
	double sinftimesweep = 0.0;
	double sinftimedelay = 0.0;

	/* get segy limits if required */
	if (traceend < 1 || traceend < tracestart || timesweep <= 0.0) {
		get_segy_limits(verbose, segyfile, &sinftracemode, &sinftracestart, &sinftraceend, &sinfchanstart, &sinfchanend,
		                &sinftimesweep, &sinftimedelay, &error);
		if (traceend < 1 || traceend < tracestart) {
			tracemode = sinftracemode;
			tracestart = sinftracestart;
			traceend = sinftraceend;
		}
		if (chanend < 1 || chanend < chanstart) {
			chanstart = sinfchanstart;
			chanend = sinfchanend;
		}
		if (timesweep <= 0.0) {
			timesweep = sinftimesweep;
			timedelay = sinftimedelay;
		}
	}

	/* check specified parameters */
	if (traceend < 1 || traceend < tracestart) {
		/* the program exits with MBIO's error here, which can be 0 (it is when no segy file was
		   given): as a module this is the error it is */
		GMT_Report(API, GMT_MSG_ERROR, "Bad trace numbers: %d %d specified...\n", tracestart, traceend);
		Return(GMT_RUNTIME_ERROR);
	}
	if (timesweep <= 0.0) {
		GMT_Report(API, GMT_MSG_ERROR, "Bad time sweep: %f specified...\n", timesweep);
		Return(GMT_RUNTIME_ERROR);
	}

	/* initialize reading the segy file */
	void *mbsegyioptr;
	struct mb_segyasciiheader_struct asciiheader;
	struct mb_segyfileheader_struct fileheader;
	if (mb_segy_read_init(verbose, segyfile, &mbsegyioptr, &asciiheader, &fileheader, &error) != MB_SUCCESS) {
		char *message;
		mb_error(verbose, error, &message);
		GMT_Report(API, GMT_MSG_ERROR, "MBIO Error returned from function <mb_segy_read_init>: %s\n", message);
		GMT_Report(API, GMT_MSG_ERROR, "SEGY File <%s> not initialized for reading\n", segyfile);
		Return(GMT_RUNTIME_ERROR);
	}


	/* calculate implied grid parameters */
	char gridfile[MB_PATH_MAXLINE+10] = "";
	snprintf(gridfile, sizeof(gridfile), "%s.grd", fileroot);

	char psdfile[MB_PATH_MAXLINE+10] = "";
	snprintf(psdfile, sizeof(psdfile), "%s_psd.txt", fileroot);

	const int ntraces =
		chanend >= chanstart
		? (traceend - tracestart + 1) * (chanend - chanstart + 1)
		: traceend - tracestart + 1;
	ngridx = ntraces / decimatex;
	const double sampleinterval = 0.000001 * fileheader.sample_interval;
	ngridy = nfft / 2 + 1;
	ngridxy = ngridx * ngridy;
	double dx = decimatex;
	double xmin = tracestart - 0.5;
	double xmax = traceend + 0.5;
	const double dy = 1.0 / (2.0 * sampleinterval * ngridy);
	const double ymin = -0.5 * dy;
	const double ymax = (ngridy - 0.5) * dy;

	/* get start and end samples */
	int itstart =
		windowmode == MBSEGYPSD_WINDOW_OFF
		? 0
		: MAX((windowstart) / sampleinterval, 0.0);
	int itend =
		windowmode == MBSEGYPSD_WINDOW_OFF
		? ngridy - 1
		: MIN((windowend) / sampleinterval, ngridy - 1.0);


	/* allocate memory for grid array */
	float *grid = NULL;
	int status = mb_mallocd(verbose, __FILE__, __LINE__, 2 * ngridxy * sizeof(float), (void **)&grid, &error);
	double *spsd = NULL;
	status &= mb_mallocd(verbose, __FILE__, __LINE__, ngridy * sizeof(double), (void **)&spsd, &error);
	double *wpsd = NULL;
	status &= mb_mallocd(verbose, __FILE__, __LINE__, ngridy * sizeof(double), (void **)&wpsd, &error);
	double *spsdtot = NULL;
	status &= mb_mallocd(verbose, __FILE__, __LINE__, ngridy * sizeof(double), (void **)&spsdtot, &error);
	double *wpsdtot = NULL;
	status &= mb_mallocd(verbose, __FILE__, __LINE__, ngridy * sizeof(double), (void **)&wpsdtot, &error);

	if (status != MB_SUCCESS) {
		GMT_Report(API, GMT_MSG_ERROR, "Unable to allocate grid/psd arrays\n");
		Return(GMT_MEMORY_ERROR);
	}

	/* zero working psd array */
	for (int iy = 0; iy < ngridy; iy++) {
		spsdtot[iy] = 0.0;
		wpsdtot[iy] = 0.0;
	}

	if (verbose >= 0) {
		fprintf(outfp, "\nMBsegypsd Parameters:\n");
		fprintf(outfp, "Input segy file:         %s\n", segyfile);
		fprintf(outfp, "Output fileroot:         %s\n", fileroot);
		fprintf(outfp, "Input Parameters:\n");
		fprintf(outfp, "     trace mode:         %d\n", tracemode);
		fprintf(outfp, "     trace start:        %d\n", tracestart);
		fprintf(outfp, "     trace end:          %d\n", traceend);
		fprintf(outfp, "     channel start:      %d\n", chanstart);
		fprintf(outfp, "     channel end:        %d\n", chanend);
		fprintf(outfp, "     trace decimation:   %d\n", decimatex);
		fprintf(outfp, "     time sweep:         %f seconds\n", timesweep);
		fprintf(outfp, "     time delay:         %f seconds\n", timedelay);
		fprintf(outfp, "     sample interval:    %f seconds\n", sampleinterval);
		fprintf(outfp, "     window mode:        %d\n", windowmode);
		fprintf(outfp, "     window start:       %f seconds\n", windowstart);
		fprintf(outfp, "     window end:         %f seconds\n", windowend);
		fprintf(outfp, "Output Parameters:\n");
		fprintf(outfp, "     grid filename:      %s\n", gridfile);
		fprintf(outfp, "     psd filename:       %s\n", psdfile);
		fprintf(outfp, "     x grid dimension:   %d\n", ngridx);
		fprintf(outfp, "     y grid dimension:   %d\n", ngridy);
		fprintf(outfp, "     grid xmin:          %f\n", xmin);
		fprintf(outfp, "     grid xmax:          %f\n", xmax);
		fprintf(outfp, "     grid ymin:          %f\n", ymin);
		fprintf(outfp, "     grid ymax:          %f\n", ymax);
		fprintf(outfp, "     NaN values used to flag regions with no data\n");
		fprintf(outfp, "     shotscale:          %f\n", shotscale);
		fprintf(outfp, "     frequencyscale:     %f\n", frequencyscale);
		if (scale2distance) {
			fprintf(outfp, "     trace numbers scaled to distance in meters\n");
			fprintf(outfp, "     scaled grid xmin    %f\n", 0.0);
			fprintf(outfp, "     scaled grid xmax:   %f\n", shotscale * (xmax - xmin));
		}
	}
	if (verbose > 0)
		fprintf(outfp, "\n");

	/* grid controls */
	double gridmintot = 0.0;
	double gridmaxtot = 0.0;

	if (status == MB_SUCCESS) {

		/* fill grid with NaNs */
		for (int i = 0; i < ngridxy; i++)
			grid[i] = NAN;

		/* generate the fftw plan */
		fftw_complex *fftw_in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * nfft);
		fftw_complex *fftw_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * nfft);
		const fftw_plan plan = fftw_plan_dft_1d(nfft, fftw_in, fftw_out, FFTW_FORWARD, FFTW_MEASURE);


		/* read and print data */
		int nread = 0;
		// double btime;
		// double btimesave = 0.0;
		while (error <= MB_ERROR_NO_ERROR) {
			/* reset error */
			error = MB_ERROR_NO_ERROR;

			/* read a trace */
			struct mb_segytraceheader_struct traceheader;
			float *trace = NULL;
			status = mb_segy_read_trace(verbose, mbsegyioptr, &traceheader, &trace, &error);

			/* now process the trace */
			if (status == MB_SUCCESS) {
				/* figure out where this trace is in the grid */
				const int tracenum =
					tracemode == MBSEGYPSD_USESHOT
					? traceheader.shot_num : traceheader.rp_num;
				const int channum =
					tracemode == MBSEGYPSD_USESHOT
					? traceheader.shot_tr : traceheader.rp_tr;
				const int tracecount =
					chanend >= chanstart
					? (tracenum - tracestart) * (chanend - chanstart + 1) + (channum - chanstart)
					: tracenum - tracestart;
				const int ix = tracecount / decimatex;
				// TODO(schwehr): Why are these casting through float?
				const double factor =
					traceheader.elev_scalar < 0
					? 1.0 / (float)(-traceheader.elev_scalar)
					: (float)traceheader.elev_scalar;
				double dtime;
				double dtimesave = 0.0;
				if (traceheader.src_depth > 0) {
					// btime = factor * traceheader.src_depth / 750.0 + 0.001 * traceheader.delay_mils;
					dtime = factor * traceheader.src_depth / 750.0;
					// btimesave = btime;
					dtimesave = dtime;
				}
				else if (traceheader.src_elev > 0) {
					// btime = -factor * traceheader.src_elev / 750.0 + 0.001 * traceheader.delay_mils;
					dtime = -factor * traceheader.src_elev / 750.0;
					// btimesave = btime;
					dtimesave = dtime;
				}
				else {
					// btime = btimesave;
					dtime = dtimesave;
				}
				double stime;
				double stimesave = 0.0;
				if (traceheader.src_wbd > 0) {
					stime = factor * traceheader.src_wbd / 750.0;
					stimesave = stime;
				}
				else {
					stime = stimesave;
				}
				// const int iys = (btime - timedelay) / sampleinterval;

				/* now check if this is a trace of interest */
				bool traceok = true;
				if (tracenum < tracestart || tracenum > traceend)
					traceok = false;
				else if (chanend >= chanstart && (channum < chanstart || channum > chanend))
					traceok = false;
				else if (tracecount % decimatex != 0)
					traceok = false;

				/* get trace min and max */
				double tracemin = trace[0];
				double tracemax = trace[0];
				for (int i = 0; i < traceheader.nsamps; i++) {
					tracemin = MIN(tracemin, (double)(trace[i]));
					tracemax = MAX(tracemax, (double)(trace[i]));
				}

				if ((verbose == 0 && nread % 250 == 0) || (nread % 25 == 0)) {
					if (traceok)
						fprintf(outfp, "PROCESS ");
					else
						fprintf(outfp, "IGNORE  ");
					if (tracemode == MBSEGYPSD_USESHOT)
						fprintf(outfp, "read:%d position:%d shot:%d channel:%d ", nread, tracecount, tracenum, channum);
					else
						fprintf(outfp, "read:%d position:%d rp:%d channel:%d ", nread, tracecount, tracenum, channum);
					fprintf(outfp, "%4.4d/%3.3d %2.2d:%2.2d:%2.2d.%3.3d samples:%d interval:%d usec minmax: %f %f\n",
					        traceheader.year, traceheader.day_of_yr, traceheader.hour, traceheader.min, traceheader.sec,
					        traceheader.mils, traceheader.nsamps, traceheader.si_micros, tracemin, tracemax);
				}

				/* now actually process traces of interest */
				if (traceok) {
					/* zero working psd array */
					for (int iy = 0; iy < ngridy; iy++) {
						spsd[iy] = 0.0;
						wpsd[iy] = 0.0;
					}

					/* get bounds of trace in depth window mode */
					if (windowmode == MBSEGYPSD_WINDOW_DEPTH) {
						itstart = (int)((dtime + windowstart - timedelay) / sampleinterval);
						itstart = MAX(itstart, 0);
						itend = (int)((dtime + windowend - timedelay) / sampleinterval);
						itend = MIN(itend, ngridy - 1);
					}
					else if (windowmode == MBSEGYPSD_WINDOW_SEAFLOOR) {
						itstart = MAX((stime + windowstart - timedelay) / sampleinterval, 0.0);
						itend = MIN((stime + windowend - timedelay) / sampleinterval, ngridy - 1.0);
					}

					/* loop over the data calculating fft in nfft long sections */
					int nsection = (itend - itstart + 1) / nfft;
					if (((itend - itstart + 1) % nfft) > 0)
						nsection++;
					for (int j = 0; j < nsection; j++) {
						/* initialize normalization factors */
						double normraw = 0.0;
						// double normtaper = 0.0;
						double normfft = 0.0;

						/* extract data section to be fft'd with taper */
						const int kstart = itstart + j * nfft;
						const int kend = MIN(kstart + nfft, itend);
						for (int i = 0; i < nfft; i++) {
							const int k = itstart + j * nfft + i;
							if (k <= kend) {
								const double sint = sin(M_PI * ((double)(k - kstart)) / ((double)(kend - kstart)));
								const double taper = sint * sint;
								fftw_in[i][0] = taper * trace[k];
								normraw += trace[k] * trace[k];
								// normtaper += fftw_in[i][0] * fftw_in[i][0];
							}
							else
								fftw_in[i][0] = 0.0;
							/*if (ix < 500)
							fftw_in[i][0] = sin(2.0 * M_PI * 1000.0 * i * sampleinterval)
							            + sin(2.0 * M_PI * 3000.0 * i * sampleinterval)
							            + sin(2.0 * M_PI * 6000.0 * i * sampleinterval);*/
							fftw_in[i][1] = 0.0;
						}
						// const double soundpressurelevel = 20.0 * log10(normraw / nfft);

						/* execute the fft */
						fftw_execute(plan);

						/* get normalization factor - require variance of transform to equal variance of input */
						for (int i = 1; i < nfft; i++) {
							normfft += fftw_out[i][0] * fftw_out[i][0] + fftw_out[i][1] * fftw_out[i][1];
						}
						const double norm = normraw / normfft;

						/* apply normalization factor */
						for (int i = 1; i < nfft; i++) {
							fftw_out[i][0] = norm * fftw_out[i][0];
							fftw_out[i][1] = norm * fftw_out[i][1];
						}

						/* calculate psd from result of transform */
						spsd[0] += fftw_out[0][0] * fftw_out[0][0] + fftw_out[0][1] * fftw_out[0][1];
						wpsd[0] += 1.0;

						int i = 1;  // Used after for.
						for (; i < nfft / 2; i++) {
							spsd[i] += 2.0 * (fftw_out[i][0] * fftw_out[i][0] + fftw_out[i][1] * fftw_out[i][1]);
							wpsd[i] += 1.0;
						}
						if (nfft % 2 == 0) {
							spsd[i] +=
							    fftw_out[nfft / 2][0] * fftw_out[nfft / 2][0] + fftw_out[nfft / 2][1] * fftw_out[nfft / 2][1];
							wpsd[i] += 1.0;
						}
					}

					/* output psd for this trace to the grid */
					for (int iy = 0; iy < ngridy; iy++) {
						const int k = (ngridy - 1 - iy) * ngridx + ix;
						if (wpsd[iy] > 0.0) {
							if (!logscale)
								grid[k] = spsd[iy] / wpsd[iy];
							else
								grid[k] = 20.0 * log10(spsd[iy] / wpsd[iy]);
							spsdtot[iy] += grid[k];
							wpsdtot[iy] += 1.0;
							gridmintot = MIN((double)(grid[k]), gridmintot);
							gridmaxtot = MAX((double)(grid[k]), gridmaxtot);
						}
					}
				}
			}

			/* now process the trace */
			if (status == MB_SUCCESS)
				nread++;
		}

		/* deallocate fftw arrays and plan */
		fftw_destroy_plan(plan);
		fftw_free(fftw_in);
		fftw_free(fftw_out);
	}

	/* write out the grid */
	error = MB_ERROR_NO_ERROR;
	status = MB_SUCCESS;
	char projection[MB_PATH_MAXLINE] = "GenericLinear";
	char xlabel[MB_PATH_MAXLINE] = "";
	char ylabel[MB_PATH_MAXLINE] = "";
	if (scale2distance) {
		strcpy(xlabel, "Distance (m)");
		strcpy(ylabel, "Frequency (Hz)");
		xmax *= shotscale;
		xmin *= shotscale;
		dx *= shotscale;
	}
	else {
		strcpy(xlabel, "Trace Number");
		strcpy(ylabel, "Frequency (Hz)");
		dx = (double)decimatex;
	}
	char zlabel[MB_PATH_MAXLINE] = "";
	if (logscale)
		strcpy(zlabel, "dB/Hz");
	else
		strcpy(zlabel, "Intensity/Hz");
	char title[MB_PATH_MAXLINE+50] = "";
	snprintf(title, sizeof(title), "Power Spectral Density Grid from %s", segyfile);
	char **hist_argv = NULL;
	const int hist_argc = mb_gmt_history_args(API, options, program_name, &hist_argv);
	status &= mb_write_gmt_grd(
		verbose, gridfile, grid, NAN,
		ngridx, ngridy, xmin, xmax, ymin, ymax, gridmintot, gridmaxtot, dx,
		dy, xlabel, ylabel, zlabel, title, projection, hist_argc, hist_argv, &error);
	mb_gmt_history_free(API, hist_argc, hist_argv);

	/* output average power spectra */
	FILE *fp  = fopen(psdfile, "w");
	if (fp != NULL) {
		for (int iy = 0; iy < ngridy; iy++) {
			if (wpsdtot[iy] > 0.0) {
				spsdtot[iy] = spsdtot[iy] / wpsdtot[iy];
			}
			fprintf(fp, "%f %f\n", dy * iy, spsdtot[iy]);
		}
		fclose(fp);
	}

	status &= mb_segy_close(verbose, &mbsegyioptr, &error);

	// float *worktrace = NULL;
	// status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&worktrace, &error);
	status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&grid, &error);
	status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&spsd, &error);
	status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&wpsd, &error);
	status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&spsdtot, &error);
	status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&wpsdtot, &error);

	/* run mbm_grdplot */
	double xwidth = MIN(0.01 * ngridx, 55.0);
	double ywidth = MIN(0.01 * ngridy, 28.0);
	char plot_cmd[(5*MB_PATH_MAXLINE)+100] = "";
	snprintf(plot_cmd, sizeof(plot_cmd), "mbm_grdplot -I%s -JX%f/%f -G1 -S -V -L\"File %s - %s:%s\"", gridfile, xwidth, ywidth, gridfile, title,
	        zlabel);
	if (verbose) {
		fprintf(outfp, "\nexecuting mbm_grdplot...\n%s\n", plot_cmd);
	}
	int plot_status = system(plot_cmd);
	if (plot_status == -1) {
		fprintf(outfp, "\nError executing mbm_grdplot on grid file %s\n", gridfile);
	}

	/* run mbm_xyplot */
	xwidth = 9.0;
	ywidth = 7.0;
	snprintf(plot_cmd, sizeof(plot_cmd), "mbm_xyplot -I%s -JX%f/%f -V -L\"File %s - %s:%s\"", psdfile, xwidth, ywidth, psdfile, title, zlabel);
	if (verbose) {
		fprintf(outfp, "\nexecuting mbm_xyplot...\n%s\n", plot_cmd);
	}
	plot_status = system(plot_cmd);
	if (plot_status == -1) {
		fprintf(outfp, "\nError executing mbm_xyplot on psd file %s\n", psdfile);
	}

	if (verbose >= 4)
		status &= mb_memory_list(verbose, &error);

	if (verbose >= 2) {
		fprintf(outfp, "\ndbg2  Program <%s> completed\n", program_name);
		fprintf(outfp, "dbg2  Ending status:\n");
		fprintf(outfp, "dbg2       status:  %d\n", status);
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
