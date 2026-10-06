/*--------------------------------------------------------------------
 *    The MB-system:	mbsvpselect.c	03.03.2014
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
 * Mbsvpselect chooses and implements the best available sound speed model
 * for each swath file in a survey. The user provides a list of the
 * available sound speed models and specifies the criteria used for
 * model selection. The program uses mbset to turn on bathymetry
 * recalculation by raytracing through the sound speed model selected
 * for each swath file.
 *
 * Author:	Ammar Aljuhne (ammaraljuhne@gmail.com)
 * Co-author:   Christian do Santos Ferreira (cferreira@marum.de)
 *              MARUM, University of Bremen
 * Date:	3 March 2014
 *
 * Description:
 *
 * The tool aims to help users to automatically apply the sound velocity
 * correction to the survey files. since most surveys involve several SVPs,
 * the seletion of the appropriate SVP for each survey profile is still
 * missing in MB-System.
 *
 * After finding the appropriate svp for each profile based on the chosen
 * method, the results are copied to a txt file that shows each survey
 * profile with the corresponding SVP. the tool also calls mbset automatically
 * so no need to assign SVP to the data. it is done automatically.
 *
 * There are 5 methods for choosing the appropriate SVP for each survey
 * profile. These methods are:
 *
 * 1. Nearest SVP in position: the middle position of each survey profile
 *    is calculated and the geodesics (shortest distance on the ellipsoid)
 *    to all SVPs are calculated. and the SVP with the shortest distance is
 *    chosen. when the middle position of the survey profile is calculated
 *    there is an option to check for 0 lat 0 long wrong values. if it is
 *    found at the starting the geodesic will be calculated to the end of
 *    the profile.
 *
 * 2. Nearest in time: the time interveal between the starting time of
 *    the profile and the time of the SVP, and the SVP with  the shortest
 *    interval will be chosen.
 *
 * 3. Nearest in position within time: a default time radius from the
 *    profile is set as 10 hours, and within this period the nearest SVP
 *    in position is chosen. if none of the SVPs are within this period the
 *    nearest in position will be taken despit of the period threshold. The
 *    period threshold can be set by the user.
 *
 * 4. Nearest in time within range: similar to the previous option but
 *    this time a default range of 10000 meters is set and within this range
 *    the svp nearest in time is chosen. also this 10000 meter value could
 *    be set by the user.
 *
 * 5. Nearest in season within range: similar to the previous option the
 *    selected SVP could be chosen based on the month only not on the year.
 *    it means within the specified range the user could chose either the svp
 *    nearest in time or the svp nearest in month (this could be interpreted
 *    as the svp that falls in the same seasonal period despite of the year
 *    when it was taken).
 *
 * Mbsvpselect reads the .inf file of each swath file referenced in a recursive
 * datalist structure to determine the location and collection time of the
 * relevant data. The ancillary *.inf, *.fbt, and *.fnv files must be created
 * first. The water sound speed models (called SVPs by convention as an acronym
 * for Sound Velocity Profiles) to be used must include one of three supported
 * file headers specifying the time and location of the model.
 *
 * University of Bremen SVP headers:
 *   MB-SVP 2011/01/08 19:30:00 -52.965437  -36.986314
 *   (keyword yyyy/mm/dd hh:mm:ss latitude longitude)
 *
 * MB-System SVPs as now output by mbsvplist:
 *   ## MB-SVP 2011/01/08 19:30:00 -36.986314 -52.965437
 *   (keyword yyyy/mm/dd hh:mm:ss longitude latitude)
 *
 * CARIS sound velocity header format:
 *   Section 2013-150 23:22:18 -57:02:01 -26:02:18
 *   (keyword yyyy-yearDay  hh:mm:ss latitude (degree:min:sec) longitude (degree:min:sec))
 *
 * Mbsvpselect supports SVP files with single models or files with multiple models where
 * new headers occur between models.
 *
 * Instructions:
 *
 * 1) Set up a survey (or surveys) for MB-System processing in the usual way,
 *    including creating a datalist file referencing the swath data of interest
 *    and generating the ancillary *.inf, *.fbt, and *.fnv files for each of
 *    the swath files.
 * 2) Create an svplist file (analogous to a datalist, but referencing the
 *    relevant SVP files). Each SVP file is expected to be a text file with
 *    depth-sound speed pairs on each line (depth in meters, sound speed in
 *    meters/second) excepting for a header line at the start of each discrete
 *    model. Any of the header formats listed above will work.
 *    that refers to a local svp datalist. the local datalists includes
 * 3) In order to turn on bathymetry recalculation by raytracing through the
 *    most appropriate sound speed model for each swath file, call mbsvpselect:
 *
 *      mbsvpselect -N -V -Idatalist -Ssvplist [-P0, -P1, -P2/period, -P3/range, -P3/range/1]
 *
 *    -N is the option to check 0 latitude 0 longitude in the survey lines.
 *    -V verbosity.
 *    -I input datalist
 *    -S input svp datalist
 *    -P the method for choosing the svp where:
 *        -P or -P0                 is the nearest in position
 *        -P1                       is the nearest in time
 *        -P2                       is nearest in position within time (default time period is 10 hours)
 *        -P2/time                  is nearest in position within specified time period (in hours)
 *        -P3                       is nearest in time within range   (default range is 10000 meters)
 *        -P3/range or -P3/range/0  is nearest in time within specified range (in meters)
 *        -P3/range/1                     is nearest in month (seasonal) within specified range in meter.
 *
 * Example
 *
 * Suppose you are working in a directory called Survey_1 containing
 * swath files that need to have the bathymetry recalculated by
 * raytracing through water sound speed models. The local datalist
 * file might contain something like:
 *      13349457_3934_2845.mb88 88
 *      13645323_3433_5543.mb88 88
 *      46372536_6563_4637.mb88 88
 *      64362825_6344_2635.mb88 88
 *
 * or, if you use absolute passwords, something like:
 *
 *      /MyMac/User/Survey_1/13349457_3934_2845.mb88 88
 *      /MyMac/User/Survey_1/13645323_3433_5543.mb88 88
 *      /MyMac/User/Survey_1/46372536_6563_4637.mb88 88
 *      /MyMac/User/Survey_1/64362825_6344_2635.mb88 88
 *
 * By convention, this datalist will be named something
 * like datalist.mb-1, where the ".mb-1" suffix indicates to
 * MB-System programs that this is a datalist file. As
 * documented elsewhere, datalist files can contain entries
 * that reference datalists rather than single files; thus
 * datalists can be recursive.
 *
 * Suppose that the water properties were variable during this
 * survey, with the variability dominated by location.Further suppose
 * that there are three SVP files in a separate directory with
 * names such as svp1.svp, svp2.svp, and svp3.svp. Each of these files
 * contains a single model derived from CTD casts at a particular
 * place and time indicated in the single header line. In that directory
 * one can create an svplist file named SVP_list.mb-1 with contents:
 *      svp1.svp
 *      svp2.svp
 *      svp3.svp
 * Since mbsvpselect allows svplists to be recursive (like datalists),
 * one can create a second svplist named my_svplist.mb-1in the survey
 * processing directory that references the first with an entry like:
 *
 *      /MyMac/User/Survey_1/SVP_folder/SVP_list.mb-1 -1
 *
 * In order to turn on bathymetry recalculation for all of the
 * swath files referenced by datalist.mb-1 using the most appropriate
 * of the available sound speed models, run mbsvpselect with arguments
 * like:
 *
 *      mbsvpselect -N -V -I datalist.mb-1 -S my_svplist.mb-1 -P2/50
 *
 * Here the -P2/50 option specifies that the sound speed model to be
 * used for each file will be the closest one collected within 50 hours
 * of the swath data. The bathymetry recalculation will be turned on
 * using an mbset call of the form:
 *
 *      mbset -Idatalist.mb-1 -PSVPFILE:/MyMac/User/Survey_1/SVP_folder/svp1.svp
 *
 * Following the mbsvpselect usage, mbprocess must be run to actually
 * reprocess the swath data, including bathymetry recalculation by
 * raytracing.
 *
 */
/*
 * GMT-module port of src/utilities/mbsvpselect.cc: the getopt_long loop
 * is replaced by the GMT option parser (long options through module_kw,
 * lower-case aliases kept) and main() becomes
 * GMT_mbsvpselect(), every exit() a Return() with a GMT error code. Helpers
 * that exited now return a status up to GMT_mbsvpselect(). The file-scope
 * state is static and is reset on every call, because a GMT session can
 * run the module more than once. The atexit()/pause_screen() "press ENTER"
 * pause is not armed inside a GMT session: it would stall the host.
 */

