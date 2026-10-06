/*--------------------------------------------------------------------
 *    The MB-system:	mbroutetime.c	5/4/2009
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
 * mbroutetime outputs a list of the times when a survey hit the waypoints
 * of a planned survey route. This (lon lat time_d) list can then be used by mbextractsegy
 * or mb7k2ss to extract subbottom (or sidescan) data into files corresponding
 * to the lines between waypoints. The input route files are in the MBgrdviz
 * route file format. The times are in decimal epoch seconds (seconds since 1/1/1970).
 *
 * Author:	D. W. Caress
 * Date:	May 5, 2009
 * Location:	R/V Thompson, at the dock in Apia, Samoa
 */
/*
 * GMT-module port of src/utilities/mbroutetime.cc: options from GMT's option list (long options
 * through module_kw, lower-case aliases kept), main() becomes GMT_mbroutetime() and every exit() a
 * Return() with a GMT error code. The waypoint time list is the program's own output file.
 */

#define THIS_MODULE_NAME "mbroutetime"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "List the times when a survey reached the waypoints of a planned route"
/* Primary input is the swath file or datalist given with -I; the waypoint times are written to the -O file. */
#define THIS_MODULE_KEYS "ID{"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include "unistd_w.h"
#else
#include <unistd.h>
#endif
#include "mb_define.h"
#include "mb_format.h"
#include "mb_status.h"

enum { MBES_ALLOC_NUM = 128 };
/* constexpr int MBES_ROUTE_WAYPOINT_NONE = 0; */
/* constexpr int MBES_ROUTE_WAYPOINT_SIMPLE = 1; */
enum { MBES_ROUTE_WAYPOINT_TRANSIT = 2 };
/* constexpr int MBES_ROUTE_WAYPOINT_STARTLINE 3; */
enum { MBES_ROUTE_WAYPOINT_ENDLINE = 4 };
/* constexpr double MBES_ONLINE_THRESHOLD = 15.0; */
/* constexpr int MBES_ONLINE_COUNT = 30; */

static const char program_name[] = "MBroutetime";
static const char help_message[] =
    "MBroutetime outputs a list of the times when a survey hit the waypoints\n"
    "of a planned survey route. This (lon lat time_d) list can then be used by\n"
    "mbextractsegy or mb7k2ss to extract subbottom (or sidescan) data into files\n"
    "corresponding to the lines between waypoints.";
static const char usage_message[] =
    "mbroutetime  -Rroutefile [-Fformat -Ifile -Owaypointtimefile -Urangethreshold -H -V]\n\n"
    "\t--format=format_id {-Fformat_id}\n"
    "\t--help {-H}\n"
    "\t--input=file {-Ifile}\n"
    "\t--output=waypointtimefile {-Owaypointtimefile}\n"
    "\t--range-threshold=rangethreshold {-Urangethreshold}\n"
    "\t--route-file=routefile {-Rroutefile}\n"
    "\t--verbose {-V}\n";

/*--------------------------------------------------------------------*/


/* --- GMT front end ---------------------------------------------------- */

