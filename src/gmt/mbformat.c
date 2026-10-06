/*--------------------------------------------------------------------
 *    The MB-system:	mbformat.c	1/22/93
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
 * MBFORMAT provides a description of the swath data format
 * associated with a particular MBIO format identifier.  If
 * no format is specified, MBFORMAT will list descriptions of all
 * the currently supported formats.
 *
 * Author:	D. W. Caress
 * Date:	January 22, 1993
 *
 * GMT-module port of mbformat.cc: options from GMT's option list (the program's long options are
 * GMT long options through module_kw, its lower-case aliases are kept -- GMT_Parse_Common only
 * parses the common options named in THIS_MODULE_OPTIONS), and everything the program prints is
 * written as text records through the GMT API (mb_gmt_text.c).
 */

#define THIS_MODULE_NAME		"mbformat"
#define THIS_MODULE_LIB			"mbsystem"
#define THIS_MODULE_PURPOSE		"Describe MBIO swath data formats (id, name, attributes) or list all supported formats"
#define THIS_MODULE_KEYS		">D}"
#define THIS_MODULE_NEEDS		""
#define THIS_MODULE_OPTIONS		"->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_define.h"
#include "mb_format.h"
#include "mb_status.h"
#include "mb_gmt_text.h"

enum MbformatList {
	MBFORMAT_LIST_LONG   = 0,
	MBFORMAT_LIST_SIMPLE = 1,
	MBFORMAT_LIST_ROOT   = 2
};

static const char help_message[] =
    "MBFORMAT is an utility which identifies the swath data formats\n"
    "associated with MBIO format id's.  If no format id is specified,\n"
    "MBFORMAT lists all of the currently supported formats.";

/* Translation table from the program's long options to the module's short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'F', "format",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'W', "html",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",     "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'L', "list-ids",  "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'K', "list-root", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'v', "verbose",   "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

/* --- Control structure ---------------------------------------------- */

struct MBFORMAT_CTRL {
	struct mbf_F { bool active; int  format; } F;
	struct mbf_H { bool active; } H;
	struct mbf_I { bool active; char file[MB_PATH_MAXLINE]; } I;
	struct mbf_L { bool active; } L;
	struct mbf_K { bool active; } K;
	struct mbf_W { bool active; } W;
};

static void *New_mbformat_Ctrl(struct GMT_CTRL *GMT) {
	struct MBFORMAT_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBFORMAT_CTRL);
	return Ctrl;
}

static void Free_mbformat_Ctrl(struct GMT_CTRL *GMT, struct MBFORMAT_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Usage(API, 0, "usage: %s [-F<format>] [-H] [-I<file>] [-K] [-L] [-W] [%s]\n", THIS_MODULE_NAME, GMT_V_OPT);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n\n", help_message);
	GMT_Message(API, GMT_TIME_NONE, "  OPTIONAL ARGUMENTS:\n");
	GMT_Usage(API, 1, "\n-F<format> (--format=)");
	GMT_Usage(API, -2, "MBIO format id to describe.");
	GMT_Usage(API, 1, "\n-H (--help)");
	GMT_Usage(API, -2, "Print this help.");
	GMT_Usage(API, 1, "\n-I<file> (--input=)");
	GMT_Usage(API, -2, "Swath data file; the format is inferred from the filename.");
	GMT_Usage(API, 1, "\n-K (--list-root)");
	GMT_Usage(API, -2, "Root listing: print '<root> <format>' (filename root + format id).");
	GMT_Usage(API, 1, "\n-L (--list-ids)");
	GMT_Usage(API, -2, "Simple listing: print the format id(s) only.");
	GMT_Usage(API, 1, "\n-W (--html)");
	GMT_Usage(API, -2, "Write an HTML page describing all supported formats.");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse(struct GMT_CTRL *GMT, struct MBFORMAT_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		/* Every option keeps the program's lower-case alias: GMT_Parse_Common only touches the
		   common options named in THIS_MODULE_OPTIONS (-V), so these arrive here untouched. */
		case 'F': case 'f':
			if (opt->arg[0] && sscanf(opt->arg, "%d", &Ctrl->F.format) == 1)
				Ctrl->F.active = true;
			else {
				GMT_Report(API, GMT_MSG_ERROR, "Option -F: expected an integer format id\n");
				n_errors++;
			}
			break;
		case 'H': case 'h':
			Ctrl->H.active = true;
			break;
		case 'I': case 'i':
			if (opt->arg[0]) {
				strncpy(Ctrl->I.file, opt->arg, MB_PATH_MAXLINE - 1);
				Ctrl->I.file[MB_PATH_MAXLINE - 1] = '\0';
				Ctrl->I.active = true;
			}
			else {
				GMT_Report(API, GMT_MSG_ERROR, "Option -I: expected a file name\n");
				n_errors++;
			}
			break;
		case 'L': case 'l':
			Ctrl->L.active = true;
			break;
		case 'K': case 'k':
			Ctrl->K.active = true;
			break;
		case 'W': case 'w':
			Ctrl->W.active = true;
			break;
		case 'v':	/* the program's -v: verbosity, as -V */
			GMT->current.setting.verbose = GMT_MSG_INFORMATION;
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}

	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code)  { gmt_M_free_options(mode); return code; }