#define THIS_MODULE_NAME "mbsvpselect"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Choose the best sound speed model for each swath file and set it with mbset"
/* Inputs are the swath datalist (-I) and the SVP list (-S). The module
 * writes result.txt and runs mbset itself, so there is no output key. */
#define THIS_MODULE_KEYS "ID{,SD{"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _MSC_VER
#include "unistd_w.h"
#else
#include <unistd.h>
#endif

#include <geodesic.h>

#include "mb_define.h"
#include "mb_status.h"

/* A GMT session switches the C runtime's default file mode to binary on
 * Windows. The parsers below compare whole lines ending in a newline and
 * trim only that newline, so CRLF text files must be opened in text mode. */
#ifdef _WIN32
#define MBSVPSELECT_READ "rt"
#define MBSVPSELECT_WRITE "w+t"
#else
#define MBSVPSELECT_READ "r"
#define MBSVPSELECT_WRITE "w+"
#endif


/* struct info_holder (shortly inf) hold the information from auxiliary files .inf
 * that are created from mbdatalist command */

struct info_holder {
	int flag;  // TODO(schwehr): Make this an enum
	char *file_name;
	long double s_lat;
	long double s_lon;
	double e_sec;
	long double e_lat;
	long double e_lon;
	long double ave_lat;
	long double ave_lon;
	struct tm s_datum_time;
	struct tm e_datum_time;
	struct tm ave_datum_time;
	time_t s_Time;
	time_t e_time;
};
typedef struct info_holder inf;

/* struct svp_holder (shortly svp) hold the information from auxiliary files .inf
 * that are created from mbdatalist command */
struct svp_holder {
	char *file_name;
	long double s_lat;
	long double s_lon;
	struct tm svp_datum_time;
	time_t svp_Time;
};
typedef struct svp_holder svp;


/* global variables */
// int counter_i_i2 = 0;
static int p_flag = 0;  // TODO(schwehr): Make this an enum
static int p_3_time = 10;
static int p_4_range = 10000;
static int p_4_flage = 0;
static int zero_test = 0;
static int size_2 = 0;
static int n_p2 = 0;
static char dHolder[1000][1000];
static char sdHolder[1000][1000];
static char svps[1000][1000];
static char swaths[1000][1000];
static int verbose = 0;
static char holder[1000][1000];   /* copy the buffer into holder array for indexing */
static char holder_2[1000][1000]; /* copy the buffer into holder array for indexing */
static char holder_3[1000][1000];
static char holder_4[1000][1000];
static char dBuffer[BUFSIZ];
static char sdBuffer[BUFSIZ];
static char buffer[BUFSIZ]; /* String to hold the file name */
static char buffer_2[BUFSIZ];
static int svp_total = 0;
static int surveyLines_total = 0;

static const char program_name[] = "mbsvpselect";
static const char help_message[] =
    "Program mbsvpselect chooses and implements the best available sound speed\n"
    "model for each swath file in a survey. The user provides a list of the\n"
    "available sound speed models and specifies the criteria used for\n"
    "model selection. The program uses mbset to turn on bathymetry\n"
    "recalculation by raytracing through the sound speed model selected\n"
    "for each swath file.";
static const char usage_message_old[] =
    "mbsvpselect -H -N -Idatalist -Ssvplist "
    "[-P0, -P1, -P2/period, -P3/range, -P3/range/1]  -V";
static const char usage_message[] =
    "mbsvpselect\n"
    "\t--check-zero-position {-N}\n"
    "\t--help {-H}\n"
    "\t--input=datalist {-Idatalist}\n"
    "\t--method=mode[/period_or_range[/seasonal]] {-Pmode[/period_or_range[/seasonal]]}\n"
    "\t--svplist=svplist {-Ssvplist}\n"
    "\t--verbose {-V}\n";

/* ---------------------------------------------------------------- */
/* Is leap old */
static bool Is_Leap(int year) {
	if (year % 400 == 0)
		return false;
	else if (year % 100 == 0)
		return true;
	else if (year % 4 == 0)
		return false;
	return true;
}
/* ---------------------------------------------------------------- */
/*
 * calc_ave_dateTime function fill the struct with average date and time. it takes the starting date
 *of the file and the end date of the file and then calculate the diffeence in  seconds
 * then it transform the sec into time format and add it to the starting date
 *
 void calc_ave_dateTime(inf *inf_hold)
 {
 //time_t *temp1 = &inf_hold->s_Time;
 //time_t *temp2 = &inf_hold->e_time;
 double s = difftime(inf_hold->s_Time, inf_hold->e_time);
 if(s>86400.0)
 {
 fprintf(stderr, "\n\n\n\n%f\n\n\n", s);
 }
 else
 {
 fprintf(stderr, "\n\n\n\n%f\n\n\n", s);
 }
 }*/
/* ---------------------------------------------------------------- */
static void JulianToGregorian(int year, int yearDay, int *year_tm, int *month, int *wDay) {
	*year_tm = year - 1900;
	if (!Is_Leap(year)) {
		if (yearDay > 335) {
			*month = 11;
			*wDay = yearDay - 335;
		}
		if ((yearDay > 305) && (yearDay <= 335)) {
			*month = 10;
			*wDay = yearDay - 305;
		}
		if ((yearDay > 274) && (yearDay <= 305)) {
			*month = 9;
			*wDay = yearDay - 274;
		}
		if ((yearDay > 244) && (yearDay <= 274)) {
			*month = 8;
			*wDay = yearDay - 244;
		}
		if ((yearDay > 213) && (yearDay <= 244)) {
			*month = 7;
			*wDay = yearDay - 213;
		}
		if ((yearDay > 182) && (yearDay <= 213)) {
			*month = 6;
			*wDay = yearDay - 182;
		}
		if ((yearDay > 152) && (yearDay <= 182)) {
			*month = 5;
			*wDay = yearDay - 152;
		}
		if ((yearDay > 121) && (yearDay <= 152)) {
			*month = 4;
			*wDay = yearDay - 121;
		}
		if ((yearDay > 91) && (yearDay <= 121)) {
			*month = 3;
			*wDay = yearDay - 91;
		}
		if ((yearDay > 60) && (yearDay <= 91)) {
			*month = 2;
			*wDay = yearDay - 60;
		}
		if ((yearDay > 31) && (yearDay <= 60)) {
			*month = 1;
			*wDay = yearDay - 274;
		}
		if (yearDay < 31) {
			*month = 0;
			*wDay = yearDay;
		}
	}
	else {
		if (yearDay > 334) {
			*month = 11;
			*wDay = yearDay - 334;
		}
		if ((yearDay > 304) && (yearDay <= 334)) {
			*month = 10;
			*wDay = yearDay - 304;
		}
		if ((yearDay > 273) && (yearDay <= 304)) {
			*month = 9;
			*wDay = yearDay - 273;
		}
		if ((yearDay > 243) && (yearDay <= 273)) {
			*month = 8;
			*wDay = yearDay - 243;
		}
		if ((yearDay > 212) && (yearDay <= 243)) {
			*month = 7;
			*wDay = yearDay - 212;
		}
		if ((yearDay > 181) && (yearDay <= 212)) {
			*month = 6;
			*wDay = yearDay - 181;
		}
		if ((yearDay > 151) && (yearDay <= 181)) {
			*month = 5;
			*wDay = yearDay - 151;
		}
		if ((yearDay > 120) && (yearDay <= 151)) {
			*month = 4;
			*wDay = yearDay - 120;
		}
		if ((yearDay > 90) && (yearDay <= 120)) {
			*month = 3;
			*wDay = yearDay - 90;
		}
		if ((yearDay > 59) && (yearDay <= 90)) {
			*month = 2;
			*wDay = yearDay - 59;
		}
		if ((yearDay > 31) && (yearDay <= 59)) {
			*month = 1;
			*wDay = yearDay - 31;
		}
		if (yearDay < 31) {
			*month = 0;
			*wDay = yearDay;
		}
	}
} /* JulianToGregorian */
/* ---------------------------------------------------------------- */
static void GregorianToJulian(int year, int month, int day, int *yearDay) {
	if (Is_Leap(year))
		switch (month) {
		case 0:
			*yearDay = day;
			break;
		case 1:
			*yearDay = day + 31;
			break;
		case 2:
			*yearDay = day + 59;
			break;
		case 3:
			*yearDay = day + 90;
			break;
		case 4:
			*yearDay = day + 120;
			break;
		case 5:
			*yearDay = day + 151;
			break;
		case 6:
			*yearDay = day + 181;
			break;
		case 7:
			*yearDay = day + 212;
			break;
		case 8:
			*yearDay = day + 243;
			break;
		case 9:
			*yearDay = day + 273;
			break;
		case 10:
			*yearDay = day + 304;
			break;
		case 11:
			*yearDay = day + 334;
			break;

		default:
			break;
		} /* switch */
	else
		switch (month) {
		case 0:
			*yearDay = day;
			break;
		case 1:
			*yearDay = day + 31;
			break;
		case 2:
			*yearDay = day + 60;
			break;
		case 3:
			*yearDay = day + 91;
			break;
		case 4:
			*yearDay = day + 121;
			break;
		case 5:
			*yearDay = day + 152;
			break;
		case 6:
			*yearDay = day + 182;
			break;
		case 7:
			*yearDay = day + 213;
			break;
		case 8:
			*yearDay = day + 244;
			break;
		case 9:
			*yearDay = day + 274;
			break;
		case 10:
			*yearDay = day + 305;
			break;
		case 11:
			*yearDay = day + 335;
			break;

		default:
			break;
		} /* switch */
} /* GregorianToJulian */
/* --------------------------------------------------------------- */
/* calculate the average position of two points */
/* http://www.movable-type.co.uk/scripts/latlong.html */
static void mid_point(long double lat1, long double lon1, long double lat2, long double lon2, long double *lat3, long double *lon3) {
	const double dLon = DTR * ((lon2) - (lon1));
	const double lat1_rad = DTR * ((lat1));
	const double lat2_rad = DTR * ((lat2));
	const double lon1_rad = DTR * ((lon1));
	const double bx = cos(lat2_rad) * cos(dLon);
	const double by = cos(lat2_rad) * sin(dLon);
	*(lat3) = atan2(sin(lat1_rad) + sin(lat2_rad), sqrt((((cos(lat1_rad)) + bx) * ((cos(lat1_rad)) + bx)) + (by * by))) * RTD;
	*(lon3) = (lon1_rad + atan2(by, (cos(lat1_rad) + bx))) * RTD;
}
/* ---------------------------------------------------------------- */
/*this function fills the inf struct with the appropriate values
 * it takes a pointer to the inf_struct and the file_name (file_name.inf) to read information from
 */
