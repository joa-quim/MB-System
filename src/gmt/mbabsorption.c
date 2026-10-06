/*--------------------------------------------------------------------
 *    The MB-system:	mbabsorption.c	2/10/2008
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
 * MBabsorption calculates the absorption of sound in sea water in dB/km
 * as a function of frequency, temperature, salinity, sound speed, and depth.
 *
 * We use the Francois and Garrison equations from:
 *     Francois, R.E., Garrison, G.R., "Sound absorption based
 *       on ocean measurements: Part I: Pure water and magnesium
 *       sulfate contributions", J. Acoust. Soc. Am., 72(3),
 *       896-907, 1982.
 *     Francois, R.E., Garrison, G.R., "Sound absorption based
 *       on ocean measurements: Part II: Boric acid contribution
 *       and equation for total absorption", J. Acoust. Soc. Am.,
 *       72(6), 1879-1890, 1982.
 *
 * Francois and Garrison [1982] model the sound absorption in
 * sea water as resulting from contributions from pure water,
 * magnesium sulfate, and boric acid. The boric acid contribution
 * is significant below 10 kHz. The equations are:
 *
 * absorption = Boric Acid Contribution
 * 		+ MbSO4 Contribution
 * 		+ Pure Water Contribution
 *
 * **************************
 *
 * Boric Acid Contribution
 * AlphaB = Ab * Pb * Fb * f**2
 *          -------------------
 *             f**2 + Fb**2
 *
 * Ab = 8.86 / c * 10**(0.78 * pH - 5) (dB/km/kHz)
 * Pb = 1
 * Fb = 2.8 * (S / 35)**0.5 * 10**(4 - 1245 / Tk) (kHz)
 *
 * **************************
 *
 * MgSO4 Contribution
 * AlphaM = Am * Pm * Fm * f**2
 *          -------------------
 *             f**2 + Fm**2
 *
 * Am = 21.44 * S * (1 + 0.025 * T) / c (dB/km/kHZ)
 * Pm = 1 - 0.000137 * D + 0.0000000062 * D**2
 * Fm = (8.17 * 10**(8 - 1990 / Tk)) / (1 + 0.0018 * (S - 35))  (kHz)
 *
 * **************************
 *
 * Pure Water Contribution
 * AlphaW = Aw * Pw * f**2
 *
 * For T <= 20 deg C
 *   Aw = 0.0004397 - 0.0000259 * T
 *           + 0.000000911 * T**2 - 0.000000015 * T**3 (dB/km/kHz)
 * For T > 20 deg C
 *   Aw = 0.0003964 - 0.00001146 * T
 *           + 0.000000145 * T**2 - 0.00000000049 * T**3 (dB/km/kHz)
 * Pw = 1 - 0.0000383 * D + 0.00000000049 * D**2
 *
 * **************************
 *
 * f = sound frequency (kHz)
 * c = speed of sound (m/s)
 *   =~ 1412 + 3.21 * T + 1.19 * S + 0.0167 * D
 * T = temperature (deg C)
 * Tk = temperature (deg K) = T + 273 (deg K)
 * S = salinity (per mil)
 * D = depth (m)
 *
 * **************************
 *
 * Author:	D. W. Caress
 * Date:	February 10, 2008
 *              R/V Zephyr
 *              Hanging out at the channel entrance to La Paz, BCS, MX
 *              helping out as MBARI tries to save the grounded
 *              R/V Western Flyer.
 *              Note: as I was writing this code the Flyer was refloated
 *              and successfully backed off the reef.
 *
 * GMT-module port of src/utilities/mbabsorption.cc: options parsed in parse() from GMT's option
 * list, the program's long options kept through module_kw and its lower-case aliases kept; the
 * result printed through the GMT API (mb_gmt_text.c); every Return() a GMT error code.
 */

#define THIS_MODULE_NAME		"mbabsorption"
#define THIS_MODULE_LIB			"mbsystem"
#define THIS_MODULE_PURPOSE		"Compute absorption of sound in sea water (dB/km) from frequency, T, S, c, pH, depth"
#define THIS_MODULE_KEYS		">D}"
#define THIS_MODULE_NEEDS		""
#define THIS_MODULE_OPTIONS		"->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_define.h"
#include "mb_status.h"
#include "mb_gmt_text.h"