/* Translation table from the program's long options to its short ones (each one has a short twin) */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'v', "verbose",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",            "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'F', "format",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",           "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "output",          "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'U', "range-threshold", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'R', "route-file",      "", "", "", "", GMT_TP_STANDARD },
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
EXTERN_MSC int GMT_mbroutetime(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbroutetime(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	int gmt_error;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program, which reports the missing route file itself */
	if ((gmt_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(gmt_error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	int verbose = 0;
	int format;
	int pings;
	int lonflip;
	double bounds[4];
	int btime_i[7];
	int etime_i[7];
	double speedmin;
	double timegap;
	int status = mb_defaults(verbose, &format, &pings, &lonflip, bounds, btime_i, etime_i, &speedmin, &timegap);

	char output_file[MB_PATH_MAXLINE + 14] = "";  // Needs extra padding for "_wpttime_d.txt"
	bool output_file_set = false;
	char route_file[MB_PATH_MAXLINE] = "";
	double rangethreshold = 25.0;

	char read_file[MB_PATH_MAXLINE] = "";
	strcpy(read_file, "datalist.mb-1");

	{
		bool errflg = false;
		bool help = false;
		/* process argument list: the program's options from GMT's option list (long options come
		   in as their short twins through module_kw; lower-case aliases kept) */
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
			case 'F':
			case 'f':
				sscanf(opt->arg, "%d", &format);
				break;
			case 'I':
			case 'i':
				sscanf(opt->arg, "%1023s", read_file);
				break;
			case 'O':
			case 'o':
				sscanf(opt->arg, "%1023s", output_file);
				output_file_set = true;
				break;
			case 'R':
			case 'r':
				sscanf(opt->arg, "%1023s", route_file);
				break;
			case 'U':
			case 'u':
				sscanf(opt->arg, "%lf", &rangethreshold);
				break;
			default:
				errflg |= (gmt_default_option_error(GMT, opt) != 0);
				break;
			}

		if (errflg)
			Return(GMT_PARSE_ERROR);

		if (verbose == 1 || help) {
			fprintf(stderr, "\nProgram %s\n", program_name);
			fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
		}

		if (verbose >= 2) {
			fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
			fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(stderr, "dbg2  Control Parameters:\n");
			fprintf(stderr, "dbg2       verbose:           %d\n", verbose);
			fprintf(stderr, "dbg2       help:              %d\n", help);
			fprintf(stderr, "dbg2       format:            %d\n", format);
			fprintf(stderr, "dbg2       pings:             %d\n", pings);
			fprintf(stderr, "dbg2       lonflip:           %d\n", lonflip);
			fprintf(stderr, "dbg2       bounds[0]:         %f\n", bounds[0]);
			fprintf(stderr, "dbg2       bounds[1]:         %f\n", bounds[1]);
			fprintf(stderr, "dbg2       bounds[2]:         %f\n", bounds[2]);
			fprintf(stderr, "dbg2       bounds[3]:         %f\n", bounds[3]);
			fprintf(stderr, "dbg2       btime_i[0]:        %d\n", btime_i[0]);
			fprintf(stderr, "dbg2       btime_i[1]:        %d\n", btime_i[1]);
			fprintf(stderr, "dbg2       btime_i[2]:        %d\n", btime_i[2]);
			fprintf(stderr, "dbg2       btime_i[3]:        %d\n", btime_i[3]);
			fprintf(stderr, "dbg2       btime_i[4]:        %d\n", btime_i[4]);
			fprintf(stderr, "dbg2       btime_i[5]:        %d\n", btime_i[5]);
			fprintf(stderr, "dbg2       btime_i[6]:        %d\n", btime_i[6]);
			fprintf(stderr, "dbg2       etime_i[0]:        %d\n", etime_i[0]);
			fprintf(stderr, "dbg2       etime_i[1]:        %d\n", etime_i[1]);
			fprintf(stderr, "dbg2       etime_i[2]:        %d\n", etime_i[2]);
			fprintf(stderr, "dbg2       etime_i[3]:        %d\n", etime_i[3]);
			fprintf(stderr, "dbg2       etime_i[4]:        %d\n", etime_i[4]);
			fprintf(stderr, "dbg2       etime_i[5]:        %d\n", etime_i[5]);
			fprintf(stderr, "dbg2       etime_i[6]:        %d\n", etime_i[6]);
			fprintf(stderr, "dbg2       speedmin:          %f\n", speedmin);
			fprintf(stderr, "dbg2       timegap:           %f\n", timegap);
			fprintf(stderr, "dbg2       read_file:         %s\n", read_file);
			fprintf(stderr, "dbg2       route_file:        %s\n", route_file);
			fprintf(stderr, "dbg2       output_file_set:   %d\n", output_file_set);
			fprintf(stderr, "dbg2       output_file:       %s\n", output_file);
			fprintf(stderr, "dbg2       rangethreshold:    %f\n", rangethreshold);
		}

		if (help) {
			Return(usage(API, GMT_USAGE));
		}
	}

	/* read route file */
	FILE *fp = fopen(route_file, "r");
	if (fp == NULL) {
		/* the program exits with MB_FAILURE here, which is 0: as a module it is the error it is */
		GMT_Report(API, GMT_MSG_ERROR, "Unable to open route file <%s> for reading\n", route_file);
		Return(GMT_ERROR_ON_FOPEN);
	}

	int route_file_version_major = 0;
	int route_file_version_minor = 0;
	char comment[MB_COMMENT_MAXLINE] = "";
	double heading;

	int nroutepoint = 0;
	int nroutepointalloc = 0;
	double *routelon = NULL;
	double *routelat = NULL;
	double *routeheading = NULL;
	int *routewaypoint = NULL;
	double *routetime_d = NULL;

	int error = MB_ERROR_NO_ERROR;

	char *result;
	while ((result = fgets(comment, MB_PATH_MAXLINE, fp)) == comment) {
		if (comment[0] == '#') {
			if (strncmp(comment, "## Route File Version", 21) == 0) {
				int n = sscanf(comment, "## Route File Version %d.%d", &route_file_version_major, &route_file_version_minor);
				if (n <= 0) {
					route_file_version_major = 0;
					route_file_version_minor = 0;
				}
			}
		}
		else {
			double lon;
			double lat;
			double topo;
			int waypoint;
			bool point_ok = false;
			if (route_file_version_major <= 0) {
				const int nget = sscanf(comment, "%lf %lf %lf %d %lf", &lon, &lat, &topo, &waypoint, &heading);
				point_ok = (nget >= 2);
			}
			else if (route_file_version_major == 1) {
				const int nget = sscanf(comment, "%lf %lf %lf %d %lf", &lon, &lat, &topo, &waypoint, &heading);
				point_ok = (nget >= 3 && waypoint > MBES_ROUTE_WAYPOINT_TRANSIT);
			}
			else {
				const int nget = sscanf(comment, "%lf,%lf,%lf,%d,%lf", &lon, &lat, &topo, &waypoint, &heading);
				point_ok = (nget >= 3 && waypoint > MBES_ROUTE_WAYPOINT_TRANSIT);
			}

			/* if good data check for need to allocate more space */
			if (point_ok && nroutepoint + 2 > nroutepointalloc) {
				nroutepointalloc += MBES_ALLOC_NUM;
				status = mb_reallocd(verbose, __FILE__, __LINE__, nroutepointalloc * sizeof(double), (void **)&routelon, &error);
				status &= mb_reallocd(verbose, __FILE__, __LINE__, nroutepointalloc * sizeof(double), (void **)&routelat, &error);
				status &=
				    mb_reallocd(verbose, __FILE__, __LINE__, nroutepointalloc * sizeof(double), (void **)&routeheading, &error);
				status &=
				    mb_reallocd(verbose, __FILE__, __LINE__, nroutepointalloc * sizeof(int), (void **)&routewaypoint, &error);
				status &=
				    mb_reallocd(verbose, __FILE__, __LINE__, nroutepointalloc * sizeof(double), (void **)&routetime_d, &error);
				if (status != MB_SUCCESS) {
					char *message;
					mb_error(verbose, error, &message);
					GMT_Report(API, GMT_MSG_ERROR, "MBIO Error allocating data arrays: %s\n", message);
					Return(GMT_RUNTIME_ERROR);
				}
			}

			/* add good point to route */
			if (point_ok && nroutepointalloc > nroutepoint) {
				routelon[nroutepoint] = lon;
				routelat[nroutepoint] = lat;
				routeheading[nroutepoint] = heading;
				routewaypoint[nroutepoint] = waypoint;
				routetime_d[nroutepoint] = 0.0;
				nroutepoint++;
			}
		}
	}

	fclose(fp);
	fp = NULL;

	/* Check that there are valid waypoints in memory */
	if (nroutepoint < 1) {
		GMT_Report(API, GMT_MSG_ERROR, "No line start or line end waypoints read from route file: <%s>\n", route_file);
		Return(GMT_RUNTIME_ERROR);
	}
	else if (nroutepoint < 2) {
		GMT_Report(API, GMT_MSG_ERROR, "Only one line start or line end waypoint read from route file: <%s>\n", route_file);
		Return(GMT_RUNTIME_ERROR);
	}

	/* set starting values */
	int activewaypoint = 0;
	double mtodeglon;
	double mtodeglat;
	mb_coor_scale(verbose, routelat[activewaypoint], &mtodeglon, &mtodeglat);
	double rangelast = 1000 * rangethreshold;

	/* output status */
	if (verbose > 0) {
		/* output info on file output */
		fprintf(stderr, "Read %d waypoints from route file: %s\n", nroutepoint, route_file);
	}

	/* get format if required */
	if (format == 0)
		mb_get_format(verbose, read_file, NULL, &format, &error);

	/* determine whether to read one file or a list of files */
	const bool read_datalist = format < 0;
	bool read_data;
	void *datalist;
	char file[MB_PATH_MAXLINE] = "";
	char dfile[MB_PATH_MAXLINE] = "";

	/* open file list */
	if (read_datalist) {
		const int look_processed = MB_DATALIST_LOOK_UNSET;
		if (mb_datalist_open(verbose, &datalist, read_file, look_processed, &error) != MB_SUCCESS) {
			GMT_Report(API, GMT_MSG_ERROR, "Unable to open data list file: %s\n", read_file);
			Return(GMT_ERROR_ON_FOPEN);
		}
		double file_weight;
		read_data = mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight, &error) == MB_SUCCESS;
	} else {
		// else copy single filename to be read
		strcpy(file, read_file);
		read_data = true;
	}

	double btime_d;
	double etime_d;
	int beams_bath;
	int beams_amp;
	int pixels_ss;

	/* MBIO read values */
	void *mbio_ptr = NULL;
	void *store_ptr = NULL;
	int kind;
	int time_i[7];
	double time_d;
	double navlon;
	double navlat;
	double speed;
	double distance;
	double altitude;
	double sensordepth;
	char *beamflag = NULL;
	double *bath = NULL;
	double *bathacrosstrack = NULL;
	double *bathalongtrack = NULL;
	double *amp = NULL;
	double *ss = NULL;
	double *ssacrosstrack = NULL;
	double *ssalongtrack = NULL;

	int nroutepointfound = 0;

	double lasttime_d = 0.0;
	double lastheading = 0.0;
	double lastlon = 0.0;
	double lastlat = 0.0;
	double range = 0.0;

	/* loop over all files to be read */
	while (read_data) {
		/* read fnv file if possible */
		mb_get_fnv(verbose, file, &format, &error);

		/* initialize reading the swath file */
		if (mb_read_init(verbose, file, format, pings, lonflip, bounds, btime_i, etime_i, speedmin, timegap, &mbio_ptr,
		                           &btime_d, &etime_d, &beams_bath, &beams_amp, &pixels_ss, &error) != MB_SUCCESS) {
			char *message;
			mb_error(verbose, error, &message);
			GMT_Report(API, GMT_MSG_ERROR, "MBIO Error returned from function <mb_read_init>: %s\n", message);
			GMT_Report(API, GMT_MSG_ERROR, "Multibeam File <%s> not initialized for reading\n", file);
			Return(GMT_RUNTIME_ERROR);
		}

		/* allocate memory for data arrays */
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(char), (void **)&beamflag, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bath, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_AMPLITUDE, sizeof(double), (void **)&amp, &error);
		if (error == MB_ERROR_NO_ERROR)
			status =
			    mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathacrosstrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status =
			    mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathalongtrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ss, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssacrosstrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, mbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssalongtrack, &error);

		/* if error initializing memory then quit */
		if (error != MB_ERROR_NO_ERROR) {
			char *message;
			mb_error(verbose, error, &message);
			GMT_Report(API, GMT_MSG_ERROR, "MBIO Error allocating data arrays: %s\n", message);
			Return(GMT_RUNTIME_ERROR);
		}

		/* read and use data */
		int nread = 0;
		while (error <= MB_ERROR_NO_ERROR && activewaypoint < nroutepoint) {
			/* reset error */
			error = MB_ERROR_NO_ERROR;

			/* read next data record */
			status = mb_get_all(verbose, mbio_ptr, &store_ptr, &kind, time_i, &time_d, &navlon, &navlat, &speed, &heading,
			                    &distance, &altitude, &sensordepth, &beams_bath, &beams_amp, &pixels_ss, beamflag, bath, amp,
			                    bathacrosstrack, bathalongtrack, ss, ssacrosstrack, ssalongtrack, comment, &error);

			/* deal with nav and time from survey data only - not nav, sidescan, or subbottom */
			if (error <= MB_ERROR_NO_ERROR && kind == MB_DATA_DATA) {
				/* increment counter */
				nread++;

				/* save last nav and heading */
				if (navlon != 0.0)
					lastlon = navlon;
				if (navlat != 0.0)
					lastlat = navlat;
				if (heading != 0.0)
					lastheading = heading;
				if (time_d != 0.0)
					lasttime_d = time_d;

				/* check survey data position against waypoints */
				if (navlon != 0.0 && navlat != 0.0) {
					const double dx = (navlon - routelon[activewaypoint]) / mtodeglon;
					const double dy = (navlat - routelat[activewaypoint]) / mtodeglat;
					range = sqrt(dx * dx + dy * dy);
					if (verbose > 0)
						fprintf(stderr, "> activewaypoint:%d time_d:%f range:%f   lon: %f %f   lat: %f %f\n", activewaypoint,
						        time_d, range, navlon, routelon[activewaypoint], navlat, routelat[activewaypoint]);

					if (range < rangethreshold && (activewaypoint == 0 || range > rangelast) && activewaypoint < nroutepoint) {
						fprintf(stderr, "Waypoint %d of %d found with range %f m\n", activewaypoint, nroutepoint, range);
						routetime_d[activewaypoint] = time_d;
						activewaypoint++;
						nroutepointfound++;
						mb_coor_scale(verbose, routelat[activewaypoint], &mtodeglon, &mtodeglat);
						rangelast = 1000 * rangethreshold;
					}
					else
						rangelast = range;
				}
			}

			if (verbose >= 2) {
				fprintf(stderr, "\ndbg2  Ping read in program <%s>\n", program_name);
				fprintf(stderr, "dbg2       kind:           %d\n", kind);
				fprintf(stderr, "dbg2       error:          %d\n", error);
				fprintf(stderr, "dbg2       status:         %d\n", status);
			}
		}

		/* close the swath file */
		status &= mb_close(verbose, &mbio_ptr, &error);

		/* output read statistics */
		fprintf(stderr, "%d records read from %s\n", nread, file);

		/* figure out whether and what to read next */
		if (read_datalist) {
			double file_weight;
			read_data = mb_datalist_read(verbose, datalist, file, dfile, &format, &file_weight, &error) == MB_SUCCESS;
		} else {
			read_data = false;
		}

		/* end loop over files in list */
	}
	if (read_datalist)
		mb_datalist_close(verbose, &datalist, &error);

	/* if the last route point was not reached, add one last waypoint */
	if (nroutepointfound < nroutepoint) {
		fprintf(stderr, "Waypoint %d of %d set at end of data with range %f m to next specified waypoint\n", activewaypoint,
		        nroutepoint, range);
		routelon[nroutepointfound] = lastlon;
		routelat[nroutepointfound] = lastlat;
		routeheading[nroutepointfound] = lastheading;
		routetime_d[nroutepointfound] = lasttime_d;
		routewaypoint[nroutepointfound] = MBES_ROUTE_WAYPOINT_ENDLINE;
		nroutepointfound++;
	}

	/* output time list for the route */
	if (!output_file_set) {
		snprintf(output_file, sizeof(output_file), "%s_wpttime_d.txt", read_file);
	}
	fp = fopen(output_file, "w");
	if (fp == NULL) {
		GMT_Report(API, GMT_MSG_ERROR, "Unable to open output waypoint time list file <%s> for writing\n", output_file);
		Return(GMT_ERROR_ON_FOPEN);
	}
	for (int i = 0; i < nroutepointfound; i++) {
		fprintf(fp, "%3d %3d %11.6f %10.6f %10.6f %.6f\n", i, routewaypoint[i], routelon[i], routelat[i], routeheading[i],
		        routetime_d[i]);
		if (verbose > 0)
			fprintf(stderr, "%3d %3d %11.6f %10.6f %10.6f %.6f\n", i, routewaypoint[i], routelon[i], routelat[i], routeheading[i],
			        routetime_d[i]);
	}
	fclose(fp);

	/* deallocate route arrays */
	status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&routelon, &error);
	status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&routelat, &error);
	status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&routeheading, &error);
	status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&routewaypoint, &error);
	status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&routetime_d, &error);

	/* check memory */
	if (verbose >= 4)
		status &= mb_memory_list(verbose, &error);

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
		fprintf(stderr, "dbg2  Ending status:\n");
		fprintf(stderr, "dbg2       status:  %d\n", status);
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