static int fill_struct_inf(inf *inf_hold, char *holder) {
	inf_hold->flag = 0;
	inf_hold->file_name = holder;

	/* reading relative inf file */
	FILE *fileName = fopen(inf_hold->file_name, MBSVPSELECT_READ);
	if (fileName == NULL) {
		fprintf(stderr, "%s could not be opened Please check the datalist files\n", inf_hold->file_name);
		return MB_FAILURE;
	}

	/* reaching start of data key word */
	while ((fgets(buffer, sizeof buffer, fileName)) != NULL)
		if (strcmp(buffer, "Start of Data:\n") == 0)
			break;

	/* parsing date and time*/
	fgets(buffer, sizeof buffer, fileName);

	int mon;
	int year;
	double s_sec;
	sscanf(buffer, "%*s %d %d %d %i:%i:%lf %*s %*s", &mon, &inf_hold->s_datum_time.tm_mday, &year,
	       &inf_hold->s_datum_time.tm_hour, &inf_hold->s_datum_time.tm_min, &s_sec);
	inf_hold->s_datum_time.tm_mon = mon - 1;
	inf_hold->s_datum_time.tm_year = year - 1900;
	s_sec = floor(s_sec);
	inf_hold->s_datum_time.tm_sec = (int)s_sec;
	GregorianToJulian(inf_hold->s_datum_time.tm_year, inf_hold->s_datum_time.tm_mon, inf_hold->s_datum_time.tm_mday,
	                  &inf_hold->s_datum_time.tm_yday);

	struct tm *ptr = &inf_hold->s_datum_time;
	inf_hold->s_Time = mktime(ptr);

	/* Lon and Lat processing*/

	fgets(buffer, sizeof buffer, fileName);

	sscanf(buffer, "%*s %Lf %*s %Lf %*s %*f %*s", &inf_hold->s_lon, &inf_hold->s_lat);

	/*parsing end of data information*/

	while ((fgets(buffer, sizeof buffer, fileName)) != NULL)
		if (strcmp(buffer, "End of Data:\n") == 0)
			break;

	fgets(buffer, sizeof buffer, fileName);

	sscanf(buffer, "%*s %d %d %d %i:%i:%lf %*s %*s", &mon, &inf_hold->e_datum_time.tm_mday, &year,
	       &inf_hold->e_datum_time.tm_hour, &inf_hold->e_datum_time.tm_min, &s_sec);

	inf_hold->e_datum_time.tm_mon = mon - 1;
	inf_hold->e_datum_time.tm_year = year - 1900;
	s_sec = floor(s_sec);
	inf_hold->e_datum_time.tm_sec = (int)s_sec;

	struct tm *ptr2 = &inf_hold->e_datum_time;
	inf_hold->e_time = mktime(ptr2);

	/* calc_ave_dateTime(inf_hold); */

	/* Lon and Lat processing */
	fgets(buffer, sizeof buffer, fileName);

	sscanf(buffer, "%*s %Lf %*s %Lf %*s %*f %*s", &inf_hold->e_lon, &inf_hold->e_lat);
	if (zero_test > 0) {
		if ((inf_hold->s_lat == 0.0) && (inf_hold->s_lon == 0.0)) {
			// TODO(schwehr): Was there supposed to be a _lat?
			if (inf_hold->e_lon == 0.0 /* && inf_hold->e_lon == 0.0 */ )
				inf_hold->flag = 3;
			else
				inf_hold->flag = 1;
		}
		else {
			// TODO(schwehr): Was there supposed to be a _lat?
			if (inf_hold->e_lon == 0.0 /* && (inf_hold->e_lon == 0.0 */ )
				inf_hold->flag = 2;
			else
				inf_hold->flag = 0;
		}
	}

	mid_point(inf_hold->s_lat, inf_hold->s_lon, inf_hold->e_lat, inf_hold->e_lon, &inf_hold->ave_lat, &inf_hold->ave_lon);

	fclose(fileName);
	return MB_SUCCESS;
} /* fill_struct_inf */
/* ---------------------------------------------------------------- */
/*
 * convert_decimal function
 * convert lat or lon from deg(int):min(int):sec(int) to decimal format
 */
static double convert_decimal(int deg, int min, int sec) {
	if (deg >= 0)
		return (double)deg + (((double)min) / 60) + (((double)sec) / 3600);

	return -(fabs((double)deg) + (((double)min) / 60) + (((double)sec) / 3600));
}
/* ------------------------------------------------------------------- */
/* SVP tool is able to read only two SVP formats
 *
 *
 */