#define Return(code)   { Free_mbformat_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }

EXTERN_MSC int GMT_mbformat(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/
int GMT_mbformat(void *V_API, int mode, void *args) {
	int error = MB_ERROR_NO_ERROR;

	struct MBFORMAT_CTRL *Ctrl = NULL;
	struct GMT_CTRL      *GMT  = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION    *options = NULL;
	struct MB_GMT_TEXT   *T = NULL;
	struct GMTAPI_CTRL   *API = gmt_get_api_ptr(V_API);

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;

	/* 1: no options at all is a normal run (list every format), not a usage request */
	if ((error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(error);

	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS,
	                           module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	Ctrl = New_mbformat_Ctrl(GMT);
	if ((error = parse(GMT, Ctrl, options))) Return(error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	const int verbose = (GMT->current.setting.verbose >= GMT_MSG_DEBUG) ? 2 : 0;

	const bool file_specified   = Ctrl->I.active;
	const bool format_specified = Ctrl->F.active;
	const bool html             = Ctrl->W.active;
	int format = format_specified ? Ctrl->F.format : 0;
	char file[MB_PATH_MAXLINE];
	strncpy(file, Ctrl->I.file, MB_PATH_MAXLINE);

	enum MbformatList list_mode = MBFORMAT_LIST_LONG;
	if (Ctrl->L.active) list_mode = MBFORMAT_LIST_SIMPLE;
	if (Ctrl->K.active) list_mode = MBFORMAT_LIST_ROOT;

	GMT_Report(API, GMT_MSG_INFORMATION, "Program %s\n", THIS_MODULE_NAME);
	GMT_Report(API, GMT_MSG_INFORMATION, "MB-system Version %s\n", MB_VERSION);
	if (format_specified) GMT_Report(API, GMT_MSG_DEBUG, "format:  %d\n", format);
	if (file_specified) GMT_Report(API, GMT_MSG_DEBUG, "file:    %s\n", file);

	if ((T = mb_gmt_text_begin(GMT, options)) == NULL)
		Return(API->error);

	int status = MB_SUCCESS;

	/* print out the info */
	int format_save = format;
	char root[MB_PATH_MAXLINE] = {""};
	if (file_specified) {
		status = mb_get_format(verbose, file, root, &format, &error);
	}
	else if (format_specified) {
		status = mb_format(verbose, &format, &error);
	}

	if (file_specified && format == 0) {
		if (list_mode == MBFORMAT_LIST_SIMPLE)
			mb_gmt_text_put(T,"%d\n", format);
		else if (list_mode == MBFORMAT_LIST_ROOT)
			mb_gmt_text_put(T,"%s %d\n", root, format);
		else
			mb_gmt_text_put(T,"Program %s unable to infer format from filename %s\n", THIS_MODULE_NAME, file);
	}
	else if (format_specified && format == 0) {
		if (list_mode == MBFORMAT_LIST_SIMPLE)
			mb_gmt_text_put(T,"%d\n", format);
		else if (list_mode == MBFORMAT_LIST_ROOT)
			mb_gmt_text_put(T,"%s %d\n", root, format);
		else
			mb_gmt_text_put(T,"Specified format %d invalid for MB-System\n", format_save);
	}
	else if (format != 0) {
		if (list_mode == MBFORMAT_LIST_SIMPLE) {
			mb_gmt_text_put(T,"%d\n", format);
		}
		else if (list_mode == MBFORMAT_LIST_ROOT) {
			mb_gmt_text_put(T,"%s %d\n", root, format);
		}
		else {
			char format_description[MB_DESCRIPTION_LENGTH];
			status = mb_format_description(verbose, &format, format_description, &error);
			if (status == MB_SUCCESS) {
				mb_gmt_text_put(T,"\nMBIO data format id: %d\n", format);
				mb_gmt_text_put(T,"%s", format_description);
			}
			else if (file_specified) {
				mb_gmt_text_put(T,"Program %s unable to infer format from filename %s\n", THIS_MODULE_NAME, file);
			}
			else if (format_specified) {
				mb_gmt_text_put(T,"Specified format %d invalid for MB-System\n", format_save);
			}
		}
	}
	else if (html) {
		mb_gmt_text_put(T,"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">\n");
		mb_gmt_text_put(T,"<HTML>\n<HEAD>\n   <TITLE>MB-System Supported Data Formats</TITLE>\n");
		mb_gmt_text_put(T,"</HEAD>\n<BODY TEXT=\"#000000\" BGCOLOR=\"#FFFFFF\" LINK=\"#336699\" VLINK=\"#997040\" ALINK=\"#CC9900\">\n\n");
		mb_gmt_text_put(T,"<CENTER><P><B><FONT SIZE=+2>MB-System Supported Swath Data Formats</FONT></B></P></CENTER>\n\n");
		mb_gmt_text_put(T,"<P>Each swath mapping sonar system outputs a data stream which includes\n");
		mb_gmt_text_put(T,"some values or parameters unique to that system. In general, a number of\n");
		mb_gmt_text_put(T,"different data formats have come into use for data from each of the sonar\n");
		mb_gmt_text_put(T,"systems; many of these formats include only a subset of the original data\n");
		mb_gmt_text_put(T,"stream. Internally, MBIO recognizes which sonar system each data format\n");
		mb_gmt_text_put(T,"is associated with and uses a data structure including the complete data\n");
		mb_gmt_text_put(T,"stream for that sonar. At present, formats associated with the following\n");
		mb_gmt_text_put(T,"sonars are supported: </P>\n\n");
		mb_gmt_text_put(T,"<UL>\n<LI>Sea Beam &quot;classic&quot; multibeam sonar </LI>\n\n");
		mb_gmt_text_put(T,"<LI>Hydrosweep DS multibeam sonar </LI>\n\n");
		mb_gmt_text_put(T,"<LI>Hydrosweep DS2 multibeam sonar </LI>\n\n");
		mb_gmt_text_put(T,"<LI>Hydrosweep MD multibeam sonar </LI>\n\n");
		mb_gmt_text_put(T,"<LI>Sea Beam 2000 multibeam sonar </LI>\n\n");
		mb_gmt_text_put(T,"<LI>Sea Beam 2112 and 2136 multibeam sonars </LI>\n\n");
		mb_gmt_text_put(T,"<LI>Sea Beam 2120 multibeam sonars </LI>\n\n");
		mb_gmt_text_put(T,"<LI>Simrad EM12, EM121, EM950, and EM1000 multibeam sonars </LI>\n\n");
		mb_gmt_text_put(T,"<LI>Simrad EM120, EM300, and EM3000 multibeam sonars</LI>\n\n");
		mb_gmt_text_put(T,"<LI>Simrad EM122, EM302, EM710, and EM3002 multibeam sonars</LI>\n\n");
		mb_gmt_text_put(T,"<LI>Simrad Mesotech SM2000 multibeam sonar</LI>\n\n");
		mb_gmt_text_put(T,"<LI>Hawaii MR-1 shallow tow interferometric sonar </LI>\n\n");
		mb_gmt_text_put(T,"<LI>ELAC Bottomchart and Bottomchart MkII shallow water multibeam sonars</LI>\n\n");
		mb_gmt_text_put(T,"<LI>Reson Seabat multibeam sonars (e.g. 9001, 8081, 7125)</LI>\n\n");
		mb_gmt_text_put(T,"<LI>WHOI DSL AMS-120 deep tow interferometric sonar </LI>\n\n");
		mb_gmt_text_put(T,"<LI>Sea Scan sidescan sonar</LI>\n\n");
		mb_gmt_text_put(T,"<LI>Furuno HS-1 multibeam sonar</LI>\n\n");
		mb_gmt_text_put(T,"<LI>Edgetech sidescan and subbottom profiler sonars</LI>\n\n");
		mb_gmt_text_put(T,"<LI>Imagenex DeltaT multibeam sonars</LI>\n\n");
		mb_gmt_text_put(T,"<LI>Odom ES3 multibeam sonar</LI>\n\n");
		mb_gmt_text_put(T,"</UL>\n\n");
		mb_gmt_text_put(T,"<P>The following swath mapping sonar data formats are currently supported by MB-System:</P>\n\n");

		for (int i = 0; i <= 1000; i++) {
			format = i;
			char format_description[MB_DESCRIPTION_LENGTH];
			if ((status = mb_format_description(verbose, &format, format_description, &error)) == MB_SUCCESS && format == i) {
				const char *format_informal_ptr   = strstr(format_description, "Informal Description:");
				const char *format_attributes_ptr = strstr(format_description, "Attributes:");
				char format_name[MB_DESCRIPTION_LENGTH];
				size_t format_name_len = MIN(MB_DESCRIPTION_LENGTH, strlen(format_description) - strlen(format_informal_ptr));
				strncpy(format_name, format_description, MIN(format_name_len, sizeof (format_description)));
				format_name[format_name_len - 1] = '\0';
				char format_informal[MB_DESCRIPTION_LENGTH];
				size_t format_informal_len = MIN(MB_DESCRIPTION_LENGTH, strlen(format_informal_ptr) - strlen(format_attributes_ptr));
				strncpy(format_informal, format_informal_ptr, MIN(format_informal_len, sizeof (format_description)));
				format_informal[format_informal_len - 1] = '\0';
				char format_attributes[MB_DESCRIPTION_LENGTH];
				strcpy(format_attributes, format_attributes_ptr);
				format_attributes[strlen(format_attributes_ptr) - 1] = '\0';
				mb_gmt_text_put(T,"\n<UL>\n<LI>MBIO Data Format ID:  %d </LI>\n", format);
				mb_gmt_text_put(T,"\n<UL>\n<LI>%s</LI>\n", format_name);
				mb_gmt_text_put(T,"\n<LI>%s</LI>\n", format_informal);
				mb_gmt_text_put(T,"\n<LI>%s</LI>\n", format_attributes);
				mb_gmt_text_put(T,"</UL>\n</UL>\n");
			}
		}

		mb_gmt_text_put(T,"\n<CENTER><P><BR>\n");
		mb_gmt_text_put(T,"\n<P>\n<HR WIDTH=\"67%%\"></P>\n\n");
		mb_gmt_text_put(T,"<center>\n");
		mb_gmt_text_put(T,"\t<a href=\"https://www.mbari.org/products/research-software/mb-system/\"><< MB-System website</a> "
		                          "| <a href=\"https://www.mbari.org/technology/mb-system/documentation/\"> Online MB-System Documenation>></a> "
		                          "| <a href=\"index.html\">MB-System Information in Local Installation</a></p>\n");
		mb_gmt_text_put(T,"</center>\n");
		mb_gmt_text_put(T,"\n</BODY>\n</HTML>\n");

		/* as the two listings below do: the loop's last failed lookup is not an error of the run */
		status = MB_SUCCESS;
		error  = MB_ERROR_NO_ERROR;
	}
	else if (list_mode) {
		for (int i = 0; i <= 1000; i++) {
			format = i;
			if ((status = mb_format(verbose, &format, &error)) == MB_SUCCESS && format == i) {
				mb_gmt_text_put(T,"%d\n", format);
			}
		}
		status = MB_SUCCESS;
		error  = MB_ERROR_NO_ERROR;
	}
	else {
		mb_gmt_text_put(T,"\nSupported MBIO Formats:\n");
		for (int i = 0; i <= 1000; i++) {
			format = i;
			char format_description[MB_DESCRIPTION_LENGTH];
			if ((status = mb_format_description(verbose, &format, format_description, &error)) == MB_SUCCESS && format == i) {
				mb_gmt_text_put(T,"\nMBIO Data Format ID:  %d\n", format);
				mb_gmt_text_put(T,"%s", format_description);
			}
		}
		status = MB_SUCCESS;
		error  = MB_ERROR_NO_ERROR;
	}

	if (mb_gmt_text_end(T))
		Return(API->error);

	GMT_Report(API, GMT_MSG_DEBUG, "Program <%s> completed, status %d\n", THIS_MODULE_NAME, status);

	/* The program exits with MBIO's error (non-zero for a format it cannot infer or does not know);
	   as a module that is a GMT error code, never an MBIO one, with MBIO's reason on stderr: a
	   caller whose run fails gets no records, so the listing's own line cannot tell it */
	if (error != MB_ERROR_NO_ERROR) {
		char *message = NULL;
		mb_error(verbose, error, &message);
		GMT_Report(API, GMT_MSG_ERROR, "%s\n", message);
		Return(GMT_RUNTIME_ERROR);
	}
	Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