static const char help_message[] =
    "MBabsorption calculates the absorption of sound in sea water\n"
    "in dB/km as a function of frequency, temperature, salinity,\n"
    "sound speed, pH, and depth.";
static const char usage_message[] =
    "mbabsorption [-Csoundspeed -Ddepth -Ffrequency -Pph -Ssalinity -Ttemperature -V -H]";

/* --- Control structure ---------------------------------------------- */

struct MBABSORPTION_CTRL {
	int verbose;	/* the program's -V/-v count */
	struct mba_C { bool active; double soundspeed; } C;
	struct mba_D { bool active; double depth;      } D;
	struct mba_F { bool active; double frequency;  } F;
	struct mba_H { bool active; } H;
	struct mba_P { bool active; double ph;         } P;
	struct mba_S { bool active; double salinity;   } S;
	struct mba_T { bool active; double temperature;} T;
};

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'v', "verbose",     "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'C', "sound-speed", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'D', "depth",       "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'F', "frequency",   "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'P', "ph",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'S', "salinity",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'T', "temperature", "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static void *New_mbabsorption_Ctrl(struct GMT_CTRL *GMT) {
	struct MBABSORPTION_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBABSORPTION_CTRL);
	/* defaults match the original mbabsorption.cc */
	Ctrl->F.frequency   = 200.0;
	Ctrl->T.temperature = 10.0;
	Ctrl->S.salinity    = 35.0;
	Ctrl->C.soundspeed  = 0.0;
	Ctrl->D.depth       = 0.0;
	Ctrl->P.ph          = 8.0;
	return Ctrl;
}