/* ------------------------------------------------------------------- */
static int fill_struct_svp(svp *svp_hold, char *holder) {
	svp_hold->file_name = holder;

	/* reading relative svp file */
	FILE *fileName = fopen(svp_hold->file_name, MBSVPSELECT_READ);
	if (fileName == NULL) {
		fprintf(stderr, "%s could not be opend\n", svp_hold->file_name);
		return MB_FAILURE;
	}

	int yearDay;
	int month;
	int year;
	double seconds;
	int s_lat_min = 0;
	int s_lat_deg = 0;
	int s_lat_sec = 0;
	int s_lon_min = 0;
	int s_lon_deg = 0;
	int s_lon_sec = 0;
	char caris_str[] = "Section";
	char mb1_str[] = "## MB-SVP";
	char mb2_str[] = "# MB-SVP";
	char *ptr_caris = NULL;
	char *ptr_mb1 = NULL;
	char *ptr_mb2 = NULL;

	/* reaching start of data */
	while ((fgets(buffer, sizeof buffer, fileName)) != NULL) {
		ptr_caris = strstr(buffer, caris_str);
		ptr_mb1 = strstr(buffer, mb1_str);
		ptr_mb2 = strstr(buffer, mb2_str);
		if (ptr_caris != NULL) {
			fprintf(stderr, "\n%s\n", buffer);
			sscanf(buffer, "%*s %d-%d  %i:%i:%i %d:%d:%d %d:%d:%d", &year, &yearDay, &svp_hold->svp_datum_time.tm_hour,
			       &svp_hold->svp_datum_time.tm_min, &svp_hold->svp_datum_time.tm_sec, &s_lat_deg, &s_lat_min, &s_lat_sec,
			       &s_lon_deg, &s_lon_min, &s_lon_sec);
			svp_hold->svp_datum_time.tm_year = year - 1900;
			svp_hold->svp_datum_time.tm_yday = yearDay;
			JulianToGregorian(year, yearDay, &svp_hold->svp_datum_time.tm_year, &svp_hold->svp_datum_time.tm_mon,
			                  &svp_hold->svp_datum_time.tm_mday);
			/* Julian to Gregorian date */
			/* svp_hold->svp_datum_time.tm_yday = yearDay - 1; */
			/* svp_hold->svp_datum_time.tm_year = year-1900; */

			struct tm *ptr1 = &svp_hold->svp_datum_time;
			svp_hold->svp_Time = mktime(ptr1);

			/* latitude to decimal */
			svp_hold->s_lat = convert_decimal(s_lat_deg, s_lat_min, s_lat_sec);
			/* longitude to decimal */
			svp_hold->s_lon = convert_decimal(s_lon_deg, s_lon_min, s_lon_sec);
			break;
		}
		else if (ptr_mb1 != NULL) {
			fprintf(stderr, "\n%s\n", buffer);
			sscanf(buffer, "## MB-SVP %d/%d/%d %d:%d:%lf %Lf %Lf", &year, &month, &svp_hold->svp_datum_time.tm_mday,
			       &svp_hold->svp_datum_time.tm_hour, &svp_hold->svp_datum_time.tm_min, &seconds, &svp_hold->s_lon,
			       &svp_hold->s_lat);

			svp_hold->svp_datum_time.tm_mon = month - 1;
			svp_hold->svp_datum_time.tm_year = year - 1900;
			svp_hold->svp_datum_time.tm_sec = seconds;
			GregorianToJulian(svp_hold->svp_datum_time.tm_year, svp_hold->svp_datum_time.tm_mon, svp_hold->svp_datum_time.tm_mday,
			                  &svp_hold->svp_datum_time.tm_yday);
			struct tm *ptr = &svp_hold->svp_datum_time;
			svp_hold->svp_Time = mktime(ptr);

			break;
		}
		else if (ptr_mb2 != NULL) {
			fprintf(stderr, "\n%s\n", buffer);
			sscanf(buffer, "%*s %*s %d/%d/%d %d:%d:%d %Lf %Lf", &year, &month, &svp_hold->svp_datum_time.tm_mday,
			       &svp_hold->svp_datum_time.tm_hour, &svp_hold->svp_datum_time.tm_min, &svp_hold->svp_datum_time.tm_sec,
			       &svp_hold->s_lon, &svp_hold->s_lat);

			svp_hold->svp_datum_time.tm_mon = month - 1;
			svp_hold->svp_datum_time.tm_year = year - 1900;
			GregorianToJulian(svp_hold->svp_datum_time.tm_year, svp_hold->svp_datum_time.tm_mon, svp_hold->svp_datum_time.tm_mday,
			                  &svp_hold->svp_datum_time.tm_yday);
			struct tm *ptr = &svp_hold->svp_datum_time;
			svp_hold->svp_Time = mktime(ptr);

			break;
		}
	}

	fclose(fileName);
	return MB_SUCCESS;
} /* fill_struct_svp */
/* --------------------------------------------------------------- */
/*copy string and handle possible buffer overlap*/
static char *my_strcpy(char *a, char *b) {
	if (a == NULL || b == NULL) {
		return NULL;
	}

	memmove(a, b, strlen(b) + 1);
	return a;
}
/* ---------------------------------------------------------------- */
/*
 *  Function trim_newline
 *	Delete the '\n' char from string
 */
static void trim_newline(char string[]) {
	if (string[strlen(string) - 1] == '\n')
		string[strlen(string) - 1] = '\0';
}
/*---------------------------------------------------------------------*/
static int read_recursive2(char *fname) {
	char original[1024] = {""};
	my_strcpy(original, fname);
	const char *result = fname;
	int counter = 0;
	trim_newline(fname);
	strcat(fname, ".inf");
	FILE *dataFile = fopen(fname, MBSVPSELECT_READ);
	if (dataFile != NULL) {
		if (surveyLines_total >= 1000) {
			fprintf(stderr, "\nToo many survey files referenced (max 1000) - ignoring: %s\n", fname);
			fclose(dataFile);
			return counter;
		}
		my_strcpy(holder[surveyLines_total], fname);
		counter += 1;
		surveyLines_total += 1;
		fclose(dataFile);
		return counter;
	}

	const char *ret = strchr(result, ' ');
	if (ret == NULL) {
		char file2[1024] = {""};
		my_strcpy(file2, original);
		trim_newline(file2);
		FILE *dataFile2 = fopen(file2, MBSVPSELECT_READ);
		if (dataFile2 == NULL) {
			fprintf(stderr, "Could not open the file %s", file2);
			return counter;
		}
		while ((fgets(dBuffer, sizeof dBuffer, dataFile2)) != NULL) {
			//char strHolder[strlen(original)];		INVALID JL
			char strHolder[1024];
			my_strcpy(strHolder, original);
			while (strHolder[strlen(strHolder) - 1] != '/')
				strHolder[strlen(strHolder) - 1] = 0;
			strcat(strHolder, dBuffer);
			int tmp = read_recursive2(strHolder);
			if (tmp == 0) {
				tmp = read_recursive2(dBuffer);
			}
			// counter +=tmp;
		}
		fclose(dataFile2);
	}
	else {
		char strHolder[1024];
		my_strcpy(strHolder, original);
		while (strHolder[strlen(strHolder) - 1] != ' ')
			strHolder[strlen(strHolder) - 1] = 0;
		strHolder[strlen(strHolder) - 1] = 0;
		const int tmp2 = read_recursive2(strHolder);
		counter += tmp2;
	}

	return counter;
}
/*---------------------------------------------------------------------*/
static int read_recursive(char *fileName) {
	trim_newline(fileName);
	FILE *dataFile = fopen(fileName, MBSVPSELECT_READ);
	if (dataFile == NULL) {
		fprintf(stderr, "Could not open the file %s", fileName);
		return 0;
	}

	char str[BUFSIZ];
	fgets(str, sizeof str, dataFile);
	trim_newline(dBuffer);
	// initialize the end condition for the svps
	const char caris_str[] = "Section";
	const char *ptr_caris = strstr(str, caris_str);
	const char mb1_str[] = "## MB-SVP";
	const char *ptr_mb1 = strstr(str, mb1_str);
	const char mb2_str[] = "MB-SVP";
	const char *ptr_mb2 = strstr(str, mb2_str);

	// initialize the end condition for the swaths

	int counter = 0;
	if ((ptr_caris != NULL) || (ptr_mb1 != NULL) || (ptr_mb2 != NULL)) {
		if (svp_total >= 1000) {
			fprintf(stderr, "\nToo many SVP files referenced (max 1000) - ignoring: %s\n", fileName);
		} else {
			my_strcpy(svps[svp_total], fileName);
			counter += 1;
		}
	} else {
		read_recursive(str);
	}
	fclose(dataFile);

	return counter;
}
/* ---------------------------------------------------------------- */
/* print the inf information on the screen */
static void print_inf(inf *cd) {
	struct tm *temp = &cd->s_datum_time;
	fprintf(stderr, "%s\n", "==================================================");
	fprintf(stderr, "file_name: %s\n", cd->file_name);
	fprintf(stderr, "%s\n", "starting Date and time");
	fprintf(stderr, "\n%s\n", asctime(temp));

	temp = &cd->e_datum_time;
	fprintf(stderr, "%s\n", "ending Date and time");
	fprintf(stderr, "\n%s\n", asctime(temp));
	fprintf(stderr, "%s\n", "Start position");
	fprintf(stderr, "lat: %Lf\t", cd->s_lat);
	fprintf(stderr, "lon: %Lf\n", cd->s_lon);
	fprintf(stderr, "%s\n", "End position");
	fprintf(stderr, "e_lat: %Lf\t", cd->e_lat);
	fprintf(stderr, "e_lon: %Lf\n", cd->e_lon);
	fprintf(stderr, "%s\n", "Average position");
	fprintf(stderr, "ave_lat: %Lf\t", cd->ave_lat);
	fprintf(stderr, "ave_lon: %Lf\n", cd->ave_lon);
	fprintf(stderr, "%s\n", "==================================================");
}
/* --------------------------------------------------------------- */
/* print the svp information on the screen */
static void print_svp(svp *cd) {
	struct tm *temp = &cd->svp_datum_time;
	fprintf(stderr, "%s\n", "==================================================");
	fprintf(stderr, "file_name: %s\n", cd->file_name);
	fprintf(stderr, "%s\n", "Date and time");
	fprintf(stderr, "\n%s\n", asctime(temp));
	fprintf(stderr, "%s\n", "position");
	fprintf(stderr, "lat: %Lf\t", cd->s_lat);
	fprintf(stderr, "lon: %Lf\n", cd->s_lon);
	fprintf(stderr, "%s\n", "==================================================");
}

