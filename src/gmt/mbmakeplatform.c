/*--------------------------------------------------------------------
 *    The MB-system:  mbmakeplatform.c  9/5/2015
 *
 *    Copyright (c) 2015-2025 by
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
 * Mbmakeplatform creates an MB-System platform file from command line arguments
 * specifying the positional and angular offsets between the various sensors
 *
 * Author:  D. W. Caress
 * Date:  September 5, 2015
 *
 */
/*
 * GMT-module port of src/utilities/mbmakeplatform.cc: the program's long options (~200, and no short
 * ones, more than a module_kw table has letters for) come from GMT's option list in command-line
 * order through mb_gmt_mark_long_options() / mb_gmt_long_option(); main() becomes
 * GMT_mbmakeplatform() and every exit() a Return() with a GMT error code. Its result is the
 * program's own platform file; its messages go to stderr.
 */

#define THIS_MODULE_NAME "mbmakeplatform"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Create or modify an MB-System platform file"
/* Inputs and the platform file are named by long options and handled by the module itself. */
#define THIS_MODULE_KEYS "ID("
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _MSC_VER
#include "unistd_w.h"
#else
#include <unistd.h>
#endif
#include "mb_define.h"
#include "mb_format.h"
#include "mb_io.h"
#include "mb_status.h"
#include "mb_gmt_opts.h"

typedef enum {
    SENSOR_OFF = 0,
    SENSOR_ADD = 1,
    SENSOR_MODIFY = 2,
} sensor_mode_t;

static const char program_name[] = "mbmakeplatform";
static const char help_message[] =
    "mbmakeplatform creates or modifies an MB-System platform file.\n";
static const char usage_message[] =
    "mbmakeplatform \n"
    "\t--output=plffile\n"
    "\t[\n"
    "\t--verbose\n"
    "\t--help\n"
    "\t--input=plffile\n"
    "\t--swath=datalist\n"
    "\t--swath=swathfile\n"
    "\t--swath-format=value\n"
    "\t--platform-type-surface-vessel\n"
    "\t--platform-type-tow-body\n"
    "\t--platform-type-rov\n"
    "\t--platform-type-auv\n"
    "\t--platform-type-aircraft\n"
    "\t--platform-type-satellite\n"
    "\t--platform-name=string\n"
    "\t--platform-organization=string\n"
    "\t--platform-documentation-url\n"
    "\t--platform-start-time=yyyy/mm/dd/hh/mm/ss.ssssss\n"
    "\t--platform-end-time=yyyy/mm/dd/hh/mm/ss.ssssss\n"
    "\t--add-sensor-sonar-echosounder\n"
    "\t--add-sensor-sonar-multiechosounder\n"
    "\t--add-sensor-sonar-sidescan\n"
    "\t--add-sensor-sonar-interferometry\n"
    "\t--add-sensor-sonar-multibeam\n"
    "\t--add-sensor-sonar-multibeam-twohead\n"
    "\t--add-sensor-sonar-subbottom\n"
    "\t--add-sensor-camera-mono\n"
    "\t--add-sensor-camera-stereo\n"
    "\t--add-sensor-camera-video\n"
    "\t--add-sensor-lidar-scan\n"
    "\t--add-sensor-lidar-swath\n"
    "\t--add-sensor-position\n"
    "\t--add-sensor-compass\n"
    "\t--add-sensor-vru\n"
    "\t--add-sensor-imu\n"
    "\t--add-sensor-ins\n"
    "\t--add-sensor-ins-with-pressure\n"
    "\t--add-sensor-ctd\n"
    "\t--add-sensor-pressure\n"
    "\t--add-sensor-soundspeed\n"
    "\t--modify-sensor=sensorid\n"
    "\t--modify-sensor-bathymetry\n"
    "\t--modify-sensor-bathymetry1\n"
    "\t--modify-sensor-bathymetry2\n"
    "\t--modify-sensor-bathymetry3\n"
    "\t--modify-sensor-backscatter\n"
    "\t--modify-sensor-backscatter1\n"
    "\t--modify-sensor-backscatter2\n"
    "\t--modify-sensor-backscatter3\n"
    "\t--modify-sensor-subbottom\n"
    "\t--modify-sensor-subbottom1\n"
    "\t--modify-sensor-subbottom2\n"
    "\t--modify-sensor-subbottom3\n"
    "\t--modify-sensor-camera\n"
    "\t--modify-sensor-camera1\n"
    "\t--modify-sensor-camera2\n"
    "\t--modify-sensor-camera3\n"
    "\t--modify-sensor-position\n"
    "\t--modify-sensor-position1\n"
    "\t--modify-sensor-position2\n"
    "\t--modify-sensor-position3\n"
    "\t--modify-sensor-depth\n"
    "\t--modify-sensor-depth1\n"
    "\t--modify-sensor-depth2\n"
    "\t--modify-sensor-depth3\n"
    "\t--modify-sensor-heading\n"
    "\t--modify-sensor-heading1\n"
    "\t--modify-sensor-heading2\n"
    "\t--modify-sensor-heading3\n"
    "\t--modify-sensor-rollpitch\n"
    "\t--modify-sensor-rollpitch1\n"
    "\t--modify-sensor-rollpitch2\n"
    "\t--modify-sensor-rollpitch3\n"
    "\t--modify-sensor-heave\n"
    "\t--modify-sensor-heave1\n"
    "\t--modify-sensor-heave2\n"
    "\t--modify-sensor-heave3\n"
    "\t--sensor-model=string\n"
    "\t--sensor-manufacturer=string\n"
    "\t--sensor-serialnumber=string\n"
    "\t--sensor-capability-position\n"
    "\t--sensor-capability-depth\n"
    "\t--sensor-capability-altitude\n"
    "\t--sensor-capability-velocity\n"
    "\t--sensor-capability-acceleration\n"
    "\t--sensor-capability-pressure\n"
    "\t--sensor-capability-rollpitch\n"
    "\t--sensor-capability-heading\n"
    "\t--sensor-capability-magneticfield\n"
    "\t--sensor-capability-temperature\n"
    "\t--sensor-capability-conductivity\n"
    "\t--sensor-capability-salinity\n"
    "\t--sensor-capability-soundspeed\n"
    "\t--sensor-capability-gravity\n"
    "\t--sensor-capability-topography-echosounder\n"
    "\t--sensor-capability-topography-interferometry\n"
    "\t--sensor-capability-topography-sass\n"
    "\t--sensor-capability-topography-multibeam\n"
    "\t--sensor-capability-topography-photogrammetry\n"
    "\t--sensor-capability-topography-structurefrommotion\n"
    "\t--sensor-capability-topography-lidar\n"
    "\t--sensor-capability-topography-structuredlight\n"
    "\t--sensor-capability-topography-laserscanner\n"
    "\t--sensor-capability-backscatter-echosounder\n"
    "\t--sensor-capability-backscatter-sidescan\n"
    "\t--sensor-capability-backscatter-interferometry\n"
    "\t--sensor-capability-backscatter-sass\n"
    "\t--sensor-capability-backscatter-multibeam\n"
    "\t--sensor-capability-backscatter-lidar\n"
    "\t--sensor-capability-backscatter-structuredlight\n"
    "\t--sensor-capability-backscatter-laserscanner\n"
    "\t--sensor-capability-photography\n"
    "\t--sensor-capability-stereophotography\n"
    "\t--sensor-capability-video\n"
    "\t--sensor-capability-stereovideo\n"
    "\t--sensor-capability1=value\n"
    "\t--sensor-capability2=value\n"
    "\t--sensor-offsets=x/y/z/azimuth/roll/pitch\n"
    "\t--sensor-offset-positions=x/y/z\n"
    "\t--sensor-offset-angles=azimuth/roll/pitch\n"
    "\t--sensor-time-latency=value\n"
    "\t--sensor-time-latency-model=file\n"
    "\t--sensor-flipsign-heading\n"
    "\t--sensor-flipsign-roll\n"
    "\t--sensor-flipsign-pitch\n"
    "\t--sensor-source-bathymetry\n"
    "\t--sensor-source-bathymetry1\n"
    "\t--sensor-source-bathymetry2\n"
    "\t--sensor-source-bathymetry3\n"
    "\t--sensor-source-backscatter\n"
    "\t--sensor-source-backscatter1\n"
    "\t--sensor-source-backscatter2\n"
    "\t--sensor-source-backscatter3\n"
    "\t--sensor-source-subbottom\n"
    "\t--sensor-source-subbottom1\n"
    "\t--sensor-source-subbottom2\n"
    "\t--sensor-source-subbottom3\n"
    "\t--sensor-source-camera\n"
    "\t--sensor-source-camera1\n"
    "\t--sensor-source-camera2\n"
    "\t--sensor-source-camera3\n"
    "\t--sensor-source-position\n"
    "\t--sensor-source-position1\n"
    "\t--sensor-source-position2\n"
    "\t--sensor-source-position3\n"
    "\t--sensor-source-depth\n"
    "\t--sensor-source-depth1\n"
    "\t--sensor-source-depth2\n"
    "\t--sensor-source-depth3\n"
    "\t--sensor-source-heading\n"
    "\t--sensor-source-heading1\n"
    "\t--sensor-source-heading2\n"
    "\t--sensor-source-heading3\n"
    "\t--sensor-source-rollpitch\n"
    "\t--sensor-source-rollpitch1\n"
    "\t--sensor-source-rollpitch2\n"
    "\t--sensor-source-rollpitch3\n"
    "\t--sensor-source-heave\n"
    "\t--sensor-source-heave1\n"
    "\t--sensor-source-heave2\n"
    "\t--sensor-source-heave3\n"
    "\t--modify-offsets=ioff/x/y/z/azimuth/roll/pitch\n"
    "\t--modify-offset-positions=ioff/x/y/z\n"
    "\t--modify-offset-angles=ioff/azimuth/roll/pitch\n"
    "\t--modify-time-latency=value\n"
    "\t--modify-time-latency-model=file\n"
    "\t--end-sensor\n"
    "\t--set-source-bathymetry\n"
    "\t--set-source-bathymetry1\n"
    "\t--set-source-bathymetry2\n"
    "\t--set-source-bathymetry3\n"
    "\t--set-source-backscatter\n"
    "\t--set-source-backscatter1\n"
    "\t--set-source-backscatter2\n"
    "\t--set-source-backscatter3\n"
    "\t--set-source-subbottom\n"
    "\t--set-source-subbottom1\n"
    "\t--set-source-subbottom2\n"
    "\t--set-source-subbottom3\n"
    "\t--set-source-camera\n"
    "\t--set-source-camera1\n"
    "\t--set-source-camera2\n"
    "\t--set-source-camera3\n"
    "\t--set-source-position\n"
    "\t--set-source-position1\n"
    "\t--set-source-position2\n"
    "\t--set-source-position3\n"
    "\t--set-source-depth\n"
    "\t--set-source-depth1\n"
    "\t--set-source-depth2\n"
    "\t--set-source-depth3\n"
    "\t--set-source-heading\n"
    "\t--set-source-heading1\n"
    "\t--set-source-heading2\n"
    "\t--set-source-heading3\n"
    "\t--set-source-rollpitch\n"
    "\t--set-source-rollpitch1\n"
    "\t--set-source-rollpitch2\n"
    "\t--set-source-rollpitch3\n"
    "\t--set-source-heave\n"
    "\t--set-source-heave1\n"
    "\t--set-source-heave2\n"
    "\t--set-source-heave3\n"
    "\t]\n";


/*--------------------------------------------------------------------*/