static void Free_mbabsorption_Ctrl(struct GMT_CTRL *GMT, struct MBABSORPTION_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "\n%s\n\n", help_message);
	GMT_Message(API, GMT_TIME_NONE,
		"\t-C Speed of sound (m/sec). Default: derived from T, S, D.\n"
		"\t-D Depth (m). Default: 0.\n"
		"\t-F Sound frequency (kHz). Default: 200.\n"
		"\t-P pH. Default: 8.\n"
		"\t-S Salinity (per mil). Default: 35.\n"
		"\t-T Temperature (deg C). Default: 10.\n"
		"\t-H Print description and exit.\n"
		"\tEvery option also has the program's lower-case and long forms (--sound-speed, --depth,\n"
		"\t--frequency, --ph, --salinity, --temperature, --verbose, --help).\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

/* one -X<value> option of the program: a number, or a syntax error */
static unsigned int mba_number(struct GMTAPI_CTRL *API, struct GMT_OPTION *opt, const char *what, double *value, bool *active) {
	if (opt->arg && opt->arg[0] && sscanf(opt->arg, "%lf", value) == 1) {
		*active = true;
		return 0;
	}
	GMT_Report(API, GMT_MSG_ERROR, "Syntax error -%c option: expected %s\n", opt->option, what);
	return 1;
}

static int parse(struct GMT_CTRL *GMT, struct MBABSORPTION_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (struct GMT_OPTION *opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case 'V': case 'v': Ctrl->verbose++; break;
		case 'H': case 'h': Ctrl->H.active = true; break;
		case 'C': case 'c': n_errors += mba_number(API, opt, "soundspeed (m/sec)", &Ctrl->C.soundspeed, &Ctrl->C.active); break;
		case 'D': case 'd': n_errors += mba_number(API, opt, "depth (m)", &Ctrl->D.depth, &Ctrl->D.active); break;
		case 'F': case 'f': n_errors += mba_number(API, opt, "frequency (kHz)", &Ctrl->F.frequency, &Ctrl->F.active); break;
		case 'P': case 'p': n_errors += mba_number(API, opt, "pH", &Ctrl->P.ph, &Ctrl->P.active); break;
		case 'S': case 's': n_errors += mba_number(API, opt, "salinity (per mil)", &Ctrl->S.salinity, &Ctrl->S.active); break;
		case 'T': case 't': n_errors += mba_number(API, opt, "temperature (deg C)", &Ctrl->T.temperature, &Ctrl->T.active); break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}

	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code)  { gmt_M_free_options(mode); return code; }
#define Return(code)   { if (T) mb_gmt_text_end(T); Free_mbabsorption_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }

EXTERN_MSC int GMT_mbabsorption(void *API, int mode, void *args);

/*--------------------------------------------------------------------*/
int GMT_mbabsorption(void *V_API, int mode, void *args) {
	int error = MB_ERROR_NO_ERROR;
	int gmt_error;

	struct MBABSORPTION_CTRL *Ctrl = NULL;
	struct MB_GMT_TEXT   *T = NULL;
	struct GMT_CTRL      *GMT  = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION    *options = NULL;
	struct GMTAPI_CTRL   *API = gmt_get_api_ptr(V_API);

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program (absorption at its default conditions) */
	if ((gmt_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(gmt_error);

	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	Ctrl = New_mbabsorption_Ctrl(GMT);
	if ((gmt_error = parse(GMT, Ctrl, options)) != GMT_NOERROR) Return(gmt_error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	const int verbose = Ctrl->verbose;
	const double frequency   = Ctrl->F.frequency;
	const double temperature = Ctrl->T.temperature;
	const double salinity    = Ctrl->S.salinity;
	double       soundspeed  = Ctrl->C.soundspeed;
	const double depth       = Ctrl->D.depth;
	const double ph          = Ctrl->P.ph;

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s>\n", THIS_MODULE_NAME);
		fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
		fprintf(stderr, "dbg2  Control Parameters:\n");
		fprintf(stderr, "dbg2       verbose:    %d\n", verbose);
		fprintf(stderr, "dbg2       frequency:  %f\n", frequency);
		fprintf(stderr, "dbg2       temperature:%f\n", temperature);
		fprintf(stderr, "dbg2       salinity:   %f\n", salinity);
		fprintf(stderr, "dbg2       soundspeed: %f\n", soundspeed);
		fprintf(stderr, "dbg2       depth:      %f\n", depth);
		fprintf(stderr, "dbg2       ph:         %f\n", ph);
	}

	/* call function to calculate absorption */
	double absorption;  /* absorption (dB/km) */
	double density;     /* kg/m3 */

	int status = mb_absorption(verbose, frequency, temperature, salinity, depth, ph, soundspeed, &absorption, &error);
	const double pressure = 1.006 * depth;  /* depth (m) */
	status &= mb_seabird_density(verbose, salinity, temperature, pressure, &density, &error);

	/* the result: what the program prints on stdout, through the GMT API */
	if ((T = mb_gmt_text_begin(GMT, options)) == NULL) Return(API->error);
	if (verbose > 0) {
		mb_gmt_text_put(T, "\nProgram <%s>\n", THIS_MODULE_NAME);
		mb_gmt_text_put(T, "MB-system Version %s\n", MB_VERSION);
		mb_gmt_text_put(T, "Input Parameters:\n");
		mb_gmt_text_put(T, "     Frequency:        %f kHz\n", frequency);
		mb_gmt_text_put(T, "     Temperature:      %f deg C\n", temperature);
		mb_gmt_text_put(T, "     Salinity:         %f per mil\n", salinity);
		if (soundspeed > 0.0)
			mb_gmt_text_put(T, "     Soundspeed:       %f m/sec\n", soundspeed);
		mb_gmt_text_put(T, "     Depth:            %f m\n", depth);
		mb_gmt_text_put(T, "     pH:               %f\n", ph);
		mb_gmt_text_put(T, "Result:\n");
		mb_gmt_text_put(T, "     Sound absorption: %f dB/km\n", absorption);
		mb_gmt_text_put(T, "     Density:          %f kg/m3\n", density);
	}
	else {
		mb_gmt_text_put(T, "%f\n", absorption);
	}
	const int output_failed = mb_gmt_text_end(T);
	T = NULL;
	if (output_failed) Return(GMT_RUNTIME_ERROR);

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