/* ---------------------------------------------------------------- */
/*
 * pause the screen at the exit of the program
 */
static void pause_screen() {
	fprintf(stderr, "\nEnd the program press ENTER");
	// TODO(schwehr): Undefine behavior.  Was fflush on stdout intended?
	// fflush(stdin);
	getchar();
}
/* ------------------------------------------------------------------- */
static int read_list(char *list, char *list_2) {
	/* atexit(pause_screen); pause the screen - not armed inside a GMT session,
	   where it would stall the host process at its exit */

	/* open datalist.mb-1 for names of the files */
	FILE *fDatalist = fopen(list, MBSVPSELECT_READ);
	if (fDatalist == NULL) {
		fprintf(stderr, "%s Could not be found", list);
		return MB_FAILURE;
	}

	FILE *fSvp = fopen(list_2, MBSVPSELECT_READ);
	if (fSvp == NULL) {
		fprintf(stderr, "%s Could not be found", list_2);
		fclose(fDatalist);
		return MB_FAILURE;
	}
	FILE *fresult = fopen("result.txt", MBSVPSELECT_WRITE);
	if (fresult == NULL) {
		fprintf(stderr, "result.txt could not be found");
		fclose(fDatalist);
		fclose(fSvp);
		return MB_FAILURE;
	}
	// int count_size2;
	/* ------------------------------ */
	while ((fgets(dBuffer, sizeof dBuffer, fDatalist)) != NULL) {
		/* count_size2 = */ read_recursive2(dBuffer);
	}

	/* ------------------------------ */

	/* fill size */

	/* Allocate memory for inf_struct */
	inf *inf_hold = (inf *)(malloc((surveyLines_total) * sizeof(inf)));
	if (inf_hold == NULL) {
		fprintf(stderr, "no memory for the process end of process");
		fclose(fDatalist);
		fclose(fSvp);
		fclose(fresult);
		return MB_FAILURE;
	}
	/* zero the records: mktime() reads tm_isdst, which is never parsed */
	memset(inf_hold, 0, (surveyLines_total) * sizeof(inf));
	int size = surveyLines_total;
	for (int i = 0; i < surveyLines_total; i++) {
		if (fill_struct_inf(&inf_hold[i], holder[i]) != MB_SUCCESS) {
			free(inf_hold);
			fclose(fDatalist);
			fclose(fSvp);
			fclose(fresult);
			return MB_FAILURE;
		}
		if (verbose == 1)
			print_inf(&inf_hold[i]);
	}

	/* reset for svp_hold */
	/* ------------------------ */

	while ((fgets(sdBuffer, sizeof sdBuffer, fSvp) != NULL)) {
		int count_size = read_recursive(sdBuffer);
		svp_total += count_size;
	}
	/* ------------------------ */
	fprintf(stderr, "\n\n\n%d svp to be read\n\n\n", svp_total);
	/* fill size of svp_list */
	size_2 = svp_total;

	/* Allocate memory for svp_struct */
	svp *svp_hold = (svp *)(malloc((svp_total) * sizeof(svp)));
	if (svp_hold == NULL) {
		fprintf(stderr, "no memory for the process end of process");
		free(inf_hold);
		fclose(fDatalist);
		fclose(fSvp);
		fclose(fresult);
		return MB_FAILURE;
	}
	/* zero the records: mktime() reads tm_isdst, which is never parsed */
	memset(svp_hold, 0, (svp_total) * sizeof(svp));
#ifdef _WIN32
	int hour_hold[100][100];	// Have no idea if it's enough JL
	int min_hold[100][100];
	int day_hold[100][100];
#else
	int hour_hold[size][size_2];	// INVALID STANDARD C
	int min_hold[size][size_2];
	int day_hold[size][size_2];
#endif
	for (int i = 0; i < size_2; i++) {
		if (fill_struct_svp(&svp_hold[i], svps[i]) != MB_SUCCESS) {
			free(inf_hold);
			free(svp_hold);
			fclose(fDatalist);
			fclose(fSvp);
			fclose(fresult);
			return MB_FAILURE;
		}
		if (verbose == 1)
			print_svp(&svp_hold[i]);
	}

	/* calculating the distances and choose the appropriate file */
	if (p_flag == 0)
		fprintf(stderr, "\n Method chosen is %d nearest in position\n", p_flag);
	if (p_flag == 1)
		fprintf(stderr, "\n Method chosen is %d nearest in time\n", p_flag);
	if (p_flag == 2) {
		fprintf(stderr, "\n Method chosen is %d nearest in position within time\n", p_flag);
		if (n_p2 == 1)
			fprintf(stderr, "\n No specific time period was entered and the default time period %d hours will be taken\n", p_3_time);
		if (n_p2 == 2)
			fprintf(stderr, "\n Time period %d hours will be taken\n", p_3_time);
	}
	if (p_flag == 3) {
		fprintf(stderr, "\n Method chosen is %d nearest in time within range\n", p_flag);
		fprintf(stderr, "\n range  %d meters will be taken\n", p_4_range);
		if (p_4_flage == 0)
			fprintf(stderr, "\n Option 0 was chosen. The nearest in time within range will be calculated\n");

		if (p_4_flage == 1)
			fprintf(stderr, "\n Option 1 was chosen. The nearest in month within range will be calculated. This will calculate within the "
			       "specified range the SVP with the nearest month to the profile regardless of the year. This is the seasonal "
			       "interpretation \n");
	}
	int n = 0;
	struct geod_geodesic g;
	double azi1, azi2;

  // WGS84 ellipsoid parameters
  const double radius_equatorial = 6378137.0;
  const double radius_polar = 6356752.314245;
  const double flattening = 0.00335281066;

	geod_init(&g, radius_equatorial, flattening);
	size = surveyLines_total;
	for (int i = 0; i < size; i++) {
#ifdef _WIN32
		double dist[100][100];				// Have no idea if it's enough JL
		double time_hold[100][100];
#else
		double dist[size][size_2];
		double time_hold[size][size_2];
#endif
		char all_in_sys[BUFSIZ] = "mbset";
		if (p_flag == 0) {
			switch (inf_hold[i].flag) {
			case 0:
			{
				if (verbose == 1) {
					fprintf(stderr, "%s\n", "\n\n========N check passed no 0.0 position was found===========\n\n");
					fprintf(stderr, "\nCalculating the distances to all svp profiles for %s\n", inf_hold[i].file_name);
				}
				double temp_dist = 0.0;
				for (int j = 0; j < size_2; j++) {
					geod_inverse(&g, inf_hold[i].ave_lat, inf_hold[i].ave_lon, svp_hold[j].s_lat, svp_hold[j].s_lon, &dist[0][j],
					             &azi1, &azi2);
					if (j == 0) {
						temp_dist = dist[0][j];
					}
					if (temp_dist >= dist[0][j]) {
						temp_dist = dist[0][j];
						n = j;
					}
					if (verbose == 1)
						fprintf(stderr, "Distance number %d is : %lf\n", j, dist[0][j]);
				}
				if (verbose == 1) {
					fprintf(stderr, "\nSearching for the SVP with nearest position\n");
					fprintf(stderr, "the shortest distance is number %d from the list\n", n);
					fprintf(stderr, "%s\n", "==================================================");
				}

				fprintf(fresult, "%s\n", "============================================================");
				fprintf(fresult, "%s\t", inf_hold[i].file_name);
				fprintf(fresult, "%s\n", svp_hold[n].file_name);
				fprintf(fresult, "%s\n", "=============================================================");
				fprintf(stderr, "Calling mbset\n");
				/* fprintf(stderr, "%s\n", all_in_sys); */
				strcat(all_in_sys, " -I ");
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				strcat(all_in_sys, inf_hold[i].file_name);
				/* fprintf(stderr, "%s\n", all_in_sys); */
				strcat(all_in_sys, " -PSVPFILE:");
				strcat(all_in_sys, svp_hold[n].file_name);
				fprintf(stderr, "%s\n", all_in_sys);
				/* int shellstatus = */ system(all_in_sys);
				break;
			}
			case 1:
			{
				if (verbose == 1) {
					fprintf(stderr, "%s\n", "\n\n=====================N check:   0.0 position was found=====================\n\n");
					fprintf(stderr, "\nThe file %s has no navigation information at the start position and the svp profile will be "
					       "assigned to the end point of the file\n",
					       inf_hold[i].file_name);
				}
				double temp_dist = 0.0;
				for (int j = 0; j < size_2; j++) {
					geod_inverse(&g, inf_hold[i].s_lat, inf_hold[i].s_lon, svp_hold[j].s_lat, svp_hold[j].s_lon, &dist[0][j],
					             &azi1, &azi2);
					if (j == 0) {
						temp_dist = dist[0][j];
					}
					if (temp_dist >= dist[0][j]) {
						temp_dist = dist[0][j];
						n = j;
					}
					if (verbose == 1)
						fprintf(stderr, "Distance number %d is : %lf\n", j, dist[0][j]);
				}
				if (verbose == 1) {
					fprintf(stderr, "\nSearching for the SVP with nearest position\n");
					fprintf(stderr, "the shortest distance is number %d from the list\n", n);
					fprintf(stderr, "%s\n", "==================================================");
				}

				fprintf(fresult, "%s\n", "============================================================");
				fprintf(fresult, "%s\t", inf_hold[i].file_name);
				fprintf(fresult, "%s\n", svp_hold[n].file_name);
				fprintf(fresult, "%s\n", "=============================================================");
				fprintf(stderr, "Building the parameters to call mbset\n");
				/* fprintf(stderr, "%s\n", all_in_sys); */
				strcat(all_in_sys, " -I ");
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				strcat(all_in_sys, inf_hold[i].file_name);
				/* fprintf(stderr, "%s\n", all_in_sys); */
				strcat(all_in_sys, " -P ");
				strcat(all_in_sys, svp_hold[n].file_name);
				fprintf(stderr, "%s\n", all_in_sys);
				/* int shellstatus = */ system(all_in_sys);
				break;
			}
			case 2:
			{
				if (verbose == 1) {
					fprintf(stderr, "%s\n", "\n\n==============N check:   0.0 position was found===================\n\n");
					fprintf(stderr, "\nThe file %s has no navigation information at the end position and the svp profile will be assigned "
					       "to the start point of the file\n",
					       inf_hold[i].file_name);
				}
				double temp_dist = 0.0;
				for (int j = 0; j < size_2; j++) {
					geod_inverse(&g, inf_hold[i].e_lat, inf_hold[i].e_lon, svp_hold[j].s_lat, svp_hold[j].s_lon, &dist[0][j],
					             &azi1, &azi2);
					if (j == 0) {
						temp_dist = dist[0][j];
					}
					if (temp_dist >= dist[0][j]) {
						temp_dist = dist[0][j];
						n = j;
					}
					if (verbose == 1)
						fprintf(stderr, "Distance number %d is : %lf\n", j, dist[0][j]);
				}
				if (verbose == 1) {
					fprintf(stderr, "\nSearching for the SVP with nearest position\n");
					fprintf(stderr, "the shortest distance is number %d from the list\n", n);
					fprintf(stderr, "%s\n", "==================================================");
				}
				fprintf(fresult, "%s\n", "============================================================");
				fprintf(fresult, "%s\t", inf_hold[i].file_name);
				fprintf(fresult, "%s\n", svp_hold[n].file_name);
				fprintf(stderr, "Building the parameters to call mbset\n");
				/* fprintf(stderr, "%s\n", all_in_sys); */
				strcat(all_in_sys, " -I ");
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				strcat(all_in_sys, inf_hold[i].file_name);
				/* fprintf(stderr, "%s\n", all_in_sys); */
				strcat(all_in_sys, " -P ");
				strcat(all_in_sys, svp_hold[n].file_name);
				fprintf(stderr, "%s\n", all_in_sys);
				/* int shellstatus = */ system(all_in_sys);
				fprintf(fresult, "%s\n", "=============================================================");
				break;
			}
			case 3:
			{
				if (verbose == 1) {
					fprintf(stderr, "%s\n", "\n\n==============N check:   0.0 position was found====================\n\n");
					fprintf(stderr, "\n!!!The file %s has no navigation information and no svp will be assigned to it!!!\n",
					       inf_hold[i].file_name);
					fprintf(fresult, "%s\n", "============================================================");
					fprintf(fresult, "%s\t", inf_hold[i].file_name);
					fprintf(fresult, "%s\n", "NaN");
				}
				break;
			}

			default:
				break;
			} /* switch */
		} else {
			if (p_flag == 1) /* calculate the nearest in time */
			{
				if (verbose == 1) {
					fprintf(stderr, "%s\n", "==================================================");
					fprintf(stderr, "\nCalculating the nearest svp in time for for %s\n", inf_hold[i].file_name);
				}
				double temp_time = 0;
				for (int j = 0; j < size_2; j++) {
					time_hold[0][j] = fabs(difftime(inf_hold[i].s_Time, svp_hold[j].svp_Time));
					if (j == 0) {
						temp_time = time_hold[0][j];
					}
					if (temp_time >= time_hold[0][j]) {
						temp_time = time_hold[0][j];
						n = j;
					}
					if (verbose == 1)
						fprintf(stderr, "Time difference number %d is : %lf\n", j, time_hold[0][j]);
				}
				if (verbose == 1) {
					fprintf(stderr, "\nSearch for the SVP that is the nearest in Time\n");
					fprintf(stderr, "the shortest time interval is time difference number %d\n", n);
					fprintf(stderr, "%s\n", "==================================================");
				}

				fprintf(fresult, "%s\n", "============================================================");
				fprintf(fresult, "%s\t", inf_hold[i].file_name);
				fprintf(fresult, "%s\n", svp_hold[n].file_name);
				fprintf(fresult, "%s\n", "=============================================================");
				fprintf(stderr, "Building the parameters to call mbset\n");
				/* fprintf(stderr, "%s\n", all_in_sys); */
				strcat(all_in_sys, " -I ");
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				strcat(all_in_sys, inf_hold[i].file_name);
				/* fprintf(stderr, "%s\n", all_in_sys); */
				strcat(all_in_sys, " -PSVPFILE:");
				strcat(all_in_sys, svp_hold[n].file_name);
				fprintf(stderr, "%s\n", all_in_sys);
				/* int shellstatus = */ system(all_in_sys);
			}
			/************calculate the nearest in position within time***************************/
			if (p_flag == 2) {
				if (verbose == 1) {
					fprintf(stderr, "%s\n", "==================================================");
					fprintf(stderr, "\nCalculating the nearest svp in position within %d time period for for %s\n", p_3_time,
					       inf_hold[i].file_name);
				}
				double temp_dist = 0.0;
				double temp_dist2 = 0.0;
				int count = 0;
				int n_pos_time = 0;
				int n_pos = 0;
				for (int j = 0; j < size_2; j++) {
					time_hold[0][j] = fabs(difftime(inf_hold[i].s_Time, svp_hold[j].svp_Time)) - (p_3_time * 3600);

					/* dist[i][j] = distVincenty(inf_hold[i].ave_lat, inf_hold[i].ave_lon,
					 svp_hold[j].s_lat, svp_hold[j].s_lon); */
					geod_inverse(&g, inf_hold[i].ave_lat, inf_hold[i].ave_lon, svp_hold[j].s_lat, svp_hold[j].s_lon, &dist[0][j],
					             &azi1, &azi2);
					if (verbose == 1)
						fprintf(stderr, "Time difference number %d is : %lf\n", j, time_hold[0][j]);
					fprintf(stderr, "position difference number %d is : %lf\n", j, dist[0][j]);
					if (time_hold[0][j] < 0) {
						count = 1;
						if (j == 0 || temp_dist == 0) {
							temp_dist = dist[0][j];
							n_pos_time = j;
						}
						else {
							if (temp_dist >= dist[0][j]) {
								temp_dist = dist[0][j];
								n_pos_time = j;
							}
						}
					}
					else {

						if (j == 0 || temp_dist2 == 0) {
							temp_dist2 = dist[0][j];
							n_pos = j;
						}
						else {
							if (temp_dist2 >= dist[0][j]) {
								temp_dist2 = dist[0][j];
								n_pos = j;
							}
						}
					}
				}
				if (count == 0) {
					if (verbose == 1) {
						fprintf(stderr, "\nnon of the SVP profiles are within the time period, The tool is selecting nearest in position "
						       "without time considaration\n");
						fprintf(stderr, "the shortest distance is number %d from the list\n", n_pos);
					}
					n = n_pos;
				}
				else {
					if (verbose == 1)
						fprintf(stderr, "the shortest distance within time is number %d from the list\n", n_pos_time);
					n = n_pos_time;
				}
				fprintf(fresult, "%s\n", "============================================================");
				fprintf(fresult, "%s\t", inf_hold[i].file_name);
				fprintf(fresult, "%s\n", svp_hold[n].file_name);
				fprintf(fresult, "%s\n", "=============================================================");
				fprintf(stderr, "Building the parameters to call mbset\n");
				/* fprintf(stderr, "%s\n", all_in_sys); */
				strcat(all_in_sys, " -I ");
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				strcat(all_in_sys, inf_hold[i].file_name);
				/* fprintf(stderr, "%s\n", all_in_sys); */
				strcat(all_in_sys, " -PSVPFILE:");
				strcat(all_in_sys, svp_hold[n].file_name);
				fprintf(stderr, "%s\n", all_in_sys);
				/* int shellstatus = */ system(all_in_sys);
			}
			if (p_flag == 3) {
				// SVP nearest in time within range
				if (verbose == 1) {
					fprintf(stderr, "%s\n", "==================================================");
					fprintf(stderr, "\nCalculating the nearest svp in time within %d range for for %s\n", p_4_range,
					       inf_hold[i].file_name);
				}
				if (p_4_flage == 0)
					fprintf(stderr, "\n Calculating the nearest SVP in time\n");
				if (p_4_flage == 1)
					fprintf(stderr, "\n Calculating the nearest SVP in month (seasonal selection)\n");
				double temp_time = -9999;
				double temp_day = -9999;
				double temp_hour = -9999;
				double temp_min = -9999;
				double temp_time2 = -9999;
				double temp_day2 = -9999;
				double temp_hour2 = -9999;
				double temp_min2 = -9999;
				int count = 0;
				int n_time_noSeason = 0;
				int n_time_season = 0;
				int n_pos_seasn = 0;
				int n_pos_noSeason = 0;

				for (int j = 0; j < size_2; j++) {
					// processing time differences
					day_hold[0][j] = abs(inf_hold[i].s_datum_time.tm_yday - svp_hold[j].svp_datum_time.tm_yday);
					hour_hold[0][j] = abs(inf_hold[i].s_datum_time.tm_hour - svp_hold[j].svp_datum_time.tm_hour);
					min_hold[0][j] = abs(inf_hold[i].s_datum_time.tm_min - svp_hold[j].svp_datum_time.tm_min);
					time_hold[0][j] = fabs(difftime(inf_hold[i].s_Time, svp_hold[j].svp_Time));
					// processing distance differences
					geod_inverse(&g, inf_hold[i].ave_lat, inf_hold[i].ave_lon, svp_hold[j].s_lat, svp_hold[j].s_lon, &dist[0][j],
					             &azi1, &azi2);
					dist[0][j] -= p_4_range;
					// if the SVP is within the range
					fprintf(stderr, "%s\n", "==================================================");
					fprintf(stderr, "year day difference %d is : %d\n", j, day_hold[0][j]);
					fprintf(stderr, "hour difference %d is : %d\n", j, hour_hold[0][j]);
					fprintf(stderr, "minute difference %d is : %d\n", j, min_hold[0][j]);
					fprintf(stderr, "Time difference %d is : %lf\n", j, time_hold[0][j]);
					fprintf(stderr, "distance - range (if positive then SVP out of range if negative then the SVP within range) %d is : "
					       "%lf\n",
					       j, dist[0][j]);

					if (dist[0][j] < 0) {
						count = 1;
						if (p_4_flage == 0) {
							if (j == 0 || temp_time == -9999) {
								temp_time = time_hold[0][j];
								n_time_noSeason = j;
							}
							else {
								if (temp_time >= time_hold[0][j]) {
									temp_time = time_hold[0][j];
									n_time_noSeason = j;
								}
							}
						}
						else {
							if (j == 0 || (temp_day == -9999 && temp_hour == -9999 && temp_min == -9999)) {
								temp_day = day_hold[0][j];
								temp_hour = hour_hold[0][j];
								temp_min = min_hold[0][j];
								n_time_season = j;
							}
							else {
								if (temp_day > day_hold[0][j]) {
									temp_day = day_hold[0][j];
									temp_hour = hour_hold[0][j];
									temp_min = min_hold[0][j];
									n_time_season = j;
								}
								else if (temp_day == day_hold[0][j]) {
									if (temp_hour > hour_hold[0][j]) {
										temp_day = day_hold[0][j];
										temp_hour = hour_hold[0][j];
										temp_min = min_hold[0][j];
										n_time_season = j;
									}
									else if (temp_hour == hour_hold[0][j]) {
										if (temp_min > min_hold[0][j]) {
											temp_day = day_hold[0][j];
											temp_hour = hour_hold[0][j];
											temp_min = min_hold[0][j];
											n_time_season = j;
										}
									}
								}
							}
						}
					}

					// if the SVP is out of the range
					else {
						if (p_4_flage == 0) {
							if (j == 0 || temp_time2 == -9999) {
								temp_time2 = time_hold[0][j];
								n_pos_noSeason = j;
							}
							else {
								if (temp_time2 >= time_hold[0][j]) {
									temp_time2 = time_hold[0][j];
									n_pos_noSeason = j;
								}
							}
						}
						else {
							if (j == 0 || (temp_day2 == -9999 && temp_hour2 == -9999 && temp_min2 == -9999)) {
								temp_day2 = day_hold[0][j];
								temp_hour2 = hour_hold[0][j];
								temp_min2 = min_hold[0][j];
								n_pos_seasn = j;
							}
							else {
								if (temp_day2 > day_hold[0][j]) {
									temp_day2 = day_hold[0][j];
									temp_hour2 = hour_hold[0][j];
									temp_min2 = min_hold[0][j];
									n_pos_seasn = j;
								}
								else if (temp_day2 == day_hold[0][j]) {
									if (temp_hour2 > hour_hold[0][j]) {
										temp_day2 = day_hold[0][j];
										temp_hour2 = hour_hold[0][j];
										temp_min2 = min_hold[0][j];
										n_pos_seasn = j;
									}
									else if (temp_hour2 == hour_hold[0][j]) {
										if (temp_min2 > min_hold[0][j]) {
											temp_day2 = day_hold[0][j];
											temp_hour2 = hour_hold[0][j];
											temp_min2 = min_hold[0][j];
											n_pos_seasn = j;
										}
									}
								}
							}
						}
					}
				}

				if (count == 0) {
					if (p_4_flage == 0) {
						if (verbose == 1) {
							fprintf(stderr, "\nnon of the SVP profiles are within the specified range, The tool is selecting nearest in "
							       "time without range considaration\n");
							fprintf(stderr, "the nearest in time is number %d from the list\n", n_pos_noSeason);
						}
						n = n_pos_noSeason;
					}
					else {
						if (verbose == 1) {
							fprintf(stderr, "\nnon of the SVP profiles are within the specified range, The tool is selecting nearest in "
							       "time without range considaration\n");
							fprintf(stderr, "the nearest in season is number %d from the list\n", n_pos_seasn);
						}
						n = n_pos_seasn;
					}
				}
				else {
					if (p_4_flage == 0) {
						if (verbose == 1)
							fprintf(stderr, "the nearest in time within range is number %d from the list\n", n_time_noSeason);
						n = n_time_noSeason;
					}
					else {
						if (verbose == 1)
							fprintf(stderr, "the nearest in season within range is number %d from the list\n", n_time_season);
						n = n_time_season;
					}
				}
				fprintf(fresult, "%s\n", "============================================================");
				fprintf(fresult, "%s\t", inf_hold[i].file_name);
				fprintf(fresult, "%s\n", svp_hold[n].file_name);
				fprintf(fresult, "%s\n", "=============================================================");
				fprintf(stderr, "Building the parameters to call mbset\n");
				strcat(all_in_sys, " -I ");
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				inf_hold[i].file_name[strlen(inf_hold[i].file_name) - 1] = '\0';
				strcat(all_in_sys, inf_hold[i].file_name);
				strcat(all_in_sys, " -PSVPFILE:");
				strcat(all_in_sys, svp_hold[n].file_name);
				fprintf(stderr, "%s\n", all_in_sys);
				/* int shellstatus = */ system(all_in_sys);
			}
		}
	}
	free(inf_hold);
	free(svp_hold);
	fclose(fDatalist);
	fclose(fSvp);
	fclose(fresult);
	return MB_SUCCESS;
} /* read_list */
/* ------------------------------------------------------------------- */