/* The program's long options, in option_id order (it has no short ones) */
static const struct MB_GMT_LONGOPT_DEF long_options[] = {
	{"output", true},
	{"verbose", false},
	{"help", false},
	{"input", true},
	{"swath", true},
	{"swath-format", true},
	{"platform-type-surface-vessel", false},
	{"platform-type-tow-body", false},
	{"platform-type-rov", false},
	{"platform-type-auv", false},
	{"platform-type-aircraft", false},
	{"platform-type-satellite", false},
	{"platform-name", true},
	{"platform-organization", true},
	{"platform-documentation-url", true},
	{"platform-start-time", true},
	{"platform-end-time", true},
	{"add-sensor-sonar-echosounder", false},
	{"add-sensor-sonar-multiechosounder", false},
	{"add-sensor-sonar-sidescan", false},
	{"add-sensor-sonar-interferometry", false},
	{"add-sensor-sonar-multibeam", false},
	{"add-sensor-sonar-multibeam-twohead", false},
	{"add-sensor-sonar-subbottom", false},
	{"add-sensor-camera-mono", false},
	{"add-sensor-camera-stereo", false},
	{"add-sensor-camera-video", false},
	{"add-sensor-lidar-scan", false},
	{"add-sensor-lidar-swath", false},
	{"add-sensor-position", false},
	{"add-sensor-compass", false},
	{"add-sensor-vru", false},
	{"add-sensor-imu", false},
	{"add-sensor-ins", false},
	{"add-sensor-ins-with-pressure", false},
	{"add-sensor-ctd", false},
	{"add-sensor-pressure", false},
	{"add-sensor-soundspeed", false},
	{"modify-sensor", true},
	{"modify-sensor-bathymetry", false},
	{"modify-sensor-bathymetry1", false},
	{"modify-sensor-bathymetry2", false},
	{"modify-sensor-bathymetry3", false},
	{"modify-sensor-backscatter", false},
	{"modify-sensor-backscatter1", false},
	{"modify-sensor-backscatter2", false},
	{"modify-sensor-backscatter3", false},
	{"modify-sensor-subbottom", false},
	{"modify-sensor-subbottom1", false},
	{"modify-sensor-subbottom2", false},
	{"modify-sensor-subbottom3", false},
	{"modify-sensor-camera", false},
	{"modify-sensor-camera1", false},
	{"modify-sensor-camera2", false},
	{"modify-sensor-camera3", false},
	{"modify-sensor-position", false},
	{"modify-sensor-position1", false},
	{"modify-sensor-position2", false},
	{"modify-sensor-position3", false},
	{"modify-sensor-depth", false},
	{"modify-sensor-depth1", false},
	{"modify-sensor-depth2", false},
	{"modify-sensor-depth3", false},
	{"modify-sensor-heading", false},
	{"modify-sensor-heading1", false},
	{"modify-sensor-heading2", false},
	{"modify-sensor-heading3", false},
	{"modify-sensor-rollpitch", false},
	{"modify-sensor-rollpitch1", false},
	{"modify-sensor-rollpitch2", false},
	{"modify-sensor-rollpitch3", false},
	{"modify-sensor-heave", false},
	{"modify-sensor-heave1", false},
	{"modify-sensor-heave2", false},
	{"modify-sensor-heave3", false},
	{"sensor-model", true},
	{"sensor-manufacturer", true},
	{"sensor-serialnumber", true},
	{"sensor-capability-position", false},
	{"sensor-capability-depth", false},
	{"sensor-capability-altitude", false},
	{"sensor-capability-velocity", false},
	{"sensor-capability-acceleration", false},
	{"sensor-capability-pressure", false},
	{"sensor-capability-rollpitch", false},
	{"sensor-capability-heading", false},
	{"sensor-capability-magneticfield", false},
	{"sensor-capability-temperature", false},
	{"sensor-capability-conductivity", false},
	{"sensor-capability-salinity", false},
	{"sensor-capability-soundspeed", false},
	{"sensor-capability-gravity", false},
	{"sensor-capability-topography-echosounder", false},
	{"sensor-capability-topography-interferometry", false},
	{"sensor-capability-topography-sass", false},
	{"sensor-capability-topography-multibeam", false},
	{"sensor-capability-topography-photogrammetry", false},
	{"sensor-capability-topography-structurefrommotion", false},
	{"sensor-capability-topography-lidar", false},
	{"sensor-capability-topography-structuredlight", false},
	{"sensor-capability-topography-laserscanner", false},
	{"sensor-capability-backscatter-echosounder", false},
	{"sensor-capability-backscatter-sidescan", false},
	{"sensor-capability-backscatter-interferometry", false},
	{"sensor-capability-backscatter-sass", false},
	{"sensor-capability-backscatter-multibeam", false},
	{"sensor-capability-backscatter-lidar", false},
	{"sensor-capability-backscatter-structuredlight", false},
	{"sensor-capability-backscatter-laserscanner", false},
	{"sensor-capability-photography", false},
	{"sensor-capability-stereophotography", false},
	{"sensor-capability-video", false},
	{"sensor-capability-stereovideo", false},
	{"sensor-capability1", true},
	{"sensor-capability2", true},
	{"sensor-offsets", true},
	{"sensor-offset-positions", true},
	{"sensor-offset-angles", true},
	{"sensor-time-latency", true},
	{"sensor-time-latency-model", true},
	{"sensor-flipsign-heading", false},
	{"sensor-flipsign-roll", false},
	{"sensor-flipsign-pitch", false},
	{"sensor-source-bathymetry", false},
	{"sensor-source-bathymetry1", false},
	{"sensor-source-bathymetry2", false},
	{"sensor-source-bathymetry3", false},
	{"sensor-source-backscatter", false},
	{"sensor-source-backscatter1", false},
	{"sensor-source-backscatter2", false},
	{"sensor-source-backscatter3", false},
	{"sensor-source-subbottom", false},
	{"sensor-source-subbottom1", false},
	{"sensor-source-subbottom2", false},
	{"sensor-source-subbottom3", false},
	{"sensor-source-camera", false},
	{"sensor-source-camera1", false},
	{"sensor-source-camera2", false},
	{"sensor-source-camera3", false},
	{"sensor-source-position", false},
	{"sensor-source-position1", false},
	{"sensor-source-position2", false},
	{"sensor-source-position3", false},
	{"sensor-source-depth", false},
	{"sensor-source-depth1", false},
	{"sensor-source-depth2", false},
	{"sensor-source-depth3", false},
	{"sensor-source-heading", false},
	{"sensor-source-heading1", false},
	{"sensor-source-heading2", false},
	{"sensor-source-heading3", false},
	{"sensor-source-rollpitch", false},
	{"sensor-source-rollpitch1", false},
	{"sensor-source-rollpitch2", false},
	{"sensor-source-rollpitch3", false},
	{"sensor-source-heave", false},
	{"sensor-source-heave1", false},
	{"sensor-source-heave2", false},
	{"sensor-source-heave3", false},
	{"modify-offsets", true},
	{"modify-offset-positions", true},
	{"modify-offset-angles", true},
	{"modify-time-latency", true},
	{"modify-time-latency-model", true},
	{"end-sensor", false},
	{"set-source-bathymetry", true},
	{"set-source-bathymetry1", true},
	{"set-source-bathymetry2", true},
	{"set-source-bathymetry3", true},
	{"set-source-backscatter", true},
	{"set-source-backscatter1", true},
	{"set-source-backscatter2", true},
	{"set-source-backscatter3", true},
	{"set-source-subbottom", true},
	{"set-source-subbottom1", true},
	{"set-source-subbottom2", true},
	{"set-source-subbottom3", true},
	{"set-source-camera", true},
	{"set-source-camera1", true},
	{"set-source-camera2", true},
	{"set-source-camera3", true},
	{"set-source-position", true},
	{"set-source-position1", true},
	{"set-source-position2", true},
	{"set-source-position3", true},
	{"set-source-depth", true},
	{"set-source-depth1", true},
	{"set-source-depth2", true},
	{"set-source-depth3", true},
	{"set-source-heading", true},
	{"set-source-heading1", true},
	{"set-source-heading2", true},
	{"set-source-heading3", true},
	{"set-source-rollpitch", true},
	{"set-source-rollpitch1", true},
	{"set-source-rollpitch2", true},
	{"set-source-rollpitch3", true},
	{"set-source-heave", true},
	{"set-source-heave1", true},
	{"set-source-heave2", true},
	{"set-source-heave3", true},
	{NULL, false}};

/* --- GMT front end ---------------------------------------------------- */

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "%s\n", help_message);
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