/* The body of the -P / --method case of the original getopt loop. It
 * returns 1 where the original paused the screen and called exit(0). */
static int mbsvpselect_method(const char *optarg) {
				int n1 = 0;	/* -P with no number is method 0 (was read uninitialized) */
				int n2;
				int n3;
				const int n = sscanf(optarg, "%d/%d/%d", &n1, &n2, &n3);
				n_p2 = n;
				/* fprintf(stderr, "\nthis is n %d \n", n); */
				if ((n1 != 0) && (n1 != 1) && (n1 != 2) && (n1 != 3)) {
					/* the program's explanation, on stderr: a module's stdout is data */
					fputs("Only four options are available: 0 for nearest position, 1 for nearest in time, 2 for both, 3 for nearest "
					      "in time within range\n", stderr);
					fputs("The default is svp_nearest in position\n", stderr);
					fputs("If option 2 is chosen without specifying time period, 10 hours is the default value\n", stderr);
					fputs("If option 3 is chosen without specifying range, 10000 meters is the default value\n", stderr);
					fputs("If option 3 is chosen two options are available : nearest in time and nearest in month\n", stderr);
					/* was: pause_screen() then exit(0) */
					return 1;
				}
				else {
					if (n == 0)
						p_flag = 0;
					if (n == 1) {
						p_flag = n1;
						if (p_flag == 2) {
							p_3_time = 10;
							n2 = p_3_time;
						}
						if (p_flag == 3) {
							p_4_range = 10000;
							n2 = p_4_range;
						}
					}
					if (n == 2) {
						p_flag = n1;
						if ((p_flag == 0) || (p_flag == 1))
							fprintf(stderr, "%s\n", "The options -P0 for nearest in position or -P1 for nearest in time do not need further arguments");

						if (p_flag == 2)
							p_3_time = n2;
						if (p_flag == 3)
							p_4_range = n2;
					}
					if (n == 3) {
						p_flag = n1;
						p_4_range = n2;
						p_4_flage = n3;
						if ((p_flag == 0) || (p_flag == 1))
							fprintf(stderr, "%s\n", "The options -P0 for nearest in position or -P1 for nearest in time do not need further arguments");

						if ((p_4_flage != 0) && (p_4_flage != 1)) {
							puts("If option 3 is chosen two options are available : nearest in time with -P3/0 and nearest in month "
							     "with -P3/1");
							/* was: pause_screen() then exit(0) */
							return 1;
						}
					}
				}
	return 0;
}

/* --- Control structure ---------------------------------------------- */

struct MBSVPSELECT_CTRL {
	int verbose;	/* the program's -V/-v count */
	struct mbss_H { bool active; } H;
	struct mbss_I { bool active; char datalist[BUFSIZ]; } I;
	struct mbss_N { bool active; int count; } N;
	struct mbss_P { bool active; } P;	/* each -P is applied in order by mbsvpselect_method() */
	struct mbss_S { bool active; char svplist[BUFSIZ]; } S;
};

static void *New_mbsvpselect_Ctrl(struct GMT_CTRL *GMT) {
	struct MBSVPSELECT_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBSVPSELECT_CTRL);
	return Ctrl;
}

static void Free_mbsvpselect_Ctrl(struct GMT_CTRL *GMT, struct MBSVPSELECT_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'N', "check-zero-position", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'H', "help",                "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",               "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'P', "method",              "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'S', "svplist",             "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'v', "verbose",             "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message_old);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n\nusage: %s\n\n", help_message, usage_message);
	GMT_Message(API, GMT_TIME_NONE,
	            "\t-I Input recursive swath datalist [datalist.mb-1].\n"
	            "\t-S Input recursive sound velocity profile list [svplist.mb-1].\n"
	            "\t-P Selection method: 0 nearest in position, 1 nearest in time,\n"
	            "\t   2/period nearest in position within period hours,\n"
	            "\t   3/range[/1] nearest in time (or month) within range meters.\n"
	            "\t-N Check for zero longitude/latitude positions.\n"
	            "\t-H Print help and exit.\n"
	            "\tEvery option also has the program's lower-case and long forms.\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse_mbsvpselect(struct GMT_CTRL *GMT, struct MBSVPSELECT_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt;
	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {
		case 'V': case 'v':
			Ctrl->verbose++;
			break;
		case 'H': case 'h':
			Ctrl->H.active = true;
			break;
		case 'I': case 'i':
			if (opt->arg && opt->arg[0]) {
				snprintf(Ctrl->I.datalist, sizeof(Ctrl->I.datalist), "%s", opt->arg);
				Ctrl->I.active = true;
			}
			else n_errors++;
			break;
		case 'N': case 'n':
			Ctrl->N.active = true;
			Ctrl->N.count++;
			break;
		case 'P': case 'p':
			Ctrl->P.active = true;
			break;
		case 'S': case 's':
			if (opt->arg && opt->arg[0]) {
				snprintf(Ctrl->S.svplist, sizeof(Ctrl->S.svplist), "%s", opt->arg);
				Ctrl->S.active = true;
			}
			else n_errors++;
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}
	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code) { Free_mbsvpselect_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbsvpselect(void *V_API, int mode, void *args);

/* ------------------------------------------------------------------- */

int GMT_mbsvpselect(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL, *opt = NULL;
	struct MBSVPSELECT_CTRL *Ctrl = NULL;
	int parse_error;

	if (!API) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no arguments is a valid run: datalist.mb-1 against svplist.mb-1 */
	if ((parse_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(parse_error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = (struct MBSVPSELECT_CTRL *)New_mbsvpselect_Ctrl(GMT);
	if ((parse_error = parse_mbsvpselect(GMT, Ctrl, options)) != GMT_NOERROR) Return(parse_error);
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	/* reset the file-scope state to its initial values: a GMT session may
	 * run this module more than once */
	p_flag = 0;
	p_3_time = 10;
	p_4_range = 10000;
	p_4_flage = 0;
	zero_test = 0;
	size_2 = 0;
	n_p2 = 0;
	svp_total = 0;
	surveyLines_total = 0;
	verbose = Ctrl->verbose;

	int error = MB_ERROR_NO_ERROR;

	char datalist[BUFSIZ] = "datalist.mb-1";
	char svplist[BUFSIZ] = "svplist.mb-1";

	{
		const bool help = false;	/* -H returned above */

		if (Ctrl->I.active)
			sscanf(Ctrl->I.datalist, "%1023s", datalist);
		if (Ctrl->N.active)
			zero_test += Ctrl->N.count;
		if (Ctrl->P.active) {
			for (opt = options; opt; opt = opt->next) {
				if (opt->option != 'P' && opt->option != 'p') continue;
				if (mbsvpselect_method(opt->arg ? opt->arg : "")) {
					/* the program paused the screen and exited 0: an unknown method is a bad option */
					Return(GMT_PARSE_ERROR);
				}
			}
		}
		if (Ctrl->S.active)
			sscanf(Ctrl->S.svplist, "%1023s", svplist);

		if (verbose == 1) {
			fprintf(stderr, "\nProgram %s\n", program_name);
			fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
		}

		if (verbose >= 2) {
			fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
			fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
			fprintf(stderr, "dbg2  Control Parameters:\n");
			fprintf(stderr, "dbg2       verbose:    %d\n", verbose);
			fprintf(stderr, "dbg2       help:       %d\n", help);
			fprintf(stderr, "dbg2       datalist:   %s\n", datalist);
			fprintf(stderr, "dbg2       svplist:    %s\n", svplist);
			fprintf(stderr, "dbg2       p_flag:     %d\n", p_flag);
			fprintf(stderr, "dbg2       p_3_time:   %d\n", p_3_time);
			fprintf(stderr, "dbg2       p_4_range:  %d\n", p_4_range);
			fprintf(stderr, "dbg2       p_4_flage:  %d\n", p_4_flage);
			fprintf(stderr, "dbg2       zero_test:  %d\n", zero_test);
		}

	}

	/* read_list() used to exit(1) on any failure */
	if (read_list(datalist, svplist) != MB_SUCCESS)
		Return(GMT_RUNTIME_ERROR);

	const int status = MB_SUCCESS;

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
		fprintf(stderr, "dbg2  Ending status:\n");
		fprintf(stderr, "dbg2       status:  %d\n", status);
		fprintf(stderr, "dbg2       error:   %d\n", error);
	}

	/* The program exits with MBIO's error; as a module that is a GMT error code, never an MBIO one */
	if (error != MB_ERROR_NO_ERROR) {
		char *message;
		mb_error(verbose, error, &message);
		GMT_Report(API, GMT_MSG_ERROR, "%s\n", message);
		Return(GMT_RUNTIME_ERROR);
	}
	Return(GMT_NOERROR);

} /* main */
/* ---------------------------------------------------------------- */