#define bailout(code) { gmt_M_free_options(mode); return code; }
#define Return(code) { gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbmakeplatform(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbmakeplatform(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	int gmt_error;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program (which then does nothing) */
	if ((gmt_error = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(gmt_error);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, NULL, &options, &GMT_cpy)) == NULL) bailout(API->error);
	/* the program's long options (more than a module_kw could give letters) kept out of GMT's
	   --PAR=value handling */
	mb_gmt_mark_long_options(API, &options, long_options);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

  int verbose = 0;
  int input_swath_format = 0;
  int pings = 1;
  int lonflip;
  double bounds[4];
  int btime_i[7];
  int etime_i[7];
  double speedmin;
  double timegap;
  int status = mb_defaults(verbose, &input_swath_format, &pings, &lonflip, bounds, btime_i, etime_i, &speedmin, &timegap);
  input_swath_format = 0;
  pings = 1;
  bounds[0] = -360.0;
  bounds[1] = 360.0;
  bounds[2] = -90.0;
  bounds[3] = 90.0;

  int error = MB_ERROR_NO_ERROR;
  struct mb_platform_struct *platform = NULL;
  status &= mb_platform_init(verbose, (void **)&platform, &error);


  /* process argument list - for this program all the action
      happens in this loop and the order of arguments matters
      - the input and output arguments must be given first */
  typedef enum {
    option_output,
    option_verbose,
    option_help,
    option_input,
    option_swath,
    option_swath_format,
    option_platform_type_surface_vessel,
    option_platform_type_tow_body,
    option_platform_type_rov,
    option_platform_type_auv,
    option_platform_type_aircraft,
    option_platform_type_satellite,
    option_platform_name,
    option_platform_organization,
    option_platform_documentation_url,
    option_platform_start_time,
    option_platform_end_time,
    option_add_sensor_sonar_echosounder,
    option_add_sensor_sonar_multiechosounder,
    option_add_sensor_sonar_sidescan,
    option_add_sensor_sonar_interferometry,
    option_add_sensor_sonar_multibeam,
    option_add_sensor_sonar_multibeam_twohead,
    option_add_sensor_sonar_subbottom,
    option_add_sensor_camera_mono,
    option_add_sensor_camera_stereo,
    option_add_sensor_camera_video,
    option_add_sensor_lidar_scan,
    option_add_sensor_lidar_swath,
    option_add_sensor_position,
    option_add_sensor_compass,
    option_add_sensor_vru,
    option_add_sensor_imu,
    option_add_sensor_ins,
    option_add_sensor_ins_with_pressure,
    option_add_sensor_ctd,
    option_add_sensor_pressure,
    option_add_sensor_soundspeed,
    option_modify_sensor,
    option_modify_sensor_bathymetry,
    option_modify_sensor_bathymetry1,
    option_modify_sensor_bathymetry2,
    option_modify_sensor_bathymetry3,
    option_modify_sensor_backscatter,
    option_modify_sensor_backscatter1,
    option_modify_sensor_backscatter2,
    option_modify_sensor_backscatter3,
    option_modify_sensor_subbottom,
    option_modify_sensor_subbottom1,
    option_modify_sensor_subbottom2,
    option_modify_sensor_subbottom3,
    option_modify_sensor_camera,
    option_modify_sensor_camera1,
    option_modify_sensor_camera2,
    option_modify_sensor_camera3,
    option_modify_sensor_position,
    option_modify_sensor_position1,
    option_modify_sensor_position2,
    option_modify_sensor_position3,
    option_modify_sensor_depth,
    option_modify_sensor_depth1,
    option_modify_sensor_depth2,
    option_modify_sensor_depth3,
    option_modify_sensor_heading,
    option_modify_sensor_heading1,
    option_modify_sensor_heading2,
    option_modify_sensor_heading3,
    option_modify_sensor_rollpitch,
    option_modify_sensor_rollpitch1,
    option_modify_sensor_rollpitch2,
    option_modify_sensor_rollpitch3,
    option_modify_sensor_heave,
    option_modify_sensor_heave1,
    option_modify_sensor_heave2,
    option_modify_sensor_heave3,
    option_sensor_model,
    option_sensor_manufacturer,
    option_sensor_serialnumber,
    option_sensor_capability_position,
    option_sensor_capability_depth,
    option_sensor_capability_altitude,
    option_sensor_capability_velocity,
    option_sensor_capability_acceleration,
    option_sensor_capability_pressure,
    option_sensor_capability_rollpitch,
    option_sensor_capability_heading,
    option_sensor_capability_magneticfield,
    option_sensor_capability_temperature,
    option_sensor_capability_conductivity,
    option_sensor_capability_salinity,
    option_sensor_capability_soundspeed,
    option_sensor_capability_gravity,
    option_sensor_capability_topography_echosounder,
    option_sensor_capability_topography_interferometry,
    option_sensor_capability_topography_sass,
    option_sensor_capability_topography_multibeam,
    option_sensor_capability_topography_photogrammetry,
    option_sensor_capability_topography_structurefrommotion,
    option_sensor_capability_topography_lidar,
    option_sensor_capability_topography_structuredlight,
    option_sensor_capability_topography_laserscanner,
    option_sensor_capability_backscatter_echosounder,
    option_sensor_capability_backscatter_sidescan,
    option_sensor_capability_backscatter_interferometry,
    option_sensor_capability_backscatter_sass,
    option_sensor_capability_backscatter_multibeam,
    option_sensor_capability_backscatter_lidar,
    option_sensor_capability_backscatter_structuredlight,
    option_sensor_capability_backscatter_laserscanner,
    option_sensor_capability_photography,
    option_sensor_capability_stereophotography,
    option_sensor_capability_video,
    option_sensor_capability_stereovideo,
    option_sensor_capability1,
    option_sensor_capability2,
    option_sensor_offsets,
    option_sensor_offset_positions,
    option_sensor_offset_angles,
    option_sensor_time_latency,
    option_sensor_time_latency_model,
    option_sensor_flipsign_heading,
    option_sensor_flipsign_roll,
    option_sensor_flipsign_pitch,
    option_sensor_source_bathymetry,
    option_sensor_source_bathymetry1,
    option_sensor_source_bathymetry2,
    option_sensor_source_bathymetry3,
    option_sensor_source_backscatter,
    option_sensor_source_backscatter1,
    option_sensor_source_backscatter2,
    option_sensor_source_backscatter3,
    option_sensor_source_subbottom,
    option_sensor_source_subbottom1,
    option_sensor_source_subbottom2,
    option_sensor_source_subbottom3,
    option_sensor_source_camera,
    option_sensor_source_camera1,
    option_sensor_source_camera2,
    option_sensor_source_camera3,
    option_sensor_source_position,
    option_sensor_source_position1,
    option_sensor_source_position2,
    option_sensor_source_position3,
    option_sensor_source_depth,
    option_sensor_source_depth1,
    option_sensor_source_depth2,
    option_sensor_source_depth3,
    option_sensor_source_heading,
    option_sensor_source_heading1,
    option_sensor_source_heading2,
    option_sensor_source_heading3,
    option_sensor_source_rollpitch,
    option_sensor_source_rollpitch1,
    option_sensor_source_rollpitch2,
    option_sensor_source_rollpitch3,
    option_sensor_source_heave,
    option_sensor_source_heave1,
    option_sensor_source_heave2,
    option_sensor_source_heave3,
    option_modify_offsets,
    option_modify_offset_positions,
    option_modify_offset_angles,
    option_modify_time_latency,
    option_modify_time_latency_model,
    option_end_sensor,
    option_set_source_bathymetry,
    option_set_source_bathymetry1,
    option_set_source_bathymetry2,
    option_set_source_bathymetry3,
    option_set_source_backscatter,
    option_set_source_backscatter1,
    option_set_source_backscatter2,
    option_set_source_backscatter3,
    option_set_source_subbottom,
    option_set_source_subbottom1,
    option_set_source_subbottom2,
    option_set_source_subbottom3,
    option_set_source_camera,
    option_set_source_camera1,
    option_set_source_camera2,
    option_set_source_camera3,
    option_set_source_position,
    option_set_source_position1,
    option_set_source_position2,
    option_set_source_position3,
    option_set_source_depth,
    option_set_source_depth1,
    option_set_source_depth2,
    option_set_source_depth3,
    option_set_source_heading,
    option_set_source_heading1,
    option_set_source_heading2,
    option_set_source_heading3,
    option_set_source_rollpitch,
    option_set_source_rollpitch1,
    option_set_source_rollpitch2,
    option_set_source_rollpitch3,
    option_set_source_heave,
    option_set_source_heave1,
    option_set_source_heave2,
    option_set_source_heave3,
  } option_id;


  /* MBIO read control parameters */
  mb_path swath_file;
  mb_path dfile;
  void *datalist;
  double file_weight;
  double btime_d;
  double etime_d;
  int beams_bath;
  int beams_amp;
  int pixels_ss;

  /* MBIO read values */
  void *mbio_ptr = NULL;
  struct mb_io_struct *mb_io_ptr = NULL;
  void *store_ptr = NULL;
  int kind;

  /* data record source types */
  int platform_source;
  int nav_source;
  int heading_source;
  int sensordepth_source;
  int attitude_source;
  int svp_source;

  /* platform */
  struct mb_sensor_struct tmp_sensor;
  memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
  struct mb_sensor_struct *active_sensor;
  struct mb_sensor_offset_struct tmp_offsets[4];
  memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));

  int platform_num_sensors = 0;
  mb_path input_platform_file;
  mb_path input_swath_file;
  bool input_swath_platform_defined = false;
  mb_path output_platform_file;
  bool output_platform_file_defined = false;
  mb_path time_latency_model_file;
  FILE *tfp = NULL;
  char buffer[MB_PATH_MAXLINE];
  sensor_mode_t sensor_mode = SENSOR_OFF;
  int sensor_id;
  int ioffset;

  int nscan;
  double d1, d2, d3, d4, d5, d6;
  double seconds;

  bool read_data;
  option_id option_index;
  bool errflg = false;
  /* the program's long options from GMT's option list, in their command-line order (the order
     matters: sensors are added, then modified, in sequence); GMT's own -V counts as --verbose */
  for (struct GMT_OPTION *opt = options; opt; opt = opt->next) {
    const char *optarg_value = "";
    int k;
    if (opt->option == MB_GMT_LONGOPT) {
      if ((k = mb_gmt_long_option(opt, long_options, &optarg_value)) == -2) {
        GMT_Report(API, GMT_MSG_ERROR, "Option --%s requires an argument\n", opt->arg);
        errflg = true;
        continue;
      }
    }
    else if (opt->option == 'V')
      k = option_verbose;
    else {
      errflg |= (gmt_default_option_error(GMT, opt) != 0);
      continue;
    }
    option_index = (option_id)k;
    {
      switch (option_index) {
        case option_verbose:
          verbose++;
          if (verbose == 1) {
            fprintf(stderr, "\nProgram %s\n", program_name);
            fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
          }
          break;

        case option_help:
          Return(usage(API, GMT_USAGE));
          break;

        case option_input:
          /* set the name of the input platform file */
          snprintf(input_platform_file, sizeof(input_platform_file), "%s", optarg_value);

          /* read the pre-existing platform file */
          status = mb_platform_read(verbose, input_platform_file, (void **)&platform, &error);
          if (status == MB_FAILURE) {
            fprintf(stderr, "\nUnable to read the pre-existing platform file: %s\n", input_platform_file);
            fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
            Return(GMT_ERROR_ON_FOPEN);
          }
          platform_num_sensors = platform->num_sensors;

          if (verbose > 0) {
            fprintf(stderr, "\nRead existing platform file <%s>\n", input_platform_file);
            fprintf(stderr, "    platform->type:                        %d <%s>\n", platform->type,
                    mb_platform_type((mb_platform_enum)(platform->type)));
            fprintf(stderr, "    platform->name:                        %s\n", platform->name);
            fprintf(stderr, "    platform->organization:                %s\n", platform->organization);
            fprintf(stderr, "    platform->documentation_url:           %s\n", platform->documentation_url);
            fprintf(stderr, "    platform->start_time_d:                %f\n", platform->start_time_d);
            fprintf(stderr, "    platform->start_time_i:                %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
                    platform->start_time_i[0], platform->start_time_i[1], platform->start_time_i[2],
                    platform->start_time_i[3], platform->start_time_i[4], platform->start_time_i[5],
                    platform->start_time_i[6]);
            fprintf(stderr, "    platform->end_time_d:                  %f\n", platform->end_time_d);
            fprintf(stderr, "    platform->end_time_i:                %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
                    platform->end_time_i[0], platform->end_time_i[1], platform->end_time_i[2], platform->end_time_i[3],
                    platform->end_time_i[4], platform->end_time_i[5], platform->end_time_i[6]);
            fprintf(stderr, "    platform->source_bathymetry:           %d\n", platform->source_bathymetry);
            fprintf(stderr, "    platform->source_bathymetry1:          %d\n", platform->source_bathymetry1);
            fprintf(stderr, "    platform->source_bathymetry2:          %d\n", platform->source_bathymetry2);
            fprintf(stderr, "    platform->source_bathymetry3:          %d\n", platform->source_bathymetry3);
            fprintf(stderr, "    platform->source_backscatter:          %d\n", platform->source_backscatter);
            fprintf(stderr, "    platform->source_backscatter1:         %d\n", platform->source_backscatter1);
            fprintf(stderr, "    platform->source_backscatter2:         %d\n", platform->source_backscatter2);
            fprintf(stderr, "    platform->source_backscatter3:         %d\n", platform->source_backscatter3);
            fprintf(stderr, "    platform->source_subbottom:            %d\n", platform->source_subbottom);
            fprintf(stderr, "    platform->source_subbottom1:           %d\n", platform->source_subbottom1);
            fprintf(stderr, "    platform->source_subbottom2:           %d\n", platform->source_subbottom2);
            fprintf(stderr, "    platform->source_subbottom3:           %d\n", platform->source_subbottom3);
            fprintf(stderr, "    platform->source_camera:               %d\n", platform->source_camera);
            fprintf(stderr, "    platform->source_camera1:              %d\n", platform->source_camera1);
            fprintf(stderr, "    platform->source_camera2:              %d\n", platform->source_camera2);
            fprintf(stderr, "    platform->source_camera3:              %d\n", platform->source_camera3);
            fprintf(stderr, "    platform->source_position:             %d\n", platform->source_position);
            fprintf(stderr, "    platform->source_position1:            %d\n", platform->source_position1);
            fprintf(stderr, "    platform->source_position2:            %d\n", platform->source_position2);
            fprintf(stderr, "    platform->source_position3:            %d\n", platform->source_position3);
            fprintf(stderr, "    platform->source_depth:                %d\n", platform->source_depth);
            fprintf(stderr, "    platform->source_depth1:               %d\n", platform->source_depth1);
            fprintf(stderr, "    platform->source_depth2:               %d\n", platform->source_depth2);
            fprintf(stderr, "    platform->source_depth3:               %d\n", platform->source_depth3);
            fprintf(stderr, "    platform->source_heading:              %d\n", platform->source_heading);
            fprintf(stderr, "    platform->source_heading1:             %d\n", platform->source_heading1);
            fprintf(stderr, "    platform->source_heading2:             %d\n", platform->source_heading2);
            fprintf(stderr, "    platform->source_heading3:             %d\n", platform->source_heading3);
            fprintf(stderr, "    platform->source_rollpitch:            %d\n", platform->source_rollpitch);
            fprintf(stderr, "    platform->source_rollpitch1:           %d\n", platform->source_rollpitch1);
            fprintf(stderr, "    platform->source_rollpitch2:           %d\n", platform->source_rollpitch2);
            fprintf(stderr, "    platform->source_rollpitch3:           %d\n", platform->source_rollpitch3);
            fprintf(stderr, "    platform->source_heave:                %d\n", platform->source_heave);
            fprintf(stderr, "    platform->source_heave1:               %d\n", platform->source_heave1);
            fprintf(stderr, "    platform->source_heave2:               %d\n", platform->source_heave2);
            fprintf(stderr, "    platform->source_heave3:               %d\n", platform->source_heave3);
            fprintf(stderr, "    platform->num_sensors:                 %d\n", platform->num_sensors);
            for (int i = 0; i < platform->num_sensors; i++) {
              int index = 0;
              for (int j = 0; j < NUM_MB_SENSOR_TYPES; j++)
                if (mb_sensor_type_id[j] == platform->sensors[i].type)
                  index = j;
              fprintf(stderr, "    platform->sensors[%d].type:                 %d <%s>\n", i, platform->sensors[i].type,
                      mb_sensor_type_string[index]);
              fprintf(stderr, "    platform->sensors[%d].model:                %s\n", i, platform->sensors[i].model);
              fprintf(stderr, "    platform->sensors[%d].manufacturer:         %s\n", i,
                      platform->sensors[i].manufacturer);
              fprintf(stderr, "    platform->sensors[%d].serialnumber:         %s\n", i,
                      platform->sensors[i].serialnumber);
              fprintf(stderr, "    platform->sensors[%d].capability1:          %d\n", i,
                      platform->sensors[i].capability1);
              fprintf(stderr, "    platform->sensors[%d].capability2:          %d\n", i,
                      platform->sensors[i].capability2);
              fprintf(stderr, "    platform->sensors[%d].num_offsets:          %d\n", i,
                      platform->sensors[i].num_offsets);
              for (int j = 0; j < platform->sensors[i].num_offsets; j++) {
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].position_offset_x:          %f\n", i, j,
                        platform->sensors[i].offsets[j].position_offset_x);
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].position_offset_y:          %f\n", i, j,
                        platform->sensors[i].offsets[j].position_offset_y);
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].position_offset_z:          %f\n", i, j,
                        platform->sensors[i].offsets[j].position_offset_z);
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].attitude_offset_heading:    %f\n", i, j,
                        platform->sensors[i].offsets[j].attitude_offset_heading);
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].attitude_offset_roll:       %f\n", i, j,
                        platform->sensors[i].offsets[j].attitude_offset_roll);
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].attitude_offset_pitch:      %f\n", i, j,
                        platform->sensors[i].offsets[j].attitude_offset_pitch);
              }
              fprintf(stderr, "    platform->sensors[%d].time_latency_mode:    %d\n", i,
                      platform->sensors[i].time_latency_mode);
              fprintf(stderr, "    platform->sensors[%d].time_latency_static:  %f\n", i,
                      platform->sensors[i].time_latency_static);
              fprintf(stderr, "    platform->sensors[%d].num_time_latency:     %d\n", i,
                      platform->sensors[i].num_time_latency);
              for (int j = 0; j < platform->sensors[i].num_time_latency; j++) {
                fprintf(stderr, "    platform->sensors[%d].time_latency[%d]:                       %16.6f %8.6f\n", i,
                        j, platform->sensors[i].time_latency_time_d[j], platform->sensors[i].time_latency_value[j]);
              }
              fprintf(stderr, "    platform->sensors[%d].heading_flipsign_heading:  %d\n", i,
                      platform->sensors[i].heading_flipsign_heading);
              fprintf(stderr, "    platform->sensors[%d].attitude_flipsign_roll:     %d\n", i,
                      platform->sensors[i].attitude_flipsign_roll);
              fprintf(stderr, "    platform->sensors[%d].attitude_flipsign_pitch:    %d\n", i,
                      platform->sensors[i].attitude_flipsign_pitch);
            }
          }
          break;

        case option_swath:
          /* set the name of the input platform file */
          snprintf(input_swath_file, sizeof(input_swath_file), "%s", optarg_value);

          /* get format if required */
          if (input_swath_format == 0)
            mb_get_format(verbose, input_swath_file, NULL, &input_swath_format, &error);

          /* open datalist or single swath file */
          if (input_swath_format < 0) {
            const int look_processed = MB_DATALIST_LOOK_UNSET;
            if (mb_datalist_open(verbose, &datalist, input_swath_file, look_processed, &error) != MB_SUCCESS) {
              fprintf(stderr, "\nUnable to open data list file: %s\n", input_swath_file);
              fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
              Return(GMT_ERROR_ON_FOPEN);
            }
            read_data = mb_datalist_read(verbose, datalist, swath_file, dfile, &input_swath_format, &file_weight,
                                           &error) == MB_SUCCESS;
          } else {
            strcpy(swath_file, input_swath_file);
            read_data = true;
          }

          /* loop over all files to be read */
          while (read_data && !input_swath_platform_defined) {
            /* check format and get data sources */
            if ((status =
                                               mb_format_source(verbose, &input_swath_format, &platform_source, &nav_source, &sensordepth_source,
                                                                &heading_source, &attitude_source, &svp_source, &error)) == MB_FAILURE) {
              char *message;
              mb_error(verbose, error, &message);
              fprintf(stderr, "\nMBIO Error returned from function <mb_format_source>:\n%s\n", message);
              fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
              Return(GMT_RUNTIME_ERROR);
            }

            /* initialize reading the swath file */
            if (mb_read_init(verbose, swath_file, input_swath_format, pings, lonflip, bounds, btime_i, etime_i,
                                       speedmin, timegap, &mbio_ptr, &btime_d, &etime_d, &beams_bath, &beams_amp,
                                       &pixels_ss, &error) != MB_SUCCESS) {
              char *message;
              mb_error(verbose, error, &message);
              fprintf(stderr, "\nMBIO Error returned from function <mb_read_init>:\n%s\n", message);
              fprintf(stderr, "\nMultibeam File <%s> not initialized for reading\n", swath_file);
              fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
              Return(GMT_RUNTIME_ERROR);
            }

            /* get store_ptr */
            mb_io_ptr = (struct mb_io_struct *)mbio_ptr;
            store_ptr = (void *)mb_io_ptr->store_data;

            /* read data */
            while (error <= MB_ERROR_NO_ERROR && !input_swath_platform_defined) {
              status = mb_read_ping(verbose, mbio_ptr, store_ptr, &kind, &error);

              /* if platform_source kind then extract platform definition */
              if (error <= MB_ERROR_NO_ERROR && kind == platform_source && platform_source != MB_DATA_NONE) {
                /* extract platform */
                status = mb_extract_platform(verbose, mbio_ptr, store_ptr, &kind, (void **)&platform, &error);

                /* note success */
                if (status == MB_SUCCESS && platform != NULL) {
                  input_swath_platform_defined = true;
                  platform_num_sensors = platform->num_sensors;
                }
              }
            }

            /* close the swath file */
            status &= mb_close(verbose, &mbio_ptr, &error);

            read_data = false;
          }

          if (verbose > 0 && input_swath_platform_defined) {
            fprintf(stderr, "\nExtracted platform from swath data <%s>\n", input_swath_file);
            fprintf(stderr, "    platform->type:                        %d <%s>\n", platform->type,
                    mb_platform_type((mb_platform_enum)(platform->type)));
            fprintf(stderr, "    platform->name:                        %s\n", platform->name);
            fprintf(stderr, "    platform->organization:                %s\n", platform->organization);
            fprintf(stderr, "    platform->documentation_url:           %s\n", platform->documentation_url);
            fprintf(stderr, "    platform->start_time_d:                %f\n", platform->start_time_d);
            fprintf(stderr, "    platform->start_time_i:                %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
                    platform->start_time_i[0], platform->start_time_i[1], platform->start_time_i[2],
                    platform->start_time_i[3], platform->start_time_i[4], platform->start_time_i[5],
                    platform->start_time_i[6]);
            fprintf(stderr, "    platform->end_time_d:                  %f\n", platform->end_time_d);
            fprintf(stderr, "    platform->end_time_i:                %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
                    platform->end_time_i[0], platform->end_time_i[1], platform->end_time_i[2], platform->end_time_i[3],
                    platform->end_time_i[4], platform->end_time_i[5], platform->end_time_i[6]);
            fprintf(stderr, "    platform->source_bathymetry:           %d\n", platform->source_bathymetry);
            fprintf(stderr, "    platform->source_bathymetry1:          %d\n", platform->source_bathymetry1);
            fprintf(stderr, "    platform->source_bathymetry2:          %d\n", platform->source_bathymetry2);
            fprintf(stderr, "    platform->source_bathymetry3:          %d\n", platform->source_bathymetry3);
            fprintf(stderr, "    platform->source_backscatter:          %d\n", platform->source_backscatter);
            fprintf(stderr, "    platform->source_backscatter1:         %d\n", platform->source_backscatter1);
            fprintf(stderr, "    platform->source_backscatter2:         %d\n", platform->source_backscatter2);
            fprintf(stderr, "    platform->source_backscatter3:         %d\n", platform->source_backscatter3);
            fprintf(stderr, "    platform->source_subbottom:            %d\n", platform->source_subbottom);
            fprintf(stderr, "    platform->source_subbottom1:           %d\n", platform->source_subbottom1);
            fprintf(stderr, "    platform->source_subbottom2:           %d\n", platform->source_subbottom2);
            fprintf(stderr, "    platform->source_subbottom3:           %d\n", platform->source_subbottom3);
            fprintf(stderr, "    platform->source_camera:               %d\n", platform->source_camera);
            fprintf(stderr, "    platform->source_camera1:              %d\n", platform->source_camera1);
            fprintf(stderr, "    platform->source_camera2:              %d\n", platform->source_camera2);
            fprintf(stderr, "    platform->source_camera3:              %d\n", platform->source_camera3);
            fprintf(stderr, "    platform->source_position:             %d\n", platform->source_position);
            fprintf(stderr, "    platform->source_position1:            %d\n", platform->source_position1);
            fprintf(stderr, "    platform->source_position2:            %d\n", platform->source_position2);
            fprintf(stderr, "    platform->source_position3:            %d\n", platform->source_position3);
            fprintf(stderr, "    platform->source_depth:                %d\n", platform->source_depth);
            fprintf(stderr, "    platform->source_depth1:               %d\n", platform->source_depth1);
            fprintf(stderr, "    platform->source_depth2:               %d\n", platform->source_depth2);
            fprintf(stderr, "    platform->source_depth3:               %d\n", platform->source_depth3);
            fprintf(stderr, "    platform->source_heading:              %d\n", platform->source_heading);
            fprintf(stderr, "    platform->source_heading1:             %d\n", platform->source_heading1);
            fprintf(stderr, "    platform->source_heading2:             %d\n", platform->source_heading2);
            fprintf(stderr, "    platform->source_heading3:             %d\n", platform->source_heading3);
            fprintf(stderr, "    platform->source_rollpitch:            %d\n", platform->source_rollpitch);
            fprintf(stderr, "    platform->source_rollpitch1:           %d\n", platform->source_rollpitch1);
            fprintf(stderr, "    platform->source_rollpitch2:           %d\n", platform->source_rollpitch2);
            fprintf(stderr, "    platform->source_rollpitch3:           %d\n", platform->source_rollpitch3);
            fprintf(stderr, "    platform->source_heave:                %d\n", platform->source_heave);
            fprintf(stderr, "    platform->source_heave1:               %d\n", platform->source_heave1);
            fprintf(stderr, "    platform->source_heave2:               %d\n", platform->source_heave2);
            fprintf(stderr, "    platform->source_heave3:               %d\n", platform->source_heave3);
            fprintf(stderr, "    platform->num_sensors:                 %d\n", platform->num_sensors);
            for (int i = 0; i < platform->num_sensors; i++) {
              int index = 0;
              for (int j = 0; j < NUM_MB_SENSOR_TYPES; j++)
                if (mb_sensor_type_id[j] == platform->sensors[i].type)
                  index = j;
              fprintf(stderr, "    platform->sensors[%d].type:                 %d <%s>\n", i, platform->sensors[i].type,
                      mb_sensor_type_string[index]);
              fprintf(stderr, "    platform->sensors[%d].model:                %s\n", i, platform->sensors[i].model);
              fprintf(stderr, "    platform->sensors[%d].manufacturer:         %s\n", i,
                      platform->sensors[i].manufacturer);
              fprintf(stderr, "    platform->sensors[%d].serialnumber:         %s\n", i,
                      platform->sensors[i].serialnumber);
              fprintf(stderr, "    platform->sensors[%d].capability1:          %d\n", i,
                      platform->sensors[i].capability1);
              fprintf(stderr, "    platform->sensors[%d].capability2:          %d\n", i,
                      platform->sensors[i].capability2);
              fprintf(stderr, "    platform->sensors[%d].num_offsets:          %d\n", i,
                      platform->sensors[i].num_offsets);
              for (int j = 0; j < platform->sensors[i].num_offsets; j++) {
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].position_offset_x:          %f\n", i, j,
                        platform->sensors[i].offsets[j].position_offset_x);
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].position_offset_y:          %f\n", i, j,
                        platform->sensors[i].offsets[j].position_offset_y);
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].position_offset_z:          %f\n", i, j,
                        platform->sensors[i].offsets[j].position_offset_z);
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].attitude_offset_heading:    %f\n", i, j,
                        platform->sensors[i].offsets[j].attitude_offset_heading);
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].attitude_offset_roll:       %f\n", i, j,
                        platform->sensors[i].offsets[j].attitude_offset_roll);
                fprintf(stderr, "    platform->sensors[%d].offsets[%d].attitude_offset_pitch:      %f\n", i, j,
                        platform->sensors[i].offsets[j].attitude_offset_pitch);
              }
              fprintf(stderr, "    platform->sensors[%d].time_latency_mode:    %d\n", i,
                      platform->sensors[i].time_latency_mode);
              fprintf(stderr, "    platform->sensors[%d].time_latency_static:  %f\n", i,
                      platform->sensors[i].time_latency_static);
              fprintf(stderr, "    platform->sensors[%d].num_time_latency:     %d\n", i,
                      platform->sensors[i].num_time_latency);
              for (int j = 0; j < platform->sensors[i].num_time_latency; j++) {
                fprintf(stderr, "    platform->sensors[%d].time_latency[%d]:                       %16.6f %8.6f\n", i,
                        j, platform->sensors[i].time_latency_time_d[j], platform->sensors[i].time_latency_value[j]);
              }
              fprintf(stderr, "    platform->sensors[%d].heading_flipsign_heading:  %d\n", i,
                      platform->sensors[i].heading_flipsign_heading);
              fprintf(stderr, "    platform->sensors[%d].attitude_flipsign_roll:     %d\n", i,
                      platform->sensors[i].attitude_flipsign_roll);
              fprintf(stderr, "    platform->sensors[%d].attitude_flipsign_pitch:    %d\n", i,
                      platform->sensors[i].attitude_flipsign_pitch);
            }
          }
          break;

        case option_swath_format:
          /* set the swath format */
          sscanf(optarg_value, "%d", &input_swath_format);
          break;

        case option_output:
          /* set output platform file */
          snprintf(output_platform_file, sizeof(output_platform_file), "%s", optarg_value);
          output_platform_file_defined = true;
          break;

        case option_platform_type_surface_vessel:
          platform->type = MB_PLATFORM_SURFACE_VESSEL;
          break;

        case option_platform_type_tow_body:
          platform->type = MB_PLATFORM_TOW_BODY;
          break;

        case option_platform_type_rov:
          platform->type = MB_PLATFORM_ROV;
          break;

        case option_platform_type_auv:
          platform->type = MB_PLATFORM_AUV;
          break;

        case option_platform_type_aircraft:
          platform->type = MB_PLATFORM_AIRCRAFT;
          break;

        case option_platform_type_satellite:
          platform->type = MB_PLATFORM_SATELLITE;
          break;

        case option_platform_name:
          snprintf(platform->name, sizeof(platform->name), "%s", optarg_value);
          break;

        case option_platform_organization:
          snprintf(platform->organization, sizeof(platform->organization), "%s", optarg_value);
          break;

        case option_platform_documentation_url:
          snprintf(platform->documentation_url, sizeof(platform->documentation_url), "%s", optarg_value);
          break;

        case option_platform_start_time:
          sscanf(optarg_value, "%d/%d/%d %d:%d:%lf", &platform->start_time_i[0], &platform->start_time_i[1],
                 &platform->start_time_i[2], &platform->start_time_i[3], &platform->start_time_i[4], &seconds);
          platform->start_time_i[5] = (int)floor(seconds);
          platform->start_time_i[6] = (int)(1000000 * (seconds - floor(seconds)));
          mb_get_time(verbose, platform->start_time_i, &platform->start_time_d);
          break;

        case option_platform_end_time:
          sscanf(optarg_value, "%d/%d/%d %d:%d:%lf", &platform->end_time_i[0], &platform->end_time_i[1], &platform->end_time_i[2],
                 &platform->end_time_i[3], &platform->end_time_i[4], &seconds);
          platform->end_time_i[5] = (int)floor(seconds);
          platform->end_time_i[6] = (int)(1000000 * (seconds - floor(seconds)));
          mb_get_time(verbose, platform->end_time_i, &platform->end_time_d);
          break;

        case option_add_sensor_sonar_echosounder:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_SONAR_ECHOSOUNDER;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_sonar_multiechosounder:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_SONAR_MULTIECHOSOUNDER;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_sonar_sidescan:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_SONAR_SIDESCAN;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_sonar_interferometry:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_SONAR_INTERFEROMETRY;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_sonar_multibeam:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_SONAR_MULTIBEAM;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_sonar_multibeam_twohead:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_SONAR_MULTIBEAM_TWOHEAD;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_sonar_subbottom:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_SONAR_SUBBOTTOM;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_camera_mono:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_CAMERA_MONO;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_camera_stereo:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_CAMERA_STEREO;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_camera_video:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_CAMERA_VIDEO;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_lidar_scan:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_LIDAR_SCAN;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_lidar_swath:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_LIDAR_SWATH;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_position:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_POSITION;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_compass:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_COMPASS;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_vru:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_VRU;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_imu:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_IMU;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_ins:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_INS;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_ins_with_pressure:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_INS_WITH_PRESSURE;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_ctd:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_CTD;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_pressure:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_PRESSURE;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;

        case option_add_sensor_soundspeed:
          if (sensor_mode == SENSOR_OFF) {
            sensor_mode = SENSOR_ADD;
            active_sensor = &tmp_sensor;
            memset(&tmp_sensor, 0, sizeof(struct mb_sensor_struct));
            memset(tmp_offsets, 0, 4 * sizeof(struct mb_sensor_offset_struct));
            tmp_sensor.type = MB_SENSOR_TYPE_SOUNDSPEED;
            sensor_id = platform_num_sensors;
            platform_num_sensors++;
          }
          break;
        case option_modify_sensor:
          sscanf(optarg_value, "%d", &sensor_id);
          sensor_mode = SENSOR_MODIFY;
          active_sensor = &platform->sensors[sensor_id];
          break;

        case option_modify_sensor_bathymetry:
          if (platform->source_bathymetry >= 0 && platform->source_bathymetry < platform_num_sensors) {
          	sensor_id = platform->source_bathymetry;
          	sensor_mode = SENSOR_MODIFY;
          	active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-bathymetry failed because no sensor is defined as the source for bathymetry\n");
          }
          break;

        case option_modify_sensor_bathymetry1:
          if (platform->source_bathymetry1 >= 0 && platform->source_bathymetry1 < platform_num_sensors) {
						sensor_id = platform->source_bathymetry1;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-bathymetry1 failed because no sensor is defined as the source for bathymetry1\n");
          }
          break;

        case option_modify_sensor_bathymetry2:
          if (platform->source_bathymetry2 >= 0 && platform->source_bathymetry2 < platform_num_sensors) {
						sensor_id = platform->source_bathymetry2;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-bathymetry2 failed because no sensor is defined as the source for bathymetry2\n");
          }
          break;

        case option_modify_sensor_bathymetry3:
          if (platform->source_bathymetry3 >= 0 && platform->source_bathymetry3 < platform_num_sensors) {
						sensor_id = platform->source_bathymetry3;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-bathymetry3 failed because no sensor is defined as the source for bathymetry3\n");
          }
          break;

        case option_modify_sensor_backscatter:
          if (platform->source_backscatter >= 0 && platform->source_backscatter < platform_num_sensors) {
						sensor_id = platform->source_backscatter;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-backscatter failed because no sensor is defined as the source for backscatter\n");
          }
          break;

        case option_modify_sensor_backscatter1:
          if (platform->source_backscatter1 >= 0 && platform->source_backscatter1 < platform_num_sensors) {
						sensor_id = platform->source_backscatter1;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-backscatter1 failed because no sensor is defined as the source for backscatter1\n");
          }
          break;

        case option_modify_sensor_backscatter2:
          if (platform->source_backscatter2 >= 0 && platform->source_backscatter2 < platform_num_sensors) {
						sensor_id = platform->source_backscatter2;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-backscatter2 failed because no sensor is defined as the source for backscatter2\n");
          }
          break;

        case option_modify_sensor_backscatter3:
          if (platform->source_backscatter3 >= 0 && platform->source_backscatter3 < platform_num_sensors) {
						sensor_id = platform->source_backscatter3;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-backscatter3 failed because no sensor is defined as the source for backscatter3\n");
          }
          break;

        case option_modify_sensor_subbottom:
          if (platform->source_subbottom >= 0 && platform->source_subbottom < platform_num_sensors) {
						sensor_id = platform->source_subbottom;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-subbottom failed because no sensor is defined as the source for subbottom\n");
          }
          break;

        case option_modify_sensor_subbottom1:
          if (platform->source_subbottom1 >= 0 && platform->source_subbottom1 < platform_num_sensors) {
						sensor_id = platform->source_subbottom1;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-subbottom1 failed because no sensor is defined as the source for subbottom1\n");
          }
          break;

        case option_modify_sensor_subbottom2:
          if (platform->source_subbottom2 >= 0 && platform->source_subbottom2 < platform_num_sensors) {
						sensor_id = platform->source_subbottom2;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-subbottom2 failed because no sensor is defined as the source for subbottom2\n");
          }
          break;

        case option_modify_sensor_subbottom3:
          if (platform->source_subbottom3 >= 0 && platform->source_subbottom3 < platform_num_sensors) {
						sensor_id = platform->source_subbottom3;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-subbottom3 failed because no sensor is defined as the source for subbottom3\n");
          }
          break;

        case option_modify_sensor_camera:
          if (platform->source_camera >= 0 && platform->source_camera < platform_num_sensors) {
          sensor_id = platform->source_camera;
          sensor_mode = SENSOR_MODIFY;
          active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-camera failed because no sensor is defined as the source for camera\n");
          }
          break;

        case option_modify_sensor_camera1:
          if (platform->source_camera1 >= 0 && platform->source_camera1 < platform_num_sensors) {
						sensor_id = platform->source_camera1;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-camera1 failed because no sensor is defined as the source for camera1\n");
          }
          break;

        case option_modify_sensor_camera2:
          if (platform->source_camera2 >= 0 && platform->source_camera2 < platform_num_sensors) {
						sensor_id = platform->source_camera2;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-camera2 failed because no sensor is defined as the source for camera2\n");
          }
          break;

        case option_modify_sensor_camera3:
          if (platform->source_camera3 >= 0 && platform->source_camera3 < platform_num_sensors) {
						sensor_id = platform->source_camera3;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-camera3 failed because no sensor is defined as the source for camera3\n");
          }
          break;

        case option_modify_sensor_position:
          if (platform->source_position >= 0 && platform->source_position < platform_num_sensors) {
						sensor_id = platform->source_position;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-position failed because no sensor is defined as the source for position\n");
          }
          break;

        case option_modify_sensor_position1:
          if (platform->source_position1 >= 0 && platform->source_position1 < platform_num_sensors) {
						sensor_id = platform->source_position1;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-position1 failed because no sensor is defined as the source for position1\n");
          }
          break;

        case option_modify_sensor_position2:
          if (platform->source_position2 >= 0 && platform->source_position2 < platform_num_sensors) {
						sensor_id = platform->source_position2;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-position2 failed because no sensor is defined as the source for position2\n");
          }
          break;

        case option_modify_sensor_position3:
          if (platform->source_position3 >= 0 && platform->source_position3 < platform_num_sensors) {
						sensor_id = platform->source_position3;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-position3 failed because no sensor is defined as the source for position3\n");
          }
          break;

        case option_modify_sensor_depth:
          if (platform->source_depth >= 0 && platform->source_depth < platform_num_sensors) {
						sensor_id = platform->source_depth;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-depth failed because no sensor is defined as the source for depth\n");
          }
          break;

        case option_modify_sensor_depth1:
          if (platform->source_depth1 >= 0 && platform->source_depth1 < platform_num_sensors) {
						sensor_id = platform->source_depth1;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-depth1 failed because no sensor is defined as the source for depth1\n");
          }
          break;

        case option_modify_sensor_depth2:
          if (platform->source_depth2 >= 0 && platform->source_depth2 < platform_num_sensors) {
						sensor_id = platform->source_depth2;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-depth2 failed because no sensor is defined as the source for depth2\n");
          }
          break;

        case option_modify_sensor_depth3:
          if (platform->source_depth3 >= 0 && platform->source_depth3 < platform_num_sensors) {
						sensor_id = platform->source_depth3;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-depth3 failed because no sensor is defined as the source for depth3\n");
          }
          break;

        case option_modify_sensor_heading:
          if (platform->source_heading >= 0 && platform->source_heading < platform_num_sensors) {
						sensor_id = platform->source_heading;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-heading failed because no sensor is defined as the source for heading\n");
          }
          break;

        case option_modify_sensor_heading1:
          if (platform->source_heading1 >= 0 && platform->source_heading1 < platform_num_sensors) {
						sensor_id = platform->source_heading1;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-heading1 failed because no sensor is defined as the source for heading1\n");
          }
          break;

        case option_modify_sensor_heading2:
          if (platform->source_heading2 >= 0 && platform->source_heading2 < platform_num_sensors) {
						sensor_id = platform->source_heading2;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-heading2 failed because no sensor is defined as the source for heading2\n");
          }
          break;

        case option_modify_sensor_heading3:
          if (platform->source_heading3 >= 0 && platform->source_heading3 < platform_num_sensors) {
						sensor_id = platform->source_heading3;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-heading3 failed because no sensor is defined as the source for heading3\n");
          }
          break;

        case option_modify_sensor_rollpitch:
          if (platform->source_rollpitch >= 0 && platform->source_rollpitch < platform_num_sensors) {
						sensor_id = platform->source_rollpitch;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-rollpitch failed because no sensor is defined as the source for rollpitch\n");
          }
          break;

        case option_modify_sensor_rollpitch1:
          if (platform->source_rollpitch1 >= 0 && platform->source_rollpitch1 < platform_num_sensors) {
						sensor_id = platform->source_rollpitch1;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-rollpitch1 failed because no sensor is defined as the source for rollpitch1\n");
          }
          break;

        case option_modify_sensor_rollpitch2:
          if (platform->source_rollpitch2 >= 0 && platform->source_rollpitch2 < platform_num_sensors) {
						sensor_id = platform->source_rollpitch2;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-rollpitch2 failed because no sensor is defined as the source for rollpitch2\n");
          }
          break;

        case option_modify_sensor_rollpitch3:
          if (platform->source_rollpitch3 >= 0 && platform->source_rollpitch3 < platform_num_sensors) {
						sensor_id = platform->source_rollpitch3;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-rollpitch3 failed because no sensor is defined as the source for rollpitch3\n");
          }
          break;

        case option_modify_sensor_heave:
          if (platform->source_heave >= 0 && platform->source_heave < platform_num_sensors) {
						sensor_id = platform->source_heave;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-heave failed because no sensor is defined as the source for heave\n");
          }
          break;

        case option_modify_sensor_heave1:
          if (platform->source_heave1 >= 0 && platform->source_heave1 < platform_num_sensors) {
						sensor_id = platform->source_heave1;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-heave1 failed because no sensor is defined as the source for heave1\n");
          }
          break;

        case option_modify_sensor_heave2:
          if (platform->source_heave2 >= 0 && platform->source_heave2 < platform_num_sensors) {
						sensor_id = platform->source_heave2;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-heave2 failed because no sensor is defined as the source for heave2\n");
          }
          break;

        case option_modify_sensor_heave3:
          if (platform->source_heave3 >= 0 && platform->source_heave3 < platform_num_sensors) {
						sensor_id = platform->source_heave3;
						sensor_mode = SENSOR_MODIFY;
						active_sensor = &platform->sensors[sensor_id];
          }
          else {
            fprintf(stderr, "Option modify-sensor-heave3 failed because no sensor is defined as the source for heave3\n");
          }
          break;

        case option_sensor_model:
          snprintf(tmp_sensor.model, sizeof(tmp_sensor.model), "%s", optarg_value);
          break;

        case option_sensor_manufacturer:
          snprintf(tmp_sensor.manufacturer, sizeof(tmp_sensor.manufacturer), "%s", optarg_value);
          break;

        case option_sensor_serialnumber:
          snprintf(tmp_sensor.serialnumber, sizeof(tmp_sensor.serialnumber), "%s", optarg_value);
          break;

        case option_sensor_capability_position:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_POSITION;
          break;

        case option_sensor_capability_depth:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_DEPTH;
          break;

        case option_sensor_capability_altitude:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_ALTITUDE;
          break;

        case option_sensor_capability_velocity:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_VELOCITY;
          break;

        case option_sensor_capability_acceleration:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_ACCELERATION;
          break;

        case option_sensor_capability_pressure:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_PRESSURE;
          break;

        case option_sensor_capability_rollpitch:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_ROLLPITCH;
          break;

        case option_sensor_capability_heading:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_HEADING;
          break;

        case option_sensor_capability_magneticfield:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_MAGNETICFIELD;
          break;

        case option_sensor_capability_temperature:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_TEMPERATURE;
          break;

        case option_sensor_capability_conductivity:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_CONDUCTIVITY;
          break;

        case option_sensor_capability_salinity:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_SALINITY;
          break;

        case option_sensor_capability_soundspeed:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_SOUNDSPEED;
          break;

        case option_sensor_capability_gravity:
          tmp_sensor.capability1 = tmp_sensor.capability1 | MB_SENSOR_CAPABILITY1_GRAVITY;
          break;

        case option_sensor_capability_topography_echosounder:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_TOPOGRAPHY_ECHOSOUNDER;
          break;

        case option_sensor_capability_topography_interferometry:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_TOPOGRAPHY_INTERFEROMETRY;
          break;

        case option_sensor_capability_topography_sass:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_TOPOGRAPHY_SASS;
          break;

        case option_sensor_capability_topography_multibeam:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_TOPOGRAPHY_MULTIBEAM;
          break;

        case option_sensor_capability_topography_photogrammetry:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_TOPOGRAPHY_PHOTOGRAMMETRY;
          break;

        case option_sensor_capability_topography_structurefrommotion:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_TOPOGRAPHY_STRUCTUREFROMMOTION;
          break;

        case option_sensor_capability_topography_lidar:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_TOPOGRAPHY_LIDAR;
          break;

        case option_sensor_capability_topography_structuredlight:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_TOPOGRAPHY_STRUCTUREDLIGHT;
          break;

        case option_sensor_capability_topography_laserscanner:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_TOPOGRAPHY_LASERSCANNER;
          break;

        case option_sensor_capability_backscatter_echosounder:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_BACKSCATTER_ECHOSOUNDER;
          break;

        case option_sensor_capability_backscatter_sidescan:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_BACKSCATTER_SIDESCAN;
          break;

        case option_sensor_capability_backscatter_interferometry:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_BACKSCATTER_INTERFEROMETRY;
          break;

        case option_sensor_capability_backscatter_sass:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_BACKSCATTER_SASS;
          break;

        case option_sensor_capability_backscatter_multibeam:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_BACKSCATTER_MULTIBEAM;
          break;

        case option_sensor_capability_backscatter_lidar:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_BACKSCATTER_LIDAR;
          break;

        case option_sensor_capability_backscatter_structuredlight:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_BACKSCATTER_STRUCTUREDLIGHT;
          break;

        case option_sensor_capability_backscatter_laserscanner:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_BACKSCATTER_LASERSCANNER;
          break;

        case option_sensor_capability_photography:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_PHOTOGRAPHY;
          break;

        case option_sensor_capability_stereophotography:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_STEREOPHOTOGRAPHY;
          break;

        case option_sensor_capability_video:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_VIDEO;
          break;

        case option_sensor_capability_stereovideo:
          tmp_sensor.capability2 = tmp_sensor.capability2 | MB_SENSOR_CAPABILITY2_STEREOVIDEO;
          break;

      /*-------------------------------------------------------
       * Set sensor capability bitmasks directly */
        case option_sensor_capability1:
          sscanf(optarg_value, "%d", &tmp_sensor.capability1);
          break;

        case option_sensor_capability2:
          sscanf(optarg_value, "%d", &tmp_sensor.capability2);
          break;

        case option_sensor_offsets:
          sscanf(optarg_value, "%lf/%lf/%lf/%lf/%lf/%lf", &tmp_offsets[tmp_sensor.num_offsets].position_offset_x,
                 &tmp_offsets[tmp_sensor.num_offsets].position_offset_y,
                 &tmp_offsets[tmp_sensor.num_offsets].position_offset_z,
                 &tmp_offsets[tmp_sensor.num_offsets].attitude_offset_heading,
                 &tmp_offsets[tmp_sensor.num_offsets].attitude_offset_roll,
                 &tmp_offsets[tmp_sensor.num_offsets].attitude_offset_pitch);
          tmp_sensor.num_offsets++;
          break;

        case option_sensor_offset_positions:
          sscanf(optarg_value, "%lf/%lf/%lf", &tmp_offsets[tmp_sensor.num_offsets].position_offset_x,
                 &tmp_offsets[tmp_sensor.num_offsets].position_offset_y,
                 &tmp_offsets[tmp_sensor.num_offsets].position_offset_z);
          tmp_sensor.num_offsets++;
          break;

        case option_sensor_offset_angles:
          sscanf(optarg_value, "%lf/%lf/%lf", &tmp_offsets[tmp_sensor.num_offsets].attitude_offset_heading,
                 &tmp_offsets[tmp_sensor.num_offsets].attitude_offset_roll,
                 &tmp_offsets[tmp_sensor.num_offsets].attitude_offset_pitch);
          tmp_sensor.num_offsets++;
          break;

        case option_sensor_time_latency:
          sscanf(optarg_value, "%lf", &tmp_sensor.time_latency_static);
          tmp_sensor.time_latency_mode = MB_SENSOR_TIME_LATENCY_STATIC;
          break;

        case option_sensor_time_latency_model:
          /* set the name of the input time latency file */
          snprintf(time_latency_model_file, sizeof(time_latency_model_file), "%s", optarg_value);
          tmp_sensor.time_latency_mode = MB_SENSOR_TIME_LATENCY_MODEL;

          /* count the data points in the time latency file */
          tmp_sensor.num_time_latency = 0;
          if ((tfp = fopen(time_latency_model_file, "r")) == NULL) {
            fprintf(stderr, "\nUnable to open time latency model file <%s> for reading\n", time_latency_model_file);
            fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
            Return(GMT_ERROR_ON_FOPEN);
          }
          char *result;
          while ((result = fgets(buffer, MB_PATH_MAXLINE, tfp)) == buffer)
            if (buffer[0] != '#')
              tmp_sensor.num_time_latency++;
          rewind(tfp);

          /* allocate arrays for time latency */
          if (tmp_sensor.num_time_latency > tmp_sensor.num_time_latency_alloc) {
            status = mb_mallocd(verbose, __FILE__, __LINE__, tmp_sensor.num_time_latency * sizeof(double),
                                (void **)&tmp_sensor.time_latency_time_d, &error);
            if (error == MB_ERROR_NO_ERROR)
              status = mb_mallocd(verbose, __FILE__, __LINE__, tmp_sensor.num_time_latency * sizeof(double),
                                  (void **)&tmp_sensor.time_latency_value, &error);
            if (error != MB_ERROR_NO_ERROR) {
              char *message;
              mb_error(verbose, error, &message);
              fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
              fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
              Return(GMT_MEMORY_ERROR);
            }
            tmp_sensor.num_time_latency_alloc = tmp_sensor.num_time_latency;
          }

          /* read the data points in the time latency file */
          tmp_sensor.num_time_latency = 0;
          while ((result = fgets(buffer, MB_PATH_MAXLINE, tfp)) == buffer) {
            if (buffer[0] != '#') {
              /* read the time and time latency pair */
              if (sscanf(buffer, "%lf %lf", &tmp_sensor.time_latency_time_d[tmp_sensor.num_time_latency],
                         &tmp_sensor.time_latency_value[tmp_sensor.num_time_latency]) == 2)
                tmp_sensor.num_time_latency++;
            }
          }
          fclose(tfp);
          break;

        case option_sensor_flipsign_heading:
          tmp_sensor.heading_flipsign_heading = true;
          break;

        case option_sensor_flipsign_roll:
          tmp_sensor.attitude_flipsign_roll = true;
          break;

        case option_sensor_flipsign_pitch:
          tmp_sensor.attitude_flipsign_pitch = true;
          break;

        case option_sensor_source_bathymetry:
          platform->source_bathymetry = sensor_id;
          break;

        case option_sensor_source_bathymetry1:
          platform->source_bathymetry1 = sensor_id;
          break;

        case option_sensor_source_bathymetry2:
          platform->source_bathymetry2 = sensor_id;
          break;

        case option_sensor_source_bathymetry3:
          platform->source_bathymetry3 = sensor_id;
          break;

        case option_sensor_source_backscatter:
          platform->source_backscatter = sensor_id;
          break;

        case option_sensor_source_backscatter1:
          platform->source_backscatter1 = sensor_id;
          break;

        case option_sensor_source_backscatter2:
          platform->source_backscatter2 = sensor_id;
          break;

        case option_sensor_source_backscatter3:
          platform->source_backscatter3 = sensor_id;
          break;

        case option_sensor_source_subbottom:
        platform->source_subbottom = sensor_id;
          break;

        case option_sensor_source_subbottom1:
        platform->source_subbottom1 = sensor_id;
          break;

        case option_sensor_source_subbottom2:
          platform->source_subbottom2 = sensor_id;
          break;

        case option_sensor_source_subbottom3:
          platform->source_subbottom3 = sensor_id;
          break;

        case option_sensor_source_camera:
        platform->source_camera = sensor_id;
          break;

        case option_sensor_source_camera1:
        platform->source_camera1 = sensor_id;
          break;

        case option_sensor_source_camera2:
          platform->source_camera2 = sensor_id;
          break;

        case option_sensor_source_camera3:
          platform->source_camera3 = sensor_id;
          break;

        case option_sensor_source_position:
          platform->source_position = sensor_id;
          break;

        case option_sensor_source_position1:
          platform->source_position1 = sensor_id;
          break;

        case option_sensor_source_position2:
          platform->source_position2 = sensor_id;
          break;

        case option_sensor_source_position3:
          platform->source_position3 = sensor_id;
          break;

        case option_sensor_source_depth:
          platform->source_depth = sensor_id;
          break;

        case option_sensor_source_depth1:
          platform->source_depth1 = sensor_id;
          break;

        case option_sensor_source_depth2:
          platform->source_depth2 = sensor_id;
          break;

        case option_sensor_source_depth3:
          platform->source_depth3 = sensor_id;
          break;

        case option_sensor_source_heading:
          platform->source_heading = sensor_id;
          break;

        case option_sensor_source_heading1:
          platform->source_heading1 = sensor_id;
          break;

        case option_sensor_source_heading2:
          platform->source_heading2 = sensor_id;
          break;

        case option_sensor_source_heading3:
          platform->source_heading3 = sensor_id;
          break;

        case option_sensor_source_rollpitch:
          platform->source_rollpitch = sensor_id;
          break;

        case option_sensor_source_rollpitch1:
          platform->source_rollpitch1 = sensor_id;
          break;

        case option_sensor_source_rollpitch2:
          platform->source_rollpitch2 = sensor_id;
          break;

        case option_sensor_source_rollpitch3:
          platform->source_rollpitch3 = sensor_id;
          break;

        case option_sensor_source_heave:
          platform->source_heave = sensor_id;
          break;

        case option_sensor_source_heave1:
          platform->source_heave1 = sensor_id;
          break;

        case option_sensor_source_heave2:
          platform->source_heave2 = sensor_id;
          break;

        case option_sensor_source_heave3:
          platform->source_heave3 = sensor_id;
          break;

        case option_modify_offsets:
          if (sensor_mode == SENSOR_MODIFY || sensor_mode == SENSOR_ADD) {
            nscan = sscanf(optarg_value, "%d/%lf/%lf/%lf/%lf/%lf/%lf", &ioffset, &d1, &d2, &d3, &d4, &d5, &d6);
            if (nscan == 7 && ioffset >= 0 && ioffset < active_sensor->num_offsets) {
              active_sensor->offsets[ioffset].position_offset_x = d1;
              active_sensor->offsets[ioffset].position_offset_y = d2;
              active_sensor->offsets[ioffset].position_offset_z = d3;
              active_sensor->offsets[ioffset].attitude_offset_heading = d4;
              active_sensor->offsets[ioffset].attitude_offset_roll = d5;
              active_sensor->offsets[ioffset].attitude_offset_pitch = d6;
            }
          }
          break;

        case option_modify_offset_positions:
          if (sensor_mode == SENSOR_MODIFY || sensor_mode == SENSOR_ADD) {
            nscan = sscanf(optarg_value, "%d/%lf/%lf/%lf", &ioffset, &d1, &d2, &d3);
            if (nscan == 4 && ioffset >= 0 && ioffset < active_sensor->num_offsets) {
              active_sensor->offsets[ioffset].position_offset_x = d1;
              active_sensor->offsets[ioffset].position_offset_y = d2;
              active_sensor->offsets[ioffset].position_offset_z = d3;
            }
          }
          break;

        case option_modify_offset_angles:
          if (sensor_mode == SENSOR_MODIFY || sensor_mode == SENSOR_ADD) {
            nscan = sscanf(optarg_value, "%d/%lf/%lf/%lf", &ioffset, &d1, &d2, &d3);
            if (nscan == 4 && ioffset >= 0 && ioffset < active_sensor->num_offsets) {
              active_sensor->offsets[ioffset].attitude_offset_heading = d1;
              active_sensor->offsets[ioffset].attitude_offset_roll = d2;
              active_sensor->offsets[ioffset].attitude_offset_pitch = d3;
            }
          }
          break;

        case option_modify_time_latency:
          if (sensor_mode == SENSOR_MODIFY || sensor_mode == SENSOR_ADD) {
            sscanf(optarg_value, "%lf", &active_sensor->time_latency_static);
            active_sensor->time_latency_mode = MB_SENSOR_TIME_LATENCY_STATIC;
          }
          break;

        case option_modify_time_latency_model:
          if (sensor_mode == SENSOR_MODIFY || sensor_mode == SENSOR_ADD) {
            /* set the name of the input time latency file */
            snprintf(time_latency_model_file, sizeof(time_latency_model_file), "%s", optarg_value);
            active_sensor->time_latency_mode = MB_SENSOR_TIME_LATENCY_MODEL;

            /* count the data points in the time latency file */
            active_sensor->num_time_latency = 0;
            if ((tfp = fopen(time_latency_model_file, "r")) == NULL) {
              fprintf(stderr, "\nUnable to open time latency model file <%s> for reading\n", time_latency_model_file);
              fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
              Return(GMT_ERROR_ON_FOPEN);
            }
            char *result;
            while ((result = fgets(buffer, MB_PATH_MAXLINE, tfp)) == buffer)
              if (buffer[0] != '#')
                active_sensor->num_time_latency++;
            rewind(tfp);

            /* allocate arrays for time latency */
            if (active_sensor->num_time_latency > active_sensor->num_time_latency_alloc) {
              status = mb_mallocd(verbose, __FILE__, __LINE__, active_sensor->num_time_latency * sizeof(double),
                                  (void **)&active_sensor->time_latency_time_d, &error);
              if (error == MB_ERROR_NO_ERROR)
                status = mb_mallocd(verbose, __FILE__, __LINE__, active_sensor->num_time_latency * sizeof(double),
                                    (void **)&active_sensor->time_latency_value, &error);
              if (error != MB_ERROR_NO_ERROR) {
                char *message;
                mb_error(verbose, error, &message);
                fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
                fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
                Return(GMT_MEMORY_ERROR);
              }
              active_sensor->num_time_latency_alloc = active_sensor->num_time_latency;
            }

            /* read the data points in the time latency file */
            active_sensor->num_time_latency = 0;
            while ((result = fgets(buffer, MB_PATH_MAXLINE, tfp)) == buffer) {
              if (buffer[0] != '#') {
                /* read the time and time latency pair */
                if (sscanf(buffer, "%lf %lf", &active_sensor->time_latency_time_d[active_sensor->num_time_latency],
                           &active_sensor->time_latency_value[active_sensor->num_time_latency]) == 2)
                  active_sensor->num_time_latency++;
              }
            }
            fclose(tfp);
          }
          break;

        case option_end_sensor:
          if (sensor_mode == SENSOR_ADD) {
            status = mb_platform_add_sensor(verbose, (void *)platform, tmp_sensor.type, tmp_sensor.model,
                                            tmp_sensor.manufacturer, tmp_sensor.serialnumber, tmp_sensor.capability1,
                                            tmp_sensor.capability2, tmp_sensor.num_offsets, tmp_sensor.num_time_latency,
                                            &error);
            for (ioffset = 0; ioffset < tmp_sensor.num_offsets; ioffset++) {
              status = mb_platform_set_sensor_offset(
                  verbose, (void *)platform, sensor_id, ioffset, 
                  tmp_offsets[ioffset].position_offset_x, tmp_offsets[ioffset].position_offset_y,
                  tmp_offsets[ioffset].position_offset_z, 
                  tmp_offsets[ioffset].attitude_offset_heading, tmp_offsets[ioffset].attitude_offset_roll,
                  tmp_offsets[ioffset].attitude_offset_pitch, &error);
            }
            status &= mb_platform_set_sensor_timelatency(
                verbose, (void *)platform, sensor_id, tmp_sensor.time_latency_mode, tmp_sensor.time_latency_static,
                tmp_sensor.num_time_latency, tmp_sensor.time_latency_time_d, tmp_sensor.time_latency_value, &error);
            if (tmp_sensor.heading_flipsign_heading)
            	status &= mb_platform_set_sensor_flipsign_heading(verbose, (void *)platform, sensor_id, &error);
            if (tmp_sensor.attitude_flipsign_roll)
            	status &= mb_platform_set_sensor_flipsign_roll(verbose, (void *)platform, sensor_id, &error);
            if (tmp_sensor.attitude_flipsign_pitch)
            	status &= mb_platform_set_sensor_flipsign_pitch(verbose, (void *)platform, sensor_id, &error);

            sensor_mode = SENSOR_OFF;
            sensor_id = -1;
          }
          break;

        case option_set_source_bathymetry:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_bathymetry = sensor_id;
          break;

        case option_set_source_bathymetry1:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_bathymetry1 = sensor_id;
          break;

        case option_set_source_bathymetry2:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_bathymetry2 = sensor_id;
          break;

        case option_set_source_bathymetry3:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_bathymetry3 = sensor_id;
          break;

        case option_set_source_backscatter:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_backscatter = sensor_id;
          break;

        case option_set_source_backscatter1:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_backscatter1 = sensor_id;
          break;

        case option_set_source_backscatter2:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_backscatter2 = sensor_id;
          break;

        case option_set_source_backscatter3:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_backscatter3 = sensor_id;
          break;

        case option_set_source_subbottom:
        nscan = sscanf(optarg_value, "%d", &sensor_id);
        if (sensor_id >= -1 && sensor_id < platform->num_sensors)
          platform->source_subbottom = sensor_id;
          break;

        case option_set_source_subbottom1:
        nscan = sscanf(optarg_value, "%d", &sensor_id);
        if (sensor_id >= -1 && sensor_id < platform->num_sensors)
          platform->source_subbottom1 = sensor_id;
          break;

        case option_set_source_subbottom2:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_subbottom2 = sensor_id;
          break;

        case option_set_source_subbottom3:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_subbottom3 = sensor_id;
          break;

        case option_set_source_camera:
        nscan = sscanf(optarg_value, "%d", &sensor_id);
        if (sensor_id >= -1 && sensor_id < platform->num_sensors)
          platform->source_camera = sensor_id;
          break;

        case option_set_source_camera1:
        nscan = sscanf(optarg_value, "%d", &sensor_id);
        if (sensor_id >= -1 && sensor_id < platform->num_sensors)
          platform->source_camera1 = sensor_id;
          break;

        case option_set_source_camera2:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_camera2 = sensor_id;
          break;

        case option_set_source_camera3:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_camera3 = sensor_id;
          break;

        case option_set_source_position:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_position = sensor_id;
          break;

        case option_set_source_position1:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_position1 = sensor_id;
          break;

        case option_set_source_position2:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_position2 = sensor_id;
          break;

        case option_set_source_position3:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_position3 = sensor_id;
          break;

        case option_set_source_depth:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_depth = sensor_id;
          break;

        case option_set_source_depth1:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_depth1 = sensor_id;
          break;

        case option_set_source_depth2:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_depth2 = sensor_id;
          break;

        case option_set_source_depth3:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_depth3 = sensor_id;
          break;

        case option_set_source_heading:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_heading = sensor_id;
          break;

        case option_set_source_heading1:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_heading1 = sensor_id;
          break;

        case option_set_source_heading2:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_heading2 = sensor_id;
          break;

        case option_set_source_heading3:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_heading3 = sensor_id;
          break;

        case option_set_source_rollpitch:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_rollpitch = sensor_id;
          break;

        case option_set_source_rollpitch1:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_rollpitch1 = sensor_id;
          break;

        case option_set_source_rollpitch2:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_rollpitch2 = sensor_id;
          break;

        case option_set_source_rollpitch3:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_rollpitch3 = sensor_id;
          break;

        case option_set_source_heave:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_heave = sensor_id;
          break;

        case option_set_source_heave1:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_heave1 = sensor_id;
          break;

        case option_set_source_heave2:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_heave2 = sensor_id;
          break;

        case option_set_source_heave3:
          nscan = sscanf(optarg_value, "%d", &sensor_id);
          if (sensor_id >= -1 && sensor_id < platform->num_sensors)
            platform->source_heave3 = sensor_id;
          break;

      }
    }

    if (sensor_mode == SENSOR_MODIFY || sensor_mode == SENSOR_ADD) {
      if (platform == NULL || sensor_id < 0 || sensor_id > platform->num_sensors) {
        sensor_id = -1;
        sensor_mode = SENSOR_OFF;
      }
    }
  }

  if (errflg)
    Return(GMT_PARSE_ERROR);

  /* if an output has been specified but there are still not sensors in the
   * platform, make a generic null platform with one sensor that is the source
   * for all data, with no offsets */
  if (output_platform_file_defined && platform != NULL && platform->num_sensors == 0) {
    status =
        mb_platform_add_sensor(verbose, (void *)platform, MB_SENSOR_TYPE_NONE, NULL, NULL, NULL,
                               (MB_SENSOR_CAPABILITY1_POSITION + MB_SENSOR_CAPABILITY1_DEPTH + MB_SENSOR_CAPABILITY1_HEAVE +
                                MB_SENSOR_CAPABILITY1_ROLLPITCH + MB_SENSOR_CAPABILITY1_HEADING),
                               MB_SENSOR_CAPABILITY2_TOPOGRAPHY_MULTIBEAM, 1, 0, &error);
    status &= mb_platform_set_sensor_offset(verbose, (void *)platform, 0, 0, 0.0, 0.0, 0.0,
                                           0.0, 0.0, 0.0, &error);
  }

  /* write out the platform file */
  if (status == MB_SUCCESS && output_platform_file_defined) {
    status = mb_platform_write(verbose, output_platform_file, (void *)platform, &error);
  }

  if (verbose > 0) {
    fprintf(stderr, "\nOutput platform file <%s>\n", output_platform_file);
    fprintf(stderr, "    platform->type:                        %d <%s>\n", platform->type,
            mb_platform_type((mb_platform_enum)(platform->type)));
    fprintf(stderr, "    platform->name:                        %s\n", platform->name);
    fprintf(stderr, "    platform->organization:                %s\n", platform->organization);
    fprintf(stderr, "    platform->documentation_url:           %s\n", platform->documentation_url);
    fprintf(stderr, "    platform->start_time_d:                %f\n", platform->start_time_d);
    fprintf(stderr, "    platform->start_time_i:                %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
            platform->start_time_i[0], platform->start_time_i[1], platform->start_time_i[2], platform->start_time_i[3],
            platform->start_time_i[4], platform->start_time_i[5], platform->start_time_i[6]);
    fprintf(stderr, "    platform->end_time_d:                  %f\n", platform->end_time_d);
    fprintf(stderr, "    platform->end_time_i:                %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n",
            platform->end_time_i[0], platform->end_time_i[1], platform->end_time_i[2], platform->end_time_i[3],
            platform->end_time_i[4], platform->end_time_i[5], platform->end_time_i[6]);
    fprintf(stderr, "    platform->source_bathymetry:           %d\n", platform->source_bathymetry);
    fprintf(stderr, "    platform->source_bathymetry1:          %d\n", platform->source_bathymetry1);
    fprintf(stderr, "    platform->source_bathymetry2:          %d\n", platform->source_bathymetry2);
    fprintf(stderr, "    platform->source_bathymetry3:          %d\n", platform->source_bathymetry3);
    fprintf(stderr, "    platform->source_backscatter:          %d\n", platform->source_backscatter);
    fprintf(stderr, "    platform->source_backscatter1:         %d\n", platform->source_backscatter1);
    fprintf(stderr, "    platform->source_backscatter2:         %d\n", platform->source_backscatter2);
    fprintf(stderr, "    platform->source_backscatter3:         %d\n", platform->source_backscatter3);
    fprintf(stderr, "    platform->source_subbottom:            %d\n", platform->source_subbottom);
    fprintf(stderr, "    platform->source_subbottom1:           %d\n", platform->source_subbottom1);
    fprintf(stderr, "    platform->source_subbottom2:           %d\n", platform->source_subbottom2);
    fprintf(stderr, "    platform->source_subbottom3:           %d\n", platform->source_subbottom3);
    fprintf(stderr, "    platform->source_camera:               %d\n", platform->source_camera);
    fprintf(stderr, "    platform->source_camera1:              %d\n", platform->source_camera1);
    fprintf(stderr, "    platform->source_camera2:              %d\n", platform->source_camera2);
    fprintf(stderr, "    platform->source_camera3:              %d\n", platform->source_camera3);
    fprintf(stderr, "    platform->source_position:             %d\n", platform->source_position);
    fprintf(stderr, "    platform->source_position1:            %d\n", platform->source_position1);
    fprintf(stderr, "    platform->source_position2:            %d\n", platform->source_position2);
    fprintf(stderr, "    platform->source_position3:            %d\n", platform->source_position3);
    fprintf(stderr, "    platform->source_depth:                %d\n", platform->source_depth);
    fprintf(stderr, "    platform->source_depth1:               %d\n", platform->source_depth1);
    fprintf(stderr, "    platform->source_depth2:               %d\n", platform->source_depth2);
    fprintf(stderr, "    platform->source_depth3:               %d\n", platform->source_depth3);
    fprintf(stderr, "    platform->source_heading:              %d\n", platform->source_heading);
    fprintf(stderr, "    platform->source_heading1:             %d\n", platform->source_heading1);
    fprintf(stderr, "    platform->source_heading2:             %d\n", platform->source_heading2);
    fprintf(stderr, "    platform->source_heading3:             %d\n", platform->source_heading3);
    fprintf(stderr, "    platform->source_rollpitch:            %d\n", platform->source_rollpitch);
    fprintf(stderr, "    platform->source_rollpitch1:           %d\n", platform->source_rollpitch1);
    fprintf(stderr, "    platform->source_rollpitch2:           %d\n", platform->source_rollpitch2);
    fprintf(stderr, "    platform->source_rollpitch3:           %d\n", platform->source_rollpitch3);
    fprintf(stderr, "    platform->source_heave:                %d\n", platform->source_heave);
    fprintf(stderr, "    platform->source_heave1:               %d\n", platform->source_heave1);
    fprintf(stderr, "    platform->source_heave2:               %d\n", platform->source_heave2);
    fprintf(stderr, "    platform->source_heave3:               %d\n", platform->source_heave3);
    fprintf(stderr, "    platform->num_sensors:                 %d\n", platform->num_sensors);
    for (int i = 0; i < platform->num_sensors; i++) {
      int index = 0;
      for (int j = 0; j < NUM_MB_SENSOR_TYPES; j++)
        if (mb_sensor_type_id[j] == platform->sensors[i].type)
          index = j;
      fprintf(stderr, "    platform->sensors[%d].type:                 %d <%s>\n", i, platform->sensors[i].type,
              mb_sensor_type_string[index]);
      fprintf(stderr, "    platform->sensors[%d].model:                %s\n", i, platform->sensors[i].model);
      fprintf(stderr, "    platform->sensors[%d].manufacturer:         %s\n", i, platform->sensors[i].manufacturer);
      fprintf(stderr, "    platform->sensors[%d].serialnumber:         %s\n", i, platform->sensors[i].serialnumber);
      fprintf(stderr, "    platform->sensors[%d].capability1:          %d\n", i, platform->sensors[i].capability1);
      fprintf(stderr, "    platform->sensors[%d].capability2:          %d\n", i, platform->sensors[i].capability2);
      fprintf(stderr, "    platform->sensors[%d].num_offsets:          %d\n", i, platform->sensors[i].num_offsets);
      for (int j = 0; j < platform->sensors[i].num_offsets; j++) {
        fprintf(stderr, "    platform->sensors[%d].offsets[%d].position_offset_x:          %f\n", i, j,
                platform->sensors[i].offsets[j].position_offset_x);
        fprintf(stderr, "    platform->sensors[%d].offsets[%d].position_offset_y:          %f\n", i, j,
                platform->sensors[i].offsets[j].position_offset_y);
        fprintf(stderr, "    platform->sensors[%d].offsets[%d].position_offset_z:          %f\n", i, j,
                platform->sensors[i].offsets[j].position_offset_z);
        fprintf(stderr, "    platform->sensors[%d].offsets[%d].attitude_offset_heading:    %f\n", i, j,
                platform->sensors[i].offsets[j].attitude_offset_heading);
        fprintf(stderr, "    platform->sensors[%d].offsets[%d].attitude_offset_roll:       %f\n", i, j,
                platform->sensors[i].offsets[j].attitude_offset_roll);
        fprintf(stderr, "    platform->sensors[%d].offsets[%d].attitude_offset_pitch:      %f\n", i, j,
                platform->sensors[i].offsets[j].attitude_offset_pitch);
      }
      fprintf(stderr, "    platform->sensors[%d].time_latency_mode:    %d\n", i, platform->sensors[i].time_latency_mode);
      fprintf(stderr, "    platform->sensors[%d].time_latency_static:  %f\n", i, platform->sensors[i].time_latency_static);
      fprintf(stderr, "    platform->sensors[%d].num_time_latency:     %d\n", i, platform->sensors[i].num_time_latency);
      for (int j = 0; j < platform->sensors[i].num_time_latency; j++) {
        fprintf(stderr, "    platform->sensors[%d].time_latency[%d]:                       %16.6f %8.6f\n", i, j,
                platform->sensors[i].time_latency_time_d[j], platform->sensors[i].time_latency_value[j]);
      }
      fprintf(stderr, "    platform->sensors[%d].heading_flipsign_heading:  %d\n", i, platform->sensors[i].heading_flipsign_heading);
      fprintf(stderr, "    platform->sensors[%d].attitude_flipsign_roll:    %d\n", i, platform->sensors[i].attitude_flipsign_roll);
      fprintf(stderr, "    platform->sensors[%d].attitude_flipsign_pitch:   %d\n", i, platform->sensors[i].attitude_flipsign_pitch);
    }
  }

  /* deallocate platform structure */
  if (platform != NULL) {
    status = mb_platform_deall(verbose, (void **)&platform, &error);
  }

  if (verbose >= 4)
    status = mb_memory_list(verbose, &error);

  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
    fprintf(stderr, "dbg2  Ending status:\n");
    fprintf(stderr, "dbg2       status:  %d\n", status);
  }

  /* The program exits with MBIO's error; as a module that is a GMT error code, never an MBIO one.
     The end of the data (EOF) is how every read finishes, not an error. */
  if (error > MB_ERROR_NO_ERROR && error != MB_ERROR_EOF) {
    char *message;
    mb_error(verbose, error, &message);
    GMT_Report(API, GMT_MSG_ERROR, "%s\n", message);
    Return(GMT_RUNTIME_ERROR);
  }
  Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
