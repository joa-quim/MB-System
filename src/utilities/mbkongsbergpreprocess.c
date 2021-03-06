/*--------------------------------------------------------------------
 *    The MB-system:	mbkongsbergpreprocess.c	1/1/2012
 *
 *    Copyright (c) 2012-2019 by
 *    David W. Caress (caress@mbari.org)
 *      Monterey Bay Aquarium Research Institute
 *      Moss Landing, CA 95039
 *    and Dale N. Chayes (dale@ldeo.columbia.edu)
 *      Lamont-Doherty Earth Observatory
 *      Palisades, NY 10964
 *
 *    See README file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * mbkongsbergpreprocess reads a HYSWEEP HSX format file, interpolates the
 * asynchronous navigation and attitude onto the multibeam data,
 * and writes a new HSX file with that information correctly embedded
 * in the multibeam data. This program can also fix various problems
 * with the data, including sensor offsets.
 *
 * Author:	D. W. Caress
 * Date:	June 1, 2012
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mb_aux.h"
#include "mb_define.h"
#include "mb_format.h"
#include "mb_io.h"
#include "mb_status.h"
#include "mbsys_simrad3.h"

#define MBKONSBERGPREPROCESS_ALLOC_CHUNK 1000
#define MBKONSBERGPREPROCESS_PROCESS 1
#define MBKONSBERGPREPROCESS_TIMESTAMPLIST 2
#define MBKONSBERGPREPROCESS_TIMELAG_OFF 0
#define MBKONSBERGPREPROCESS_TIMELAG_CONSTANT 1
#define MBKONSBERGPREPROCESS_TIMELAG_MODEL 2

#define MBKONSBERGPREPROCESS_SONAR_OFFSET_NONE 0
#define MBKONSBERGPREPROCESS_SONAR_OFFSET_SONAR 1
#define MBKONSBERGPREPROCESS_SONAR_OFFSET_MRU 2
#define MBKONSBERGPREPROCESS_SONAR_OFFSET_NAVIGATION 3

#define MBKONSBERGPREPROCESS_OFFSET_MAX 12

#define MBKONSBERGPREPROCESS_NAVFORMAT_NONE 0
#define MBKONSBERGPREPROCESS_NAVFORMAT_OFG 1

/* set precision of iterative raytracing depth & distance matching */
#define MBKONSBERGPREPROCESS_BATH_RECALC_PRECISION 0.0001
#define MBKONSBERGPREPROCESS_BATH_RECALC_NCALCMAX 50
#define MBKONSBERGPREPROCESS_BATH_RECALC_ANGLEMODE 0

#define MBKONSBERGPREPROCESS_ZMODE_UNKNOWN 0
#define MBKONSBERGPREPROCESS_ZMODE_USE_HEAVE_ONLY 1
#define MBKONSBERGPREPROCESS_ZMODE_USE_SENSORDEPTH_ONLY 2
#define MBKONSBERGPREPROCESS_ZMODE_USE_SENSORDEPTH_AND_HEAVE 3

#define MBKONSBERGPREPROCESS_WATERCOLUMN_IGNORE 0
#define MBKONSBERGPREPROCESS_WATERCOLUMN_OUTPUT 1

#define MBKONSBERGPREPROCESS_FILTER_NONE 0
#define MBKONSBERGPREPROCESS_FILTER_MEAN 1
#define MBKONSBERGPREPROCESS_FILTER_MEDIAN 2


/*--------------------------------------------------------------------*/

int main(int argc, char **argv) {
	char program_name[] = "mbkongsbergpreprocess";
	char help_message[] = "mbkongsbergpreprocess reads a Kongsberg multibeam vendor format file (or datalist of "
	                      "files),\ninterpolates the asynchronous navigation and attitude onto the multibeam data, \nand writes "
	                      "the data as one or more format 59 files.";
	char usage_message[] = "mbkongsbergpreprocess [-C -Doutputdirectory -Eoffx/offy[/offdepth] -Fformat -Ifile -Ooutfile "
	                       "\n\t\t\t-Pfilterlength/filterdepth -Sdatatype/source -Ttimelag -W -H -V]";

	extern char *optarg;
	int errflg = 0;
	int c;
	int help = 0;
	int flag = 0;

	/* MBIO status variables */
	int status = MB_SUCCESS;
	int verbose = 0;
	int error = MB_ERROR_NO_ERROR;
	char *message;

	/* MBIO read control parameters */
	int read_datalist = MB_NO;
	char read_file[MB_PATH_MAXLINE] = "";
	void *datalist;
	int look_processed = MB_DATALIST_LOOK_UNSET;
	double file_weight;
	int format = 0;
	int pings;
	int lonflip;
	double bounds[4];
	int btime_i[7];
	int etime_i[7];
	double btime_d;
	double etime_d;
	double speedmin;
	double timegap;
	char ifile[MB_PATH_MAXLINE] = "";
	char dfile[MB_PATH_MAXLINE] = "";
	char ofile[MB_PATH_MAXLINE] = "";
	int ofile_set = MB_NO;
	char odir[MB_PATH_MAXLINE] = "";
	int odir_set = MB_NO;
	int beams_bath;
	int beams_amp;
	int pixels_ss;
	int obeams_bath;
	int obeams_amp;
	int opixels_ss;

	/* MBIO read values */
	struct mb_io_struct *imb_io_ptr = NULL;
	struct mbsys_simrad3_struct *istore = NULL;
	struct mbsys_simrad3_ping_struct *ping = NULL;
	struct mbsys_simrad3_attitude_struct *attitude = NULL;
	struct mbsys_simrad3_netattitude_struct *netattitude = NULL;
	struct mbsys_simrad3_heading_struct *headingr = NULL;
	void *imbio_ptr = NULL;
	void *istore_ptr = NULL;
	void *ombio_ptr = NULL;
	int kind;
	int time_i[7];
	double time_d;
	double navlon;
	double navlat;
	double speed;
	double heading;
	double distance;
	double altitude;
	double sonardepth;
	double roll;
	double pitch;
	double heave;
	char *beamflag = NULL;
	double *bath = NULL;
	double *bathacrosstrack = NULL;
	double *bathalongtrack = NULL;
	double *amp = NULL;
	double *ss = NULL;
	double *ssacrosstrack = NULL;
	double *ssalongtrack = NULL;
	char comment[MB_COMMENT_MAXLINE];

	/* program mode */
	int mode = MBKONSBERGPREPROCESS_PROCESS;
	int nav_source = MB_DATA_NAV;
	int attitude_source = MB_DATA_NONE; // usually MB_DATA_ATTITUDE but let this be set by active sensor
	int heading_source = MB_DATA_NAV;
	int sonardepth_source = MB_DATA_DATA;

	/* counting variables file */
	int output_counts = MB_NO;
	int nfile_read = 0;
	int nfile_write = 0;
	int nrec_0x30_pu_id = 0;
	int nrec_0x31_pu_status = 0;
	int nrec_0x32_pu_bist = 0;
	int nrec_0x33_parameter_extra = 0;
	int nrec_0x41_attitude = 0;
	int nrec_0x43_clock = 0;
	int nrec_0x44_bathymetry = 0;
	int nrec_0x45_singlebeam = 0;
	int nrec_0x46_rawbeamF = 0;
	int nrec_0x47_surfacesoundspeed2 = 0;
	int nrec_0x48_heading = 0;
	int nrec_0x49_parameter_start = 0;
	int nrec_0x4A_tilt = 0;
	int nrec_0x4B_echogram = 0;
	int nrec_0x4E_rawbeamN = 0;
	int nrec_0x4F_quality = 0;
	int nrec_0x50_pos = 0;
	int nrec_0x52_runtime = 0;
	int nrec_0x53_sidescan = 0;
	int nrec_0x54_tide = 0;
	int nrec_0x55_svp2 = 0;
	int nrec_0x56_svp = 0;
	int nrec_0x57_surfacesoundspeed = 0;
	int nrec_0x58_bathymetry2 = 0;
	int nrec_0x59_sidescan2 = 0;
	int nrec_0x66_rawbeamf = 0;
	int nrec_0x68_height = 0;
	int nrec_0x69_parameter_stop = 0;
	int nrec_0x6B_water_column = 0;
	int nrec_0x6E_network_attitude = 0;
	int nrec_0x70_parameter = 0;
	int nrec_0x73_surface_sound_speed = 0;
	int nrec_0xE1_bathymetry_mbari57 = 0;
	int nrec_0xE2_sidescan_mbari57 = 0;
	int nrec_0xE3_bathymetry_mbari59 = 0;
	int nrec_0xE4_sidescan_mbari59 = 0;
	int nrec_0xE5_bathymetry_mbari59 = 0;

	/* counting variables total */
	int nrec_0x30_pu_id_tot = 0;
	int nrec_0x31_pu_status_tot = 0;
	int nrec_0x32_pu_bist_tot = 0;
	int nrec_0x33_parameter_extra_tot = 0;
	int nrec_0x41_attitude_tot = 0;
	int nrec_0x43_clock_tot = 0;
	int nrec_0x44_bathymetry_tot = 0;
	int nrec_0x45_singlebeam_tot = 0;
	int nrec_0x46_rawbeamF_tot = 0;
	int nrec_0x47_surfacesoundspeed2_tot = 0;
	int nrec_0x48_heading_tot = 0;
	int nrec_0x49_parameter_start_tot = 0;
	int nrec_0x4A_tilt_tot = 0;
	int nrec_0x4B_echogram_tot = 0;
	int nrec_0x4E_rawbeamN_tot = 0;
	int nrec_0x4F_quality_tot = 0;
	int nrec_0x50_pos_tot = 0;
	int nrec_0x52_runtime_tot = 0;
	int nrec_0x53_sidescan_tot = 0;
	int nrec_0x54_tide_tot = 0;
	int nrec_0x55_svp2_tot = 0;
	int nrec_0x56_svp_tot = 0;
	int nrec_0x57_surfacesoundspeed_tot = 0;
	int nrec_0x58_bathymetry2_tot = 0;
	int nrec_0x59_sidescan2_tot = 0;
	int nrec_0x66_rawbeamf_tot = 0;
	int nrec_0x68_height_tot = 0;
	int nrec_0x69_parameter_stop_tot = 0;
	int nrec_0x6B_water_column_tot = 0;
	int nrec_0x6E_network_attitude_tot = 0;
	int nrec_0x70_parameter_tot = 0;
	int nrec_0x73_surface_sound_speed_tot = 0;
	int nrec_0xE1_bathymetry_mbari57_tot = 0;
	int nrec_0xE2_sidescan_mbari57_tot = 0;
	int nrec_0xE3_bathymetry_mbari59_tot = 0;
	int nrec_0xE4_sidescan_mbari59_tot = 0;
	int nrec_0xE5_bathymetry_mbari59_tot = 0;

	/* merge sonardepth from separate parosci pressure sensor data file */
	char sonardepthfile[MB_PATH_MAXLINE] = "";
	int sonardepthdata = MB_NO;
	int nsonardepth = 0;
	double *sonardepth_time_d = NULL;
	double *sonardepth_sonardepth = NULL;
	double *sonardepth_sonardepthfilter = NULL;

	/* asynchronous navigation, heading, attitude data */
	int ndat_nav = 0;
	int ndat_nav_alloc = 0;
	double *dat_nav_time_d = NULL;
	double *dat_nav_lon = NULL;
	double *dat_nav_lat = NULL;

	int ndat_sonardepth = 0;
	int ndat_sonardepth_alloc = 0;
	double *dat_sonardepth_time_d = NULL;
	double *dat_sonardepth_sonardepth = NULL;
	double *dat_sonardepth_sonardepthfilter = NULL;

	int ndat_heading = 0;
	int ndat_heading_alloc = 0;
	double *dat_heading_time_d = NULL;
	double *dat_heading_heading = NULL;

	int ndat_rph = 0;
	int ndat_rph_alloc = 0;
	double *dat_rph_time_d = NULL;
	double *dat_rph_roll = NULL;
	double *dat_rph_pitch = NULL;
	double *dat_rph_heave = NULL;

	/* timelag parameters */
	int timelagmode = MBKONSBERGPREPROCESS_TIMELAG_OFF;
	double timelag = 0.0;
	double timelagconstant = 0.0;
	char timelagfile[MB_PATH_MAXLINE] = "";
	int ntimelag = 0;
	double *timelag_time_d = NULL;
	double *timelag_model = NULL;

	/* depth sensor filtering */
	int sonardepthfilter = MBKONSBERGPREPROCESS_FILTER_NONE;
	double sonardepthfilterlength = 20.0;
	double sonardepthfilterdepth = 20.0;

	/* depth sensor offset and lever arm parameters */
	int sonardepthlever = MB_NO;
	double sonardepthoffset = 0.0; /* depth sensor offset (+ makes vehicle deeper) */
	double depthsensoroffx = 0.0;
	double depthsensoroffy = 0.0;
	double depthsensoroffz = 0.0;

	/* output asynchronous and synchronous time series ancillary files */
	char athfile[MB_PATH_MAXLINE] = "";
	char atsfile[MB_PATH_MAXLINE] = "";
	char atafile[MB_PATH_MAXLINE] = "";
	char stafile[MB_PATH_MAXLINE] = "";
	FILE *athfp;
	FILE *atsfp;
	FILE *atafp;
	FILE *stafp;

	/* handling water column records */
	int watercolumnmode = MBKONSBERGPREPROCESS_WATERCOLUMN_IGNORE;

	/* processing kluge modes */
	int klugemode;

	int interp_status;
	FILE *tfp = NULL;
	struct stat file_status;
	int fstat;
	char buffer[MB_PATH_MAXLINE] = "";
	char *result;
	int read_data;
	char fileroot[MB_PATH_MAXLINE] = "";
	char *filenameptr;
	int testformat;
	int type, source;
	double start_time_d, end_time_d;

	double transmit_time_d, transmit_heading, transmit_heave, transmit_roll, transmit_pitch;
	double receive_time_d, receive_heading, receive_heave, receive_roll, receive_pitch;

	/* transmit and receive array offsets */
	double tx_x, tx_y, tx_z, tx_h, tx_r, tx_p;
	double rx_x, rx_y, rx_z, rx_h, rx_r, rx_p;

	/* depth sensor offsets - used in place of heave for underwater platforms */
	int depthsensor_mode = MBKONSBERGPREPROCESS_ZMODE_UNKNOWN;
	double depth_off_x, depth_off_y, depth_off_z;

	/* roll and pitch sensor offsets */
	double rollpitch_off_x, rollpitch_off_y, rollpitch_off_z, rollpitch_off_h, rollpitch_off_r, rollpitch_off_p;

	/* heave sensor offsets */
	double heave_off_x, heave_off_y, heave_off_z, heave_off_h, heave_off_r, heave_off_p;

	/* heading sensor offset */
	double heading_off_x, heading_off_y, heading_off_z, heading_off_h, heading_off_r, heading_off_p;

	/* position sensor offsets */
	double position_off_x, position_off_y, position_off_z;

	/* variables for beam angle calculation */
	mb_3D_orientation tx_align;
	mb_3D_orientation tx_orientation;
	double tx_steer;
	mb_3D_orientation rx_align;
	mb_3D_orientation rx_orientation;
	double rx_steer;
	double reference_heading;
	double beamAzimuth;
	double beamDepression;
	double *pixel_size, *swath_width;
	mb_u_char detection_mask;

	int jtimelag = 0;
	int jnav = 0;
	int jheading = 0;
	int jattitude = 0;
	int jsonardepth = 0;
	int nhalffilter;
	double sonardepth_filterweight;
	double dtime, dtol, weight;
	double factor;
	double *median = NULL;
	int nmedian = 0;
	int nmedian_alloc = 0;

	int nscan;
	int i, j, j1, j2;

	/* get current default values */
	status = mb_defaults(verbose, &format, &pings, &lonflip, bounds, btime_i, etime_i, &speedmin, &timegap);

	/* set default input to datalist.mb-1 */
	strcpy(read_file, "datalist.mb-1");

	/* set default nav and attitude sources */
	nav_source = MB_DATA_NAV;
	attitude_source = MB_DATA_NONE; // usually MB_DATA_ATTITUDE but let this be set by active sensor
	heading_source = MB_DATA_NAV;
	sonardepth_source = MB_DATA_DATA;

	/* process argument list */
	while ((c = getopt(argc, argv, "CcD:d:E:e:F:f:I:i:K:k:O:o:P:p:S:s:T:t:VvHh")) != -1)
		switch (c) {
		case 'H':
		case 'h':
			help++;
			break;
		case 'V':
		case 'v':
			verbose++;
			break;
		case 'C':
		case 'c':
			output_counts = MB_YES;
			flag++;
			break;
		case 'D':
		case 'd':
			sscanf(optarg, "%s", odir);
			odir_set = MB_YES;
			flag++;
			break;
		case 'E':
		case 'e':
			nscan = sscanf(optarg, "%lf/%lf/%lf/%lf", &depthsensoroffx, &depthsensoroffy, &depthsensoroffz, &sonardepthoffset);
			if (nscan < 4) {
				if (nscan == 3) {
					sonardepthoffset = depthsensoroffz;
					depthsensoroffz = depthsensoroffy;
					depthsensoroffy = depthsensoroffx;
					depthsensoroffx = 0.0;
				}
				else if (nscan == 2) {
					sonardepthoffset = 0.0;
					depthsensoroffz = depthsensoroffy;
					depthsensoroffy = depthsensoroffx;
					depthsensoroffx = 0.0;
				}
				else if (nscan == 1) {
					sonardepthoffset = 0.0;
					depthsensoroffz = 0.0;
					depthsensoroffy = depthsensoroffx;
					depthsensoroffx = 0.0;
				}
			}
			if (nscan > 0)
				sonardepthlever = MB_YES;
			flag++;
			break;
		case 'F':
		case 'f':
			sscanf(optarg, "%d", &format);
			flag++;
			break;
		case 'I':
		case 'i':
			sscanf(optarg, "%s", read_file);
			flag++;
			break;
		case 'K':
		case 'k':
			sscanf(optarg, "%d", &klugemode);
			flag++;
			break;
		case 'O':
		case 'o':
			sscanf(optarg, "%s", ofile);
			ofile_set = MB_YES;
			flag++;
			break;
		case 'P':
		case 'p':
			sscanf(optarg, "%s", buffer);
			if ((fstat = stat(buffer, &file_status)) == 0 && (file_status.st_mode & S_IFMT) != S_IFDIR) {
				sonardepthdata = MB_YES;
				strcpy(sonardepthfile, buffer);
			}
			else if (optarg[0] == 'F' || optarg[0] == 'f') {
				nscan = sscanf(&(optarg[1]), "%lf/%lf", &sonardepthfilterlength, &sonardepthfilterdepth);
				if (nscan == 1)
					sonardepthfilterdepth = 20.0;
				if (nscan >= 1)
					sonardepthfilter = MBKONSBERGPREPROCESS_FILTER_MEAN;
				else
					sonardepthfilter = MBKONSBERGPREPROCESS_FILTER_NONE;
			}
			else if (optarg[0] == 'M' || optarg[0] == 'm') {
				nscan = sscanf(&(optarg[1]), "%lf/%lf", &sonardepthfilterlength, &sonardepthfilterdepth);
				if (nscan == 1)
					sonardepthfilterdepth = 20.0;
				if (nscan >= 1)
					sonardepthfilter = MBKONSBERGPREPROCESS_FILTER_MEDIAN;
				else
					sonardepthfilter = MBKONSBERGPREPROCESS_FILTER_NONE;
			}
			else if (optarg[0] == 'U' || optarg[0] == 'u') {
				nscan = sscanf(&(optarg[1]), "%d", &depthsensor_mode);
			}
			flag++;
			break;
		case 'S':
		case 's':
			sscanf(optarg, "%d/%d", &type, &source);
			if (type == 1)
				nav_source = source;
			else if (type == 2)
				heading_source = source;
			else if (type == 3)
				attitude_source = source;
			else if (type == 4)
				sonardepth_source = source;
			flag++;
			break;
		case 'T':
		case 't':
			sscanf(optarg, "%s", timelagfile);
			if ((fstat = stat(timelagfile, &file_status)) == 0 && (file_status.st_mode & S_IFMT) != S_IFDIR) {
				timelagmode = MBKONSBERGPREPROCESS_TIMELAG_MODEL;
			}
			else {
				sscanf(optarg, "%lf", &timelagconstant);
				timelagmode = MBKONSBERGPREPROCESS_TIMELAG_CONSTANT;
			}
			flag++;
			break;
		case 'W':
		case 'w':
			sscanf(optarg, "%d", &watercolumnmode);
			flag++;
			break;
		case '?':
			errflg++;
		}

	/* if error flagged then print it and exit */
	if (errflg) {
		fprintf(stderr, "usage: %s\n", usage_message);
		fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
		error = MB_ERROR_BAD_USAGE;
		exit(error);
	}

	/* print starting message */
	if (verbose == 1 || help) {
		fprintf(stderr, "\nProgram %s\n", program_name);
		fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
	}

	/* print starting debug statements */
	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
		fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
		fprintf(stderr, "dbg2  Control Parameters:\n");
		fprintf(stderr, "dbg2       verbose:             %d\n", verbose);
		fprintf(stderr, "dbg2       help:                %d\n", help);
		fprintf(stderr, "dbg2       format:              %d\n", format);
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
		fprintf(stderr, "dbg2       read_file:           %s\n", read_file);
		fprintf(stderr, "dbg2       ofile:               %s\n", ofile);
		fprintf(stderr, "dbg2       ofile_set:           %d\n", ofile_set);
		fprintf(stderr, "dbg2       odir:               %s\n", odir);
		fprintf(stderr, "dbg2       odir_set:           %d\n", odir_set);
		if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_MODEL) {
			fprintf(stderr, "dbg2       timelagfile:         %s\n", timelagfile);
			fprintf(stderr, "dbg2       ntimelag:            %d\n", ntimelag);
		}
		else {
			fprintf(stderr, "dbg2       timelag:             %f\n", timelag);
		}
		fprintf(stderr, "dbg2       timelag:                %f\n", timelag);
		fprintf(stderr, "dbg2       watercolumnmode:        %d\n", watercolumnmode);
		fprintf(stderr, "dbg2       sonardepthfilter:       %d\n", sonardepthfilter);
		fprintf(stderr, "dbg2       sonardepthfilterlength: %f\n", sonardepthfilterlength);
		fprintf(stderr, "dbg2       sonardepthfilterdepth:  %f\n", sonardepthfilterdepth);
		fprintf(stderr, "dbg2       sonardepthfile:         %s\n", sonardepthfile);
		fprintf(stderr, "dbg2       sonardepthdata:         %d\n", sonardepthdata);
		fprintf(stderr, "dbg2       sonardepthlever:        %d\n", sonardepthlever);
		fprintf(stderr, "dbg2       sonardepthoffset:       %f\n", sonardepthoffset);
		fprintf(stderr, "dbg2       depthsensoroffx:        %f\n", depthsensoroffx);
		fprintf(stderr, "dbg2       depthsensoroffy:        %f\n", depthsensoroffy);
		fprintf(stderr, "dbg2       depthsensoroffz:        %f\n", depthsensoroffz);
	}

	/* if help desired then print it and exit */
	if (help) {
		fprintf(stderr, "\n%s\n", help_message);
		fprintf(stderr, "\nusage: %s\n", usage_message);
		exit(error);
	}

	/* read sonardepth data from file if specified */
	if (sonardepthdata == MB_YES) {
		/* count the data points in the auv log file */
		if ((tfp = fopen(sonardepthfile, "r")) == NULL) {
			error = MB_ERROR_OPEN_FAIL;
			fprintf(stderr, "\nUnable to open sonardepth data file <%s> for reading\n", sonardepthfile);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			exit(error);
		}

		/* count the data records */
		nsonardepth = 0;
		while ((result = fgets(buffer, MB_PATH_MAXLINE, tfp)) == buffer)
			if (buffer[0] != '#')
				nsonardepth++;
		rewind(tfp);

		/* allocate arrays for sonardepth data */
		if (nsonardepth > 0) {
			status = mb_mallocd(verbose, __FILE__, __LINE__, nsonardepth * sizeof(double), (void **)&sonardepth_time_d, &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_mallocd(verbose, __FILE__, __LINE__, nsonardepth * sizeof(double), (void **)&sonardepth_sonardepth,
				                    &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_mallocd(verbose, __FILE__, __LINE__, nsonardepth * sizeof(double),
				                    (void **)&sonardepth_sonardepthfilter, &error);
			if (error != MB_ERROR_NO_ERROR) {
				mb_error(verbose, error, &message);
				fprintf(stderr, "\nMBIO Error allocating sonardepth data arrays:\n%s\n", message);
				fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
				exit(error);
			}
		}

		/* if no sonardepth data then quit */
		else {
			error = MB_ERROR_BAD_DATA;
			fprintf(stderr, "\nUnable to read data from MBARI AUV sonardepth file <%s>\n", sonardepthfile);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			exit(error);
		}

		/* read the data points in the file */
		nsonardepth = 0;
		while ((result = fgets(buffer, MB_PATH_MAXLINE, tfp)) == buffer) {
			if (buffer[0] != '#') {
				/* read the time and sonardepth pair */
				if (sscanf(buffer, "%lf %lf", &(sonardepth_time_d[nsonardepth]), &(sonardepth_sonardepth[nsonardepth])) == 2) {
					/*fprintf(stderr,"SONARDEPTH DATA: %f %f\n",
					sonardepth_time_d[nsonardepth],
					sonardepth_sonardepth[nsonardepth]);*/
					// sonardepth_sonardepth[nsonardepth] += sonardepthoffset;
					nsonardepth++;
				}
			}
		}
		fclose(tfp);

		/* output info */
		if (nsonardepth > 0) {
			mb_get_date(verbose, sonardepth_time_d[0], btime_i);
			mb_get_date(verbose, sonardepth_time_d[nsonardepth - 1], etime_i);
			fprintf(stderr,
			        "%d sonardepth records read from %s  Start:%4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d  End:%4.4d/%2.2d/%2.2d "
			        "%2.2d:%2.2d:%2.2d.%6.6d\n",
			        nsonardepth, sonardepthfile, btime_i[0], btime_i[1], btime_i[2], btime_i[3], btime_i[4], btime_i[5],
			        btime_i[6], etime_i[0], etime_i[1], etime_i[2], etime_i[3], etime_i[4], etime_i[5], etime_i[6]);
		}
		else
			fprintf(stderr, "No sonardepth data read from %s....\n", sonardepthfile);
	}

	/* get time lag model if specified */
	if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_MODEL) {
		/* count the data points in the timelag file */
		ntimelag = 0;
		if ((tfp = fopen(timelagfile, "r")) == NULL) {
			error = MB_ERROR_OPEN_FAIL;
			fprintf(stderr, "\nUnable to open time lag model File <%s> for reading\n", timelagfile);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			exit(error);
		}
		while ((result = fgets(buffer, MB_PATH_MAXLINE, tfp)) == buffer)
			if (buffer[0] != '#')
				ntimelag++;
		rewind(tfp);

		/* allocate arrays for time lag */
		if (ntimelag > 0) {
			status = mb_mallocd(verbose, __FILE__, __LINE__, ntimelag * sizeof(double), (void **)&timelag_time_d, &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_mallocd(verbose, __FILE__, __LINE__, ntimelag * sizeof(double), (void **)&timelag_model, &error);
			if (error != MB_ERROR_NO_ERROR) {
				mb_error(verbose, error, &message);
				fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
				fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
				exit(error);
			}
		}

		/* if no time lag data then quit */
		else {
			error = MB_ERROR_BAD_DATA;
			fprintf(stderr, "\nUnable to read data from time lag model file <%s>\n", timelagfile);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			exit(error);
		}

		/* read the data points in the timelag file */
		ntimelag = 0;
		while ((result = fgets(buffer, MB_PATH_MAXLINE, tfp)) == buffer) {
			if (buffer[0] != '#') {
				/* read the time and time lag pair */
				if (sscanf(buffer, "%lf %lf", &timelag_time_d[ntimelag], &timelag_model[ntimelag]) == 2)
					ntimelag++;
			}
		}
		fclose(tfp);

		/* output info */
		if (ntimelag > 0) {
			mb_get_date(verbose, timelag_time_d[0], btime_i);
			mb_get_date(verbose, timelag_time_d[ntimelag - 1], etime_i);
			fprintf(stderr,
			        "%d timelag records read from %s  Start:%4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d  End:%4.4d/%2.2d/%2.2d "
			        "%2.2d:%2.2d:%2.2d.%6.6d\n",
			        ntimelag, timelagfile, btime_i[0], btime_i[1], btime_i[2], btime_i[3], btime_i[4], btime_i[5], btime_i[6],
			        etime_i[0], etime_i[1], etime_i[2], etime_i[3], etime_i[4], etime_i[5], etime_i[6]);
		}
		else
			fprintf(stderr, "No timelag data read from %s....\n", timelagfile);
	}

	/* get format if required */
	if (format == 0)
		mb_get_format(verbose, read_file, NULL, &format, &error);

	/* determine whether to read one file or a list of files */
	if (format < 0)
		read_datalist = MB_YES;

	/* open file list */
	if (read_datalist == MB_YES) {
		if ((status = mb_datalist_open(verbose, &datalist, read_file, look_processed, &error)) != MB_SUCCESS) {
			error = MB_ERROR_OPEN_FAIL;
			fprintf(stderr, "\nUnable to open data list file: %s\n", read_file);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			exit(error);
		}
		if ((status = mb_datalist_read(verbose, datalist, ifile, dfile, &format, &file_weight, &error)) == MB_SUCCESS)
			read_data = MB_YES;
		else
			read_data = MB_NO;
	}
	/* else copy single filename to be read */
	else {
		strcpy(ifile, read_file);
		read_data = MB_YES;
	}

	/* loop over all files to be read */
	while (read_data == MB_YES &&
	       (format == MBF_EM300RAW || format == MBF_EM300MBA || format == MBF_EM710RAW || format == MBF_EM710MBA)) {
		/* initialize reading the swath file */
		if ((status = mb_read_init(verbose, ifile, format, pings, lonflip, bounds, btime_i, etime_i, speedmin, timegap,
		                           &imbio_ptr, &btime_d, &etime_d, &beams_bath, &beams_amp, &pixels_ss, &error)) != MB_SUCCESS) {
			mb_error(verbose, error, &message);
			fprintf(stderr, "\nMBIO Error returned from function <mb_read_init>:\n%s\n", message);
			fprintf(stderr, "\nMultibeam File <%s> not initialized for reading\n", ifile);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			exit(error);
		}

		/* get pointers to data storage */
		imb_io_ptr = (struct mb_io_struct *)imbio_ptr;
		istore_ptr = imb_io_ptr->store_data;
		istore = (struct mbsys_simrad3_struct *)istore_ptr;

		if (error == MB_ERROR_NO_ERROR) {
			beamflag = NULL;
			bath = NULL;
			amp = NULL;
			bathacrosstrack = NULL;
			bathalongtrack = NULL;
			ss = NULL;
			ssacrosstrack = NULL;
			ssalongtrack = NULL;
		}
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(char), (void **)&beamflag, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bath, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_AMPLITUDE, sizeof(double), (void **)&amp, &error);
		if (error == MB_ERROR_NO_ERROR)
			status =
			    mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathacrosstrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status =
			    mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathalongtrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ss, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssacrosstrack, &error);
		if (error == MB_ERROR_NO_ERROR)
			status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssalongtrack, &error);

		/* if error initializing memory then quit */
		if (error != MB_ERROR_NO_ERROR) {
			mb_error(verbose, error, &message);
			fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
			fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
			exit(error);
		}

		/* reset file record counters */
		nrec_0x30_pu_id = 0;
		nrec_0x31_pu_status = 0;
		nrec_0x32_pu_bist = 0;
		nrec_0x33_parameter_extra = 0;
		nrec_0x41_attitude = 0;
		nrec_0x43_clock = 0;
		nrec_0x44_bathymetry = 0;
		nrec_0x45_singlebeam = 0;
		nrec_0x46_rawbeamF = 0;
		nrec_0x47_surfacesoundspeed2 = 0;
		nrec_0x48_heading = 0;
		nrec_0x49_parameter_start = 0;
		nrec_0x4A_tilt = 0;
		nrec_0x4B_echogram = 0;
		nrec_0x4E_rawbeamN = 0;
		nrec_0x4F_quality = 0;
		nrec_0x50_pos = 0;
		nrec_0x52_runtime = 0;
		nrec_0x53_sidescan = 0;
		nrec_0x54_tide = 0;
		nrec_0x55_svp2 = 0;
		nrec_0x56_svp = 0;
		nrec_0x57_surfacesoundspeed = 0;
		nrec_0x58_bathymetry2 = 0;
		nrec_0x59_sidescan2 = 0;
		nrec_0x66_rawbeamf = 0;
		nrec_0x68_height = 0;
		nrec_0x69_parameter_stop = 0;
		nrec_0x6B_water_column = 0;
		nrec_0x6E_network_attitude = 0;
		nrec_0x70_parameter = 0;
		nrec_0x73_surface_sound_speed = 0;
		nrec_0xE1_bathymetry_mbari57 = 0;
		nrec_0xE2_sidescan_mbari57 = 0;
		nrec_0xE3_bathymetry_mbari59 = 0;
		nrec_0xE4_sidescan_mbari59 = 0;
		nrec_0xE5_bathymetry_mbari59 = 0;

		/* read and print data */
		while (error <= MB_ERROR_NO_ERROR) {
			/* reset error */
			error = MB_ERROR_NO_ERROR;

			/* read next data record */
			status = mb_get_all(verbose, imbio_ptr, &istore_ptr, &kind, time_i, &time_d, &navlon, &navlat, &speed, &heading,
			                    &distance, &altitude, &sonardepth, &beams_bath, &beams_amp, &pixels_ss, beamflag, bath, amp,
			                    bathacrosstrack, bathalongtrack, ss, ssacrosstrack, ssalongtrack, comment, &error);

			/* some nonfatal errors do not matter */
			if (error < MB_ERROR_NO_ERROR && error > MB_ERROR_UNINTELLIGIBLE) {
				error = MB_ERROR_NO_ERROR;
				status = MB_SUCCESS;
			}

			/* count the record that was just read */
			if (status == MB_SUCCESS && kind == MB_DATA_DATA) {
				ping = (struct mbsys_simrad3_ping_struct *)&(istore->pings[istore->ping_index]);

				if (format == MBF_EM300RAW) {
					nrec_0x58_bathymetry2++;
					if (ping->png_raw_read == MB_YES)
						nrec_0x4E_rawbeamN++;
					if (ping->png_ss_read == MB_YES)
						nrec_0x59_sidescan2++;
				}
				else if (format == MBF_EM300MBA) {
					nrec_0xE5_bathymetry_mbari59++;
					if (ping->png_raw_read == MB_YES)
						nrec_0x4E_rawbeamN++;
					if (ping->png_ss_read == MB_YES)
						nrec_0x59_sidescan2++;
				}
				else if (format == MBF_EM710RAW) {
					nrec_0x58_bathymetry2++;
					if (ping->png_raw_read == MB_YES)
						nrec_0x4E_rawbeamN++;
					if (ping->png_ss_read == MB_YES)
						nrec_0x59_sidescan2++;
					if (ping->png_quality_read == MB_YES)
						nrec_0x4F_quality++;
				}
				else if (format == MBF_EM710MBA) {
					nrec_0xE5_bathymetry_mbari59++;
					if (ping->png_raw_read == MB_YES)
						nrec_0x4E_rawbeamN++;
					if (ping->png_ss_read == MB_YES)
						nrec_0x59_sidescan2++;
					if (ping->png_quality_read == MB_YES)
						nrec_0x4F_quality++;
				}
			}
			else if (status == MB_SUCCESS) {
				if (istore->type == EM3_PU_ID)
					nrec_0x30_pu_id++;
				if (istore->type == EM3_PU_STATUS)
					nrec_0x31_pu_status++;
				if (istore->type == EM3_PU_BIST)
					nrec_0x32_pu_bist++;
				if (istore->type == EM3_ATTITUDE)
					nrec_0x41_attitude++;
				if (istore->type == EM3_CLOCK)
					nrec_0x43_clock++;
				if (istore->type == EM3_BATH)
					nrec_0x44_bathymetry++;
				if (istore->type == EM3_SBDEPTH)
					nrec_0x45_singlebeam++;
				if (istore->type == EM3_RAWBEAM)
					nrec_0x46_rawbeamF++;
				if (istore->type == EM3_SSV)
					nrec_0x47_surfacesoundspeed2++;
				if (istore->type == EM3_HEADING)
					nrec_0x48_heading++;
				if (istore->type == EM3_START)
					nrec_0x49_parameter_start++;
				if (istore->type == EM3_TILT)
					nrec_0x4A_tilt++;
				if (istore->type == EM3_CBECHO)
					nrec_0x4B_echogram++;
				if (istore->type == EM3_RAWBEAM4)
					nrec_0x4E_rawbeamN++;
				if (istore->type == EM3_QUALITY)
					nrec_0x4F_quality++;
				if (istore->type == EM3_POS)
					nrec_0x50_pos++;
				if (istore->type == EM3_RUN_PARAMETER)
					nrec_0x52_runtime++;
				if (istore->type == EM3_SS)
					nrec_0x53_sidescan++;
				if (istore->type == EM3_TIDE)
					nrec_0x54_tide++;
				if (istore->type == EM3_SVP2)
					nrec_0x55_svp2++;
				if (istore->type == EM3_SVP)
					nrec_0x56_svp++;
				if (istore->type == EM3_SSPINPUT)
					nrec_0x57_surfacesoundspeed++;
				if (istore->type == EM3_BATH2)
					nrec_0x58_bathymetry2++;
				if (istore->type == EM3_SS2)
					nrec_0x59_sidescan2++;
				if (istore->type == EM3_RAWBEAM3)
					nrec_0x66_rawbeamf++;
				if (istore->type == EM3_HEIGHT)
					nrec_0x68_height++;
				if (istore->type == EM3_STOP)
					nrec_0x69_parameter_stop++;
				if (istore->type == EM3_WATERCOLUMN)
					nrec_0x6B_water_column++;
				if (istore->type == EM3_NETATTITUDE)
					nrec_0x6E_network_attitude++;
				if (istore->type == EM3_REMOTE)
					nrec_0x70_parameter++;
				if (istore->type == EM3_SSP)
					nrec_0x73_surface_sound_speed++;
				if (istore->type == EM3_BATH_MBA)
					nrec_0xE1_bathymetry_mbari57++;
				if (istore->type == EM3_SS_MBA)
					nrec_0xE2_sidescan_mbari57++;
				if (istore->type == EM3_BATH2_MBA)
					nrec_0xE3_bathymetry_mbari59++;
				if (istore->type == EM3_SS2_MBA)
					nrec_0xE4_sidescan_mbari59++;
				if (istore->type == EM3_BATH3_MBA)
					nrec_0xE5_bathymetry_mbari59++;
			}

			/* handle read error */
			else {
				/*fprintf(stderr,"READ FAILURE: status:%d error:%d kind:%d\n",status,error,kind);*/
			}

			/* set attitude data source from active sensors set in the start datagram */
			if (status == MB_SUCCESS && istore->type == EM3_START && istore->kind == MB_DATA_START &&
			    attitude_source == MB_DATA_NONE) {
				if (istore->par_aro == 2) {
					attitude_source = MB_DATA_ATTITUDE;
				}
				else if (istore->par_aro == 3) {
					attitude_source = MB_DATA_ATTITUDE1;
				}
				else {
					attitude_source = MB_DATA_ATTITUDE2;
				}
			}

			/* save navigation and heading data from EM3_POS records */
			if (status == MB_SUCCESS && istore->type == EM3_POS &&
			    (istore->kind == nav_source || istore->kind == heading_source)) {
				/* get nav time */
				time_i[0] = istore->pos_date / 10000;
				time_i[1] = (istore->pos_date % 10000) / 100;
				time_i[2] = istore->pos_date % 100;
				time_i[3] = istore->pos_msec / 3600000;
				time_i[4] = (istore->pos_msec % 3600000) / 60000;
				time_i[5] = (istore->pos_msec % 60000) / 1000;
				time_i[6] = (istore->pos_msec % 1000) * 1000;
				mb_get_time(verbose, time_i, &time_d);

				if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
					fprintf(stderr, "Record time: %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d nrec_0x50_pos:%d\n", time_i[0],
					        time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6], nrec_0x50_pos);

				/* deal with desired navigation source and valid positions */
				if (istore->kind == nav_source && istore->pos_longitude != EM3_INVALID_INT &&
				    istore->pos_latitude != EM3_INVALID_INT) {
					/* allocate memory for position arrays if needed */
					if (ndat_nav + 1 >= ndat_nav_alloc) {
						ndat_nav_alloc += MBKONSBERGPREPROCESS_ALLOC_CHUNK;
						status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_nav_alloc * sizeof(double),
						                     (void **)&dat_nav_time_d, &error);
						status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_nav_alloc * sizeof(double), (void **)&dat_nav_lon,
						                     &error);
						status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_nav_alloc * sizeof(double), (void **)&dat_nav_lat,
						                     &error);
						if (error != MB_ERROR_NO_ERROR) {
							mb_error(verbose, error, &message);
							fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
							fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
							exit(error);
						}
					}

					/* store the position data */
					if (ndat_nav == 0 || dat_nav_time_d[ndat_nav - 1] < time_d) {
						dat_nav_time_d[ndat_nav] = time_d;
						dat_nav_lon[ndat_nav] = (double)(0.0000001 * istore->pos_longitude);
						dat_nav_lat[ndat_nav] = (double)(0.00000005 * istore->pos_latitude);

						/* apply time lag correction if specified */
						if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_CONSTANT) {
							dat_nav_time_d[ndat_nav] -= timelagconstant;
						}
						else if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_MODEL && ntimelag > 0) {
							interp_status = mb_linear_interp(verbose, timelag_time_d - 1, timelag_model - 1, ntimelag,
							                                 dat_nav_time_d[ndat_nav], &timelag, &jtimelag, &error);
							dat_nav_time_d[ndat_nav] -= timelag;
						}

						/* increment counter */
						ndat_nav++;
					}
				}

				/* deal with desired heading source and valid heading */
				if (istore->kind == heading_source && istore->pos_heading != EM3_INVALID_INT) {

					/* allocate memory for heading arrays if needed */
					if (ndat_heading + 1 >= ndat_heading_alloc) {
						ndat_heading_alloc += MBKONSBERGPREPROCESS_ALLOC_CHUNK;
						status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_heading_alloc * sizeof(double),
						                     (void **)&dat_heading_time_d, &error);
						status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_heading_alloc * sizeof(double),
						                     (void **)&dat_heading_heading, &error);
						if (error != MB_ERROR_NO_ERROR) {
							mb_error(verbose, error, &message);
							fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
							fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
							exit(error);
						}
					}

					/* store the heading data */
					if (ndat_heading == 0 || dat_heading_time_d[ndat_heading - 1] < time_d) {
						dat_heading_time_d[ndat_heading] = time_d;
						dat_heading_heading[ndat_heading] = (double)(0.01 * istore->pos_heading);

						/* apply time lag correction if specified */
						if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_CONSTANT) {
							dat_heading_time_d[ndat_heading] -= timelagconstant;
						}
						else if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_MODEL && ntimelag > 0) {
							interp_status = mb_linear_interp(verbose, timelag_time_d - 1, timelag_model - 1, ntimelag,
							                                 dat_heading_time_d[ndat_heading], &timelag, &jtimelag, &error);
							dat_nav_time_d[ndat_heading] -= timelag;
						}

						/* increment counter */
						ndat_heading++;
					}
				}
			}

			/* save sonardepth data from height records */
			if (status == MB_SUCCESS && istore->type == EM3_HEIGHT && istore->kind == sonardepth_source) {
				/* get sonardepth time */
				time_i[0] = istore->hgt_date / 10000;
				time_i[1] = (istore->hgt_date % 10000) / 100;
				time_i[2] = istore->hgt_date % 100;
				time_i[3] = istore->hgt_msec / 3600000;
				time_i[4] = (istore->hgt_msec % 3600000) / 60000;
				time_i[5] = (istore->hgt_msec % 60000) / 1000;
				time_i[6] = (istore->hgt_msec % 1000) * 1000;
				mb_get_time(verbose, time_i, &time_d);

				if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
					fprintf(stderr, "Record time: %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d nrec_0x68_height:%d\n", time_i[0],
					        time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6], nrec_0x68_height);

				/* allocate memory for sonar depth arrays if needed */
				if (ndat_sonardepth + 1 >= ndat_sonardepth_alloc) {
					ndat_sonardepth_alloc += MBKONSBERGPREPROCESS_ALLOC_CHUNK;
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_sonardepth_alloc * sizeof(double),
					                     (void **)&dat_sonardepth_time_d, &error);
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_sonardepth_alloc * sizeof(double),
					                     (void **)&dat_sonardepth_sonardepth, &error);
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_sonardepth_alloc * sizeof(double),
					                     (void **)&dat_sonardepth_sonardepthfilter, &error);
					if (error != MB_ERROR_NO_ERROR) {
						mb_error(verbose, error, &message);
						fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
						fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
						exit(error);
					}
				}

				/* store the sonar depth data */
				if (ndat_sonardepth == 0 || dat_sonardepth_time_d[ndat_sonardepth - 1] < time_d) {
					dat_sonardepth_time_d[ndat_sonardepth] = time_d;
					dat_sonardepth_sonardepth[ndat_sonardepth] = 0.01 * istore->hgt_height;
					dat_sonardepth_sonardepthfilter[ndat_sonardepth] = 0.0;

					/* apply time lag correction if specified */
					if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_CONSTANT) {
						dat_sonardepth_time_d[ndat_sonardepth] -= timelagconstant;
					}
					else if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_MODEL && ntimelag > 0) {
						interp_status = mb_linear_interp(verbose, timelag_time_d - 1, timelag_model - 1, ntimelag,
						                                 dat_sonardepth_time_d[ndat_sonardepth], &timelag, &jtimelag, &error);
						dat_sonardepth_time_d[ndat_sonardepth] -= timelag;
					}

					/* increment counter */
					ndat_sonardepth++;
				}
			}

			/* save primary attitude data from attitude records */
			if (status == MB_SUCCESS && istore->type == EM3_ATTITUDE && istore->kind == attitude_source) {
				/* get attitude structure */
				attitude = (struct mbsys_simrad3_attitude_struct *)istore->attitude;

				/* get attitude time */
				time_i[0] = attitude->att_date / 10000;
				time_i[1] = (attitude->att_date % 10000) / 100;
				time_i[2] = attitude->att_date % 100;
				time_i[3] = attitude->att_msec / 3600000;
				time_i[4] = (attitude->att_msec % 3600000) / 60000;
				time_i[5] = (attitude->att_msec % 60000) / 1000;
				time_i[6] = (attitude->att_msec % 1000) * 1000;
				mb_get_time(verbose, time_i, &time_d);

				if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
					fprintf(stderr, "Record time: %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d nrec_0x41_attitude:%d\n", time_i[0],
					        time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6], nrec_0x41_attitude);

				/* allocate memory for attitude arrays if needed */
				if (ndat_rph + attitude->att_ndata >= ndat_rph_alloc) {
					ndat_rph_alloc += MBKONSBERGPREPROCESS_ALLOC_CHUNK;
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_rph_alloc * sizeof(double), (void **)&dat_rph_time_d,
					                     &error);
					status =
					    mb_reallocd(verbose, __FILE__, __LINE__, ndat_rph_alloc * sizeof(double), (void **)&dat_rph_roll, &error);
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_rph_alloc * sizeof(double), (void **)&dat_rph_pitch,
					                     &error);
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_rph_alloc * sizeof(double), (void **)&dat_rph_heave,
					                     &error);
					if (error != MB_ERROR_NO_ERROR) {
						mb_error(verbose, error, &message);
						fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
						fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
						exit(error);
					}
				}

				/* store the attitude data */
				if (ndat_rph == 0 || dat_rph_time_d[ndat_rph - 1] < time_d) {
					for (i = 0; i < attitude->att_ndata; i++) {
						dat_rph_time_d[ndat_rph] = (double)(time_d + 0.001 * attitude->att_time[i]);
						dat_rph_heave[ndat_rph] = (double)(0.01 * attitude->att_heave[i]);
						dat_rph_roll[ndat_rph] = (double)(0.01 * attitude->att_roll[i]);
						dat_rph_pitch[ndat_rph] = (double)(0.01 * attitude->att_pitch[i]);

						/* apply time lag correction if specified */
						if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_CONSTANT) {
							dat_rph_time_d[ndat_rph] -= timelagconstant;
						}
						else if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_MODEL && ntimelag > 0) {
							interp_status = mb_linear_interp(verbose, timelag_time_d - 1, timelag_model - 1, ntimelag,
							                                 dat_rph_time_d[ndat_rph], &timelag, &jtimelag, &error);
							dat_rph_time_d[ndat_rph] -= timelag;
						}

						/* increment counter */
						ndat_rph++;
					}
				}
			}

			/* save primary attitude data from netattitude records */
			if (status == MB_SUCCESS && istore->type == EM3_NETATTITUDE && istore->kind == attitude_source) {
				/* get netattitude structure */
				netattitude = (struct mbsys_simrad3_netattitude_struct *)istore->netattitude;

				/* get attitude time */
				time_i[0] = netattitude->nat_date / 10000;
				time_i[1] = (netattitude->nat_date % 10000) / 100;
				time_i[2] = netattitude->nat_date % 100;
				time_i[3] = netattitude->nat_msec / 3600000;
				time_i[4] = (netattitude->nat_msec % 3600000) / 60000;
				time_i[5] = (netattitude->nat_msec % 60000) / 1000;
				time_i[6] = (netattitude->nat_msec % 1000) * 1000;
				mb_get_time(verbose, time_i, &time_d);

				if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
					fprintf(stderr, "Record time: %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d nrec_0x6E_network_attitude:%d\n",
					        time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6],
					        nrec_0x6E_network_attitude);

				/* allocate memory for attitude arrays if needed */
				if (ndat_rph + netattitude->nat_ndata >= ndat_rph_alloc) {
					ndat_rph_alloc += MBKONSBERGPREPROCESS_ALLOC_CHUNK;
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_rph_alloc * sizeof(double), (void **)&dat_rph_time_d,
					                     &error);
					status =
					    mb_reallocd(verbose, __FILE__, __LINE__, ndat_rph_alloc * sizeof(double), (void **)&dat_rph_roll, &error);
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_rph_alloc * sizeof(double), (void **)&dat_rph_pitch,
					                     &error);
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_rph_alloc * sizeof(double), (void **)&dat_rph_heave,
					                     &error);
					if (error != MB_ERROR_NO_ERROR) {
						mb_error(verbose, error, &message);
						fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
						fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
						exit(error);
					}
				}

				/* store the attitude data */
				if (ndat_rph == 0 || dat_rph_time_d[ndat_rph - 1] < time_d) {
					for (i = 0; i < netattitude->nat_ndata; i++) {
						dat_rph_time_d[ndat_rph] = (double)(time_d + 0.001 * netattitude->nat_time[i]);
						dat_rph_heave[ndat_rph] = (double)(0.01 * netattitude->nat_heave[i]);
						dat_rph_roll[ndat_rph] = (double)(0.01 * netattitude->nat_roll[i]);
						dat_rph_pitch[ndat_rph] = (double)(0.01 * netattitude->nat_pitch[i]);

						/* apply time lag correction if specified */
						if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_CONSTANT) {
							dat_rph_time_d[ndat_rph] -= timelagconstant;
						}
						else if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_MODEL && ntimelag > 0) {
							interp_status = mb_linear_interp(verbose, timelag_time_d - 1, timelag_model - 1, ntimelag,
							                                 dat_rph_time_d[ndat_rph], &timelag, &jtimelag, &error);
							dat_rph_time_d[ndat_rph] -= timelag;
						}

						/* increment counter */
						ndat_rph++;
					}
				}
			}

			/* save primary heading data */
			if (status == MB_SUCCESS && istore->type == EM3_HEADING && istore->kind == heading_source) {
				/* get heading structure */
				headingr = (struct mbsys_simrad3_heading_struct *)istore->heading;

				/* get heading time */
				time_i[0] = headingr->hed_date / 10000;
				time_i[1] = (headingr->hed_date % 10000) / 100;
				time_i[2] = headingr->hed_date % 100;
				time_i[3] = headingr->hed_msec / 3600000;
				time_i[4] = (headingr->hed_msec % 3600000) / 60000;
				time_i[5] = (headingr->hed_msec % 60000) / 1000;
				time_i[6] = (headingr->hed_msec % 1000) * 1000;
				mb_get_time(verbose, time_i, &time_d);

				if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
					fprintf(stderr, "Record time: %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d nrec_0x48_heading:%d\n", time_i[0],
					        time_i[1], time_i[2], time_i[3], time_i[4], time_i[5], time_i[6], nrec_0x48_heading);

				/* allocate memory for heading arrays if needed */
				if (ndat_heading + headingr->hed_ndata >= ndat_heading_alloc) {
					ndat_heading_alloc += MBKONSBERGPREPROCESS_ALLOC_CHUNK;
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_heading_alloc * sizeof(double),
					                     (void **)&dat_heading_time_d, &error);
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_heading_alloc * sizeof(double),
					                     (void **)&dat_heading_heading, &error);
					if (error != MB_ERROR_NO_ERROR) {
						mb_error(verbose, error, &message);
						fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
						fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
						exit(error);
					}
				}

				/* store the heading data */
				if (ndat_heading == 0 || dat_heading_time_d[ndat_heading - 1] < time_d) {
					for (i = 0; i < headingr->hed_ndata; i++) {
						dat_heading_time_d[ndat_heading] = (double)(time_d + 0.001 * headingr->hed_time[i]);
						dat_heading_heading[ndat_heading] = (double)(0.01 * headingr->hed_heading[i]);

						/* apply time lag correction if specified */
						if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_CONSTANT) {
							dat_heading_time_d[ndat_heading] -= timelagconstant;
						}
						else if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_MODEL && ntimelag > 0) {
							interp_status = mb_linear_interp(verbose, timelag_time_d - 1, timelag_model - 1, ntimelag,
							                                 dat_heading_time_d[ndat_heading], &timelag, &jtimelag, &error);
							dat_heading_time_d[ndat_heading] -= timelag;
						}

						/* increment counter */
						ndat_heading++;
					}
				}
			}

			/* save heading data from survey records */
			if (status == MB_SUCCESS && istore->kind == MB_DATA_DATA && istore->kind == heading_source) {
				/* get survey data structure */
				ping = (struct mbsys_simrad3_ping_struct *)&(istore->pings[istore->ping_index]);

				/* get ping time */
				time_i[0] = ping->png_date / 10000;
				time_i[1] = (ping->png_date % 10000) / 100;
				time_i[2] = ping->png_date % 100;
				time_i[3] = ping->png_msec / 3600000;
				time_i[4] = (ping->png_msec % 3600000) / 60000;
				time_i[5] = (ping->png_msec % 60000) / 1000;
				time_i[6] = (ping->png_msec % 1000) * 1000;
				mb_get_time(verbose, time_i, &time_d);

				if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
					fprintf(stderr, "Record time: %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n", time_i[0], time_i[1], time_i[2],
					        time_i[3], time_i[4], time_i[5], time_i[6]);

				/* allocate memory for heading arrays if needed */
				if (ndat_heading + 1 >= ndat_heading_alloc) {
					ndat_heading_alloc += MBKONSBERGPREPROCESS_ALLOC_CHUNK;
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_heading_alloc * sizeof(double),
					                     (void **)&dat_heading_time_d, &error);
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_heading_alloc * sizeof(double),
					                     (void **)&dat_heading_heading, &error);
					if (error != MB_ERROR_NO_ERROR) {
						mb_error(verbose, error, &message);
						fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
						fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
						exit(error);
					}
				}

				/* store the heading data */
				if (ndat_heading == 0 || dat_heading_time_d[ndat_heading - 1] < time_d) {
					dat_heading_time_d[ndat_heading] = (double)(time_d);
					dat_heading_heading[ndat_heading] = (double)(0.01 * ping->png_heading);

					/* apply time lag correction if specified */
					if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_CONSTANT) {
						dat_heading_time_d[ndat_heading] -= timelagconstant;
					}
					else if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_MODEL && ntimelag > 0) {
						interp_status = mb_linear_interp(verbose, timelag_time_d - 1, timelag_model - 1, ntimelag,
						                                 dat_heading_time_d[ndat_heading], &timelag, &jtimelag, &error);
						dat_heading_time_d[ndat_heading] -= timelag;
					}

					/* increment counter */
					ndat_heading++;
				}
			}

			/* save sonardepth data from survey records */
			if (status == MB_SUCCESS && istore->kind == MB_DATA_DATA && istore->kind == sonardepth_source) {
				/* get survey data structure */
				ping = (struct mbsys_simrad3_ping_struct *)&(istore->pings[istore->ping_index]);

				/* get ping time */
				time_i[0] = ping->png_date / 10000;
				time_i[1] = (ping->png_date % 10000) / 100;
				time_i[2] = ping->png_date % 100;
				time_i[3] = ping->png_msec / 3600000;
				time_i[4] = (ping->png_msec % 3600000) / 60000;
				time_i[5] = (ping->png_msec % 60000) / 1000;
				time_i[6] = (ping->png_msec % 1000) * 1000;
				mb_get_time(verbose, time_i, &time_d);

				if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
					fprintf(stderr, "Record time: %4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d\n", time_i[0], time_i[1], time_i[2],
					        time_i[3], time_i[4], time_i[5], time_i[6]);

				/* allocate memory for sonar depth arrays if needed */
				if (ndat_sonardepth + 1 >= ndat_sonardepth_alloc) {
					ndat_sonardepth_alloc += MBKONSBERGPREPROCESS_ALLOC_CHUNK;
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_sonardepth_alloc * sizeof(double),
					                     (void **)&dat_sonardepth_time_d, &error);
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_sonardepth_alloc * sizeof(double),
					                     (void **)&dat_sonardepth_sonardepth, &error);
					status = mb_reallocd(verbose, __FILE__, __LINE__, ndat_sonardepth_alloc * sizeof(double),
					                     (void **)&dat_sonardepth_sonardepthfilter, &error);
					if (error != MB_ERROR_NO_ERROR) {
						mb_error(verbose, error, &message);
						fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
						fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
						exit(error);
					}
				}

				/* store the sonar depth data */
				if (ndat_sonardepth == 0 || dat_sonardepth_time_d[ndat_sonardepth - 1] < time_d) {
					dat_sonardepth_time_d[ndat_sonardepth] = time_d;
					dat_sonardepth_sonardepth[ndat_sonardepth] = ping->png_xducer_depth;
					dat_sonardepth_sonardepthfilter[ndat_sonardepth] = 0.0;

					/* apply time lag correction if specified */
					if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_CONSTANT) {
						dat_sonardepth_time_d[ndat_sonardepth] -= timelagconstant;
					}
					else if (timelagmode == MBKONSBERGPREPROCESS_TIMELAG_MODEL && ntimelag > 0) {
						interp_status = mb_linear_interp(verbose, timelag_time_d - 1, timelag_model - 1, ntimelag,
						                                 dat_sonardepth_time_d[ndat_sonardepth], &timelag, &jtimelag, &error);
						dat_sonardepth_time_d[ndat_sonardepth] -= timelag;
					}

					/* increment counter */
					ndat_sonardepth++;
				}
			}

			/* print debug statements */
			if (verbose >= 2) {
				fprintf(stderr, "\ndbg2  Ping read in program <%s>\n", program_name);
				fprintf(stderr, "dbg2       kind:           %d\n", kind);
				fprintf(stderr, "dbg2       error:          %d\n", error);
				fprintf(stderr, "dbg2       status:         %d\n", status);
			}
		}

		/* close the swath file */
		status = mb_close(verbose, &imbio_ptr, &error);

		/* output counts */
		if (output_counts == MB_YES) {
			fprintf(stdout, "\nData records read from: %s\n", ifile);
			fprintf(stdout, "     nrec_0x30_pu_id:         %d\n", nrec_0x30_pu_id);
			fprintf(stdout, "     nrec_0x31_pu_status:          %d\n", nrec_0x31_pu_status);
			fprintf(stdout, "     nrec_0x32_pu_bist:           %d\n", nrec_0x32_pu_bist);
			fprintf(stdout, "     nrec_0x33_parameter_extra:        %d\n", nrec_0x33_parameter_extra);
			fprintf(stdout, "     nrec_0x41_attitude:               %d\n", nrec_0x41_attitude);
			fprintf(stdout, "     nrec_0x43_clock:                  %d\n", nrec_0x43_clock);
			fprintf(stdout, "     nrec_0x44_bathymetry:             %d\n", nrec_0x44_bathymetry);
			fprintf(stdout, "     nrec_0x45_singlebeam:             %d\n", nrec_0x45_singlebeam);
			fprintf(stdout, "     nrec_0x46_rawbeamF:               %d\n", nrec_0x46_rawbeamF);
			fprintf(stdout, "     nrec_0x47_surfacesoundspeed2:     %d\n", nrec_0x47_surfacesoundspeed2);
			fprintf(stdout, "     nrec_0x48_heading:                %d\n", nrec_0x48_heading);
			fprintf(stdout, "     nrec_0x49_parameter_start:        %d\n", nrec_0x49_parameter_start);
			fprintf(stdout, "     nrec_0x4A_tilt:                   %d\n", nrec_0x4A_tilt);
			fprintf(stdout, "     nrec_0x4B_echogram:               %d\n", nrec_0x4B_echogram);
			fprintf(stdout, "     nrec_0x4E_rawbeamN:               %d\n", nrec_0x4E_rawbeamN);
			fprintf(stdout, "     nrec_0x4F_quality:                %d\n", nrec_0x4F_quality);
			fprintf(stdout, "     nrec_0x50_pos:                    %d\n", nrec_0x50_pos);
			fprintf(stdout, "     nrec_0x52_runtime:                %d\n", nrec_0x52_runtime);
			fprintf(stdout, "     nrec_0x53_sidescan:               %d\n", nrec_0x53_sidescan);
			fprintf(stdout, "     nrec_0x54_tide:                   %d\n", nrec_0x54_tide);
			fprintf(stdout, "     nrec_0x55_svp2:                   %d\n", nrec_0x55_svp2);
			fprintf(stdout, "     nrec_0x56_svp:                    %d\n", nrec_0x56_svp);
			fprintf(stdout, "     nrec_0x57_surfacesoundspeed:      %d\n", nrec_0x57_surfacesoundspeed);
			fprintf(stdout, "     nrec_0x58_bathymetry2:            %d\n", nrec_0x58_bathymetry2);
			fprintf(stdout, "     nrec_0x59_sidescan2:              %d\n", nrec_0x59_sidescan2);
			fprintf(stdout, "     nrec_0x66_rawbeamf:               %d\n", nrec_0x66_rawbeamf);
			fprintf(stdout, "     nrec_0x68_height:                 %d\n", nrec_0x68_height);
			fprintf(stdout, "     nrec_0x69_parameter_stop:         %d\n", nrec_0x69_parameter_stop);
			fprintf(stdout, "     nrec_0x6B_water_column:           %d\n", nrec_0x6B_water_column);
			fprintf(stdout, "     nrec_0x6E_network_attitude:       %d\n", nrec_0x6E_network_attitude);
			fprintf(stdout, "     nrec_0x70_parameter:              %d\n", nrec_0x70_parameter);
			fprintf(stdout, "     nrec_0x73_surface_sound_speed:    %d\n", nrec_0x73_surface_sound_speed);
			fprintf(stdout, "     nrec_0xE1_bathymetry_mbari57:     %d\n", nrec_0xE1_bathymetry_mbari57);
			fprintf(stdout, "     nrec_0xE2_sidescan_mbari57:       %d\n", nrec_0xE2_sidescan_mbari57);
			fprintf(stdout, "     nrec_0xE3_bathymetry_mbari59:     %d\n", nrec_0xE3_bathymetry_mbari59);
			fprintf(stdout, "     nrec_0xE4_sidescan_mbari59:       %d\n", nrec_0xE4_sidescan_mbari59);
			fprintf(stdout, "     nrec_0xE5_bathymetry_mbari59:     %d\n", nrec_0xE5_bathymetry_mbari59);

			nrec_0x30_pu_id_tot += nrec_0x30_pu_id;
			nrec_0x31_pu_status_tot += nrec_0x31_pu_status;
			nrec_0x32_pu_bist_tot += nrec_0x32_pu_bist;
			nrec_0x33_parameter_extra_tot += nrec_0x33_parameter_extra;
			nrec_0x41_attitude_tot += nrec_0x41_attitude;
			nrec_0x43_clock_tot += nrec_0x43_clock;
			nrec_0x44_bathymetry_tot += nrec_0x44_bathymetry;
			nrec_0x45_singlebeam_tot += nrec_0x45_singlebeam;
			nrec_0x46_rawbeamF_tot += nrec_0x46_rawbeamF;
			nrec_0x47_surfacesoundspeed2_tot += nrec_0x47_surfacesoundspeed2;
			nrec_0x48_heading_tot += nrec_0x48_heading;
			nrec_0x49_parameter_start_tot += nrec_0x49_parameter_start;
			nrec_0x4A_tilt_tot += nrec_0x4A_tilt;
			nrec_0x4B_echogram_tot += nrec_0x4B_echogram;
			nrec_0x4E_rawbeamN_tot += nrec_0x4E_rawbeamN;
			nrec_0x4F_quality_tot += nrec_0x4F_quality;
			nrec_0x50_pos_tot += nrec_0x50_pos;
			nrec_0x52_runtime_tot += nrec_0x52_runtime;
			nrec_0x53_sidescan_tot += nrec_0x53_sidescan;
			nrec_0x54_tide_tot += nrec_0x54_tide;
			nrec_0x55_svp2_tot += nrec_0x55_svp2;
			nrec_0x56_svp_tot += nrec_0x56_svp;
			nrec_0x57_surfacesoundspeed_tot += nrec_0x57_surfacesoundspeed;
			nrec_0x58_bathymetry2_tot += nrec_0x58_bathymetry2;
			nrec_0x59_sidescan2_tot += nrec_0x59_sidescan2;
			nrec_0x66_rawbeamf_tot += nrec_0x66_rawbeamf;
			nrec_0x68_height_tot += nrec_0x68_height;
			nrec_0x69_parameter_stop_tot += nrec_0x69_parameter_stop;
			nrec_0x6B_water_column_tot += nrec_0x6B_water_column;
			nrec_0x6E_network_attitude_tot += nrec_0x6E_network_attitude;
			nrec_0x70_parameter_tot += nrec_0x70_parameter;
			nrec_0x73_surface_sound_speed_tot += nrec_0x73_surface_sound_speed;
			nrec_0xE1_bathymetry_mbari57_tot += nrec_0xE1_bathymetry_mbari57;
			nrec_0xE2_sidescan_mbari57_tot += nrec_0xE2_sidescan_mbari57;
			nrec_0xE3_bathymetry_mbari59_tot += nrec_0xE3_bathymetry_mbari59;
			nrec_0xE4_sidescan_mbari59_tot += nrec_0xE4_sidescan_mbari59;
			nrec_0xE5_bathymetry_mbari59_tot += nrec_0xE5_bathymetry_mbari59;
		}

		/* figure out whether and what to read next */
		if (read_datalist == MB_YES) {
			if ((status = mb_datalist_read(verbose, datalist, ifile, dfile, &format, &file_weight, &error)) == MB_SUCCESS)
				read_data = MB_YES;
			else
				read_data = MB_NO;
		}
		else {
			read_data = MB_NO;
		}

		/* end loop over files in list */
	}
	if (read_datalist == MB_YES)
		mb_datalist_close(verbose, &datalist, &error);

	/* if desired apply filtering to sonardepth data */
	if (sonardepthfilter != MBKONSBERGPREPROCESS_FILTER_NONE) {
		/* apply filtering to sonardepth data
		    read from asynchronous records in 7k files */
		if (ndat_sonardepth > 1) {
			dtime = (dat_sonardepth_time_d[ndat_sonardepth - 1] - dat_sonardepth_time_d[0]) / ndat_sonardepth;
			if (sonardepthfilter == MBKONSBERGPREPROCESS_FILTER_MEDIAN) {
				fprintf(stderr, "Applying running median filtering to %d sonardepth data filter length %f seconds\n",
				        ndat_sonardepth, sonardepthfilterlength);
				nhalffilter = (int)(0.5 * sonardepthfilterlength / dtime);
				nmedian_alloc = 2 * nhalffilter + 1;
				median = NULL;
				status = mb_reallocd(verbose, __FILE__, __LINE__, nmedian_alloc * sizeof(double), (void **)&median, &error);
				for (i = 0; i < ndat_sonardepth; i++) {
					dat_sonardepth_sonardepthfilter[i] = dat_sonardepth_sonardepth[i];
					j1 = MAX(i - nhalffilter, 0);
					j2 = MIN(i + nhalffilter, ndat_sonardepth - 1);
					nmedian = 0;
					for (j = j1; j <= j2; j++) {
						median[nmedian] = dat_sonardepth_sonardepth[j];
						nmedian++;
					}
					if (nmedian > 0) {
						qsort((char *)median, nmedian, sizeof(double), (void *)mb_double_compare);
						dat_sonardepth_sonardepthfilter[i] = median[nmedian / 2];
					}
				}
				if (median != NULL) {
					status = mb_freed(verbose, __FILE__, __LINE__, (void **)&median, &error);
					nmedian_alloc = 0;
				}
			}
			else {
				fprintf(stderr, "Applying running Gaussian mean filtering to %d sonardepth data filter length %f seconds\n",
				        ndat_sonardepth, sonardepthfilterlength);
				nhalffilter = (int)(4.0 * sonardepthfilterlength / dtime);
				for (i = 0; i < ndat_sonardepth; i++) {
					dat_sonardepth_sonardepthfilter[i] = 0.0;
					sonardepth_filterweight = 0.0;
					j1 = MAX(i - nhalffilter, 0);
					j2 = MIN(i + nhalffilter, ndat_sonardepth - 1);
					for (j = j1; j <= j2; j++) {
						dtol = (dat_sonardepth_time_d[j] - dat_sonardepth_time_d[i]) / sonardepthfilterlength;
						weight = exp(-dtol * dtol);
						dat_sonardepth_sonardepthfilter[i] += weight * dat_sonardepth_sonardepth[j];
						sonardepth_filterweight += weight;
					}
					if (sonardepth_filterweight > 0.0)
						dat_sonardepth_sonardepthfilter[i] /= sonardepth_filterweight;
					else
						dat_sonardepth_sonardepthfilter[i] = dat_sonardepth_sonardepth[i];
				}
			}
			for (i = 0; i < ndat_sonardepth; i++) {
				if (dat_sonardepth_sonardepth[i] < 2.0 * sonardepthfilterdepth)
					factor = 1.0;
				else
					factor = exp(-(dat_sonardepth_sonardepth[i] - 2.0 * sonardepthfilterdepth) / (sonardepthfilterdepth));
				dat_sonardepth_sonardepth[i] =
				    (1.0 - factor) * dat_sonardepth_sonardepth[i] + factor * dat_sonardepth_sonardepthfilter[i];
			}
		}

		/* filter sonardepth data from separate file */
		if (nsonardepth > 1) {
			dtime = (sonardepth_time_d[nsonardepth - 1] - sonardepth_time_d[0]) / nsonardepth;
			if (sonardepthfilter == MBKONSBERGPREPROCESS_FILTER_MEDIAN) {
				fprintf(stderr, "Applying running median filtering to %d sonardepth nav data filter length %f seconds\n",
				        nsonardepth, sonardepthfilterlength);
				nhalffilter = (int)(0.5 * sonardepthfilterlength / dtime);
				nmedian_alloc = 2 * nhalffilter + 1;
				median = NULL;
				status = mb_reallocd(verbose, __FILE__, __LINE__, nmedian_alloc * sizeof(double), (void **)&median, &error);
				for (i = 0; i < nsonardepth; i++) {
					sonardepth_sonardepthfilter[i] = sonardepth_sonardepth[i];
					j1 = MAX(i - nhalffilter, 0);
					j2 = MIN(i + nhalffilter, nsonardepth - 1);
					nmedian = 0;
					for (j = j1; j <= j2; j++) {
						median[nmedian] = dat_sonardepth_sonardepth[j];
						nmedian++;
					}
					if (nmedian > 0) {
						qsort((char *)median, nmedian, sizeof(double), (void *)mb_double_compare);
						dat_sonardepth_sonardepthfilter[i] = median[nmedian / 2];
					}
				}
				if (median != NULL) {
					status = mb_freed(verbose, __FILE__, __LINE__, (void **)&median, &error);
					nmedian_alloc = 0;
				}
			}
			else {
				fprintf(stderr, "Applying running Gaussian mean filtering to %d sonardepth nav data filter length %f seconds\n",
				        nsonardepth, sonardepthfilterlength);
				nhalffilter = (int)(4.0 * sonardepthfilterlength / dtime);
				for (i = 0; i < nsonardepth; i++) {
					sonardepth_sonardepthfilter[i] = 0.0;
					sonardepth_filterweight = 0.0;
					j1 = MAX(i - nhalffilter, 0);
					j2 = MIN(i + nhalffilter, nsonardepth - 1);
					for (j = j1; j <= j2; j++) {
						dtol = (sonardepth_time_d[j] - sonardepth_time_d[i]) / sonardepthfilterlength;
						weight = exp(-dtol * dtol);
						sonardepth_sonardepthfilter[i] += weight * sonardepth_sonardepth[j];
						sonardepth_filterweight += weight;
					}
					if (sonardepth_filterweight > 0.0)
						sonardepth_sonardepthfilter[i] /= sonardepth_filterweight;
				}
			}
			for (i = 0; i < nsonardepth; i++) {
				if (sonardepth_sonardepth[i] < 2.0 * sonardepthfilterdepth)
					factor = 1.0;
				else
					factor = exp(-(sonardepth_sonardepth[i] - 2.0 * sonardepthfilterdepth) / (sonardepthfilterdepth));
				sonardepth_sonardepth[i] = (1.0 - factor) * sonardepth_sonardepth[i] + factor * sonardepth_sonardepthfilter[i];
			}
		}
	}

	/* output auv sonardepth data */
	if (nsonardepth > 0 && (verbose > 0 || mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)) {
		fprintf(stdout, "\nTotal auv sonardepth data read: %d\n", nsonardepth);
		for (i = 0; i < nsonardepth; i++) {
			fprintf(stdout, "  SONARDEPTH: %12d %8.3f %8.3f\n", i, sonardepth_time_d[i], sonardepth_sonardepth[i]);
		}
	}

	/* output asynchronous navigation and attitude data */
	if (verbose > 0 || mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
		fprintf(stdout, "\nTotal navigation data read: %d\n", ndat_nav);
	if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
		for (i = 0; i < ndat_nav; i++) {
			fprintf(stdout, "  NAV: %5d %17.6f %11.6f %10.6f\n", i, dat_nav_time_d[i], dat_nav_lon[i], dat_nav_lat[i]);
		}
	if (verbose > 0 || mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
		fprintf(stdout, "\nTotal sonardepth data read: %d\n", ndat_sonardepth);
	if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
		for (i = 0; i < ndat_sonardepth; i++) {
			fprintf(stdout, "  DEP: %5d %17.6f %8.3f\n", i, dat_sonardepth_time_d[i], dat_sonardepth_sonardepth[i]);
		}
	if (verbose > 0 || mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
		fprintf(stdout, "\nTotal heading data read: %d\n", ndat_heading);
	if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
		for (i = 0; i < ndat_heading; i++) {
			fprintf(stdout, "  HDG: %5d %17.6f %8.3f\n", i, dat_heading_time_d[i], dat_heading_heading[i]);
		}
	if (verbose > 0 || mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
		fprintf(stdout, "\nTotal attitude data read: %d\n", ndat_rph);
	if (mode == MBKONSBERGPREPROCESS_TIMESTAMPLIST)
		for (i = 0; i < ndat_rph; i++) {
			fprintf(stdout, "  HCP: %5d %17.6f %8.3f %8.3f %8.3f\n", i, dat_rph_time_d[i], dat_rph_roll[i], dat_rph_pitch[i],
			        dat_rph_heave[i]);
		}

	/* output counts */
	if (output_counts == MB_YES) {
		fprintf(stdout, "\nTotal data records read from: %s\n", read_file);
		fprintf(stdout, "     nrec_0x30_pu_id_tot:     %d\n", nrec_0x30_pu_id_tot);
		fprintf(stdout, "     nrec_0x31_pu_status_tot:      %d\n", nrec_0x31_pu_status_tot);
		fprintf(stdout, "     nrec_0x32_pu_bist_tot:       %d\n", nrec_0x32_pu_bist_tot);
		fprintf(stdout, "     nrec_0x33_parameter_extra_tot:    %d\n", nrec_0x33_parameter_extra_tot);
		fprintf(stdout, "     nrec_0x41_attitude_tot:           %d\n", nrec_0x41_attitude_tot);
		fprintf(stdout, "     nrec_0x43_clock_tot:              %d\n", nrec_0x43_clock_tot);
		fprintf(stdout, "     nrec_0x44_bathymetry_tot:         %d\n", nrec_0x44_bathymetry_tot);
		fprintf(stdout, "     nrec_0x45_singlebeam_tot:         %d\n", nrec_0x45_singlebeam_tot);
		fprintf(stdout, "     nrec_0x46_rawbeamF_tot:           %d\n", nrec_0x46_rawbeamF_tot);
		fprintf(stdout, "     nrec_0x47_surfacesoundspeed2_tot: %d\n", nrec_0x47_surfacesoundspeed2_tot);
		fprintf(stdout, "     nrec_0x48_heading_tot:            %d\n", nrec_0x48_heading_tot);
		fprintf(stdout, "     nrec_0x49_parameter_start_tot:    %d\n", nrec_0x49_parameter_start_tot);
		fprintf(stdout, "     nrec_0x4A_tilt_tot:               %d\n", nrec_0x4A_tilt_tot);
		fprintf(stdout, "     nrec_0x4B_echogram_tot:           %d\n", nrec_0x4B_echogram_tot);
		fprintf(stdout, "     nrec_0x4E_rawbeamN_tot:           %d\n", nrec_0x4E_rawbeamN_tot);
		fprintf(stdout, "     nrec_0x4F_quality_tot:            %d\n", nrec_0x4F_quality_tot);
		fprintf(stdout, "     nrec_0x50_pos_tot:                %d\n", nrec_0x50_pos_tot);
		fprintf(stdout, "     nrec_0x52_runtime_tot:            %d\n", nrec_0x52_runtime_tot);
		fprintf(stdout, "     nrec_0x53_sidescan_tot:           %d\n", nrec_0x53_sidescan_tot);
		fprintf(stdout, "     nrec_0x54_tide_tot:               %d\n", nrec_0x54_tide_tot);
		fprintf(stdout, "     nrec_0x55_svp2_tot:               %d\n", nrec_0x55_svp2_tot);
		fprintf(stdout, "     nrec_0x56_svp_tot:                %d\n", nrec_0x56_svp_tot);
		fprintf(stdout, "     nrec_0x57_surfacesoundspeed_tot:  %d\n", nrec_0x57_surfacesoundspeed_tot);
		fprintf(stdout, "     nrec_0x58_bathymetry2_tot:        %d\n", nrec_0x58_bathymetry2_tot);
		fprintf(stdout, "     nrec_0x59_sidescan2_tot:          %d\n", nrec_0x59_sidescan2_tot);
		fprintf(stdout, "     nrec_0x66_rawbeamf_tot:           %d\n", nrec_0x66_rawbeamf_tot);
		fprintf(stdout, "     nrec_0x68_height_tot:             %d\n", nrec_0x68_height_tot);
		fprintf(stdout, "     nrec_0x69_parameter_stop_tot:     %d\n", nrec_0x69_parameter_stop_tot);
		fprintf(stdout, "     nrec_0x6B_water_column_tot:       %d\n", nrec_0x6B_water_column_tot);
		fprintf(stdout, "     nrec_0x6E_network_attitude_tot:   %d\n", nrec_0x6E_network_attitude_tot);
		fprintf(stdout, "     nrec_0x70_parameter_tot:          %d\n", nrec_0x70_parameter_tot);
		fprintf(stdout, "     nrec_0x73_surface_sound_speed_tot:%d\n", nrec_0x73_surface_sound_speed_tot);
		fprintf(stdout, "     nrec_0xE1_bathymetry_mbari57_tot: %d\n", nrec_0xE1_bathymetry_mbari57_tot);
		fprintf(stdout, "     nrec_0xE2_sidescan_mbari57_tot:   %d\n", nrec_0xE2_sidescan_mbari57_tot);
		fprintf(stdout, "     nrec_0xE3_bathymetry_mbari59_tot: %d\n", nrec_0xE3_bathymetry_mbari59_tot);
		fprintf(stdout, "     nrec_0xE4_sidescan_mbari59_tot:   %d\n", nrec_0xE4_sidescan_mbari59_tot);
		fprintf(stdout, "     nrec_0xE5_bathymetry_mbari59_tot: %d\n", nrec_0xE5_bathymetry_mbari59_tot);
	}
	nrec_0x30_pu_id_tot = 0;
	nrec_0x31_pu_status_tot = 0;
	nrec_0x32_pu_bist_tot = 0;
	nrec_0x33_parameter_extra_tot = 0;
	nrec_0x41_attitude_tot = 0;
	nrec_0x43_clock_tot = 0;
	nrec_0x44_bathymetry_tot = 0;
	nrec_0x45_singlebeam_tot = 0;
	nrec_0x46_rawbeamF_tot = 0;
	nrec_0x47_surfacesoundspeed2_tot = 0;
	nrec_0x48_heading_tot = 0;
	nrec_0x49_parameter_start_tot = 0;
	nrec_0x4A_tilt_tot = 0;
	nrec_0x4B_echogram_tot = 0;
	nrec_0x4E_rawbeamN_tot = 0;
	nrec_0x4F_quality_tot = 0;
	nrec_0x50_pos_tot = 0;
	nrec_0x52_runtime_tot = 0;
	nrec_0x53_sidescan_tot = 0;
	nrec_0x54_tide_tot = 0;
	nrec_0x55_svp2_tot = 0;
	nrec_0x56_svp_tot = 0;
	nrec_0x57_surfacesoundspeed_tot = 0;
	nrec_0x58_bathymetry2_tot = 0;
	nrec_0x59_sidescan2_tot = 0;
	nrec_0x66_rawbeamf_tot = 0;
	nrec_0x68_height_tot = 0;
	nrec_0x69_parameter_stop_tot = 0;
	nrec_0x6B_water_column_tot = 0;
	nrec_0x6E_network_attitude_tot = 0;
	nrec_0x70_parameter_tot = 0;
	nrec_0x73_surface_sound_speed_tot = 0;
	nrec_0xE1_bathymetry_mbari57_tot = 0;
	nrec_0xE2_sidescan_mbari57_tot = 0;
	nrec_0xE3_bathymetry_mbari59_tot = 0;
	nrec_0xE4_sidescan_mbari59_tot = 0;
	nrec_0xE5_bathymetry_mbari59_tot = 0;

	/* now read the data files again, this time interpolating nav and attitude
	    into the multibeam records and fixing other problems found in the
	    data */
	if (mode >= MBKONSBERGPREPROCESS_PROCESS) {

		/* open file list */
		if (read_datalist == MB_YES) {
			if ((status = mb_datalist_open(verbose, &datalist, read_file, look_processed, &error)) != MB_SUCCESS) {
				error = MB_ERROR_OPEN_FAIL;
				fprintf(stderr, "\nUnable to open data list file: %s\n", read_file);
				fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
				exit(error);
			}
			if ((status = mb_datalist_read(verbose, datalist, ifile, dfile, &format, &file_weight, &error)) == MB_SUCCESS)
				read_data = MB_YES;
			else
				read_data = MB_NO;
		}
		/* else copy single filename to be read */
		else {
			strcpy(ifile, read_file);
			read_data = MB_YES;
		}

		/* loop over all files to be read */
		while (read_data == MB_YES && (format == MBF_EM710RAW || format == MBF_EM710MBA)) {
			/* figure out the output file name if not specified */
			if (ofile_set == MB_NO) {
				status = mb_get_format(verbose, ifile, fileroot, &testformat, &error);
				if (format == MBF_EM710MBA && strncmp(".mb59", &ifile[strlen(ifile) - 5], 5) == 0)
					sprintf(ofile, "%sf.mb%d", fileroot, MBF_EM710MBA);
				else
					sprintf(ofile, "%s.mb%d", fileroot, MBF_EM710MBA);
			}

			/* if output directory was set by user, reset file path */
			if (odir_set == MB_YES) {
				strcpy(buffer, odir);
				if (buffer[strlen(odir) - 1] != '/')
					strcat(buffer, "/");
				if (strrchr(ofile, '/') != NULL)
					filenameptr = strrchr(ofile, '/') + 1;
				else
					filenameptr = ofile;
				strcat(buffer, filenameptr);
				strcpy(ofile, buffer);
			}

			/* initialize reading the input swath file */
			if ((status = mb_read_init(verbose, ifile, format, pings, lonflip, bounds, btime_i, etime_i, speedmin, timegap,
			                           &imbio_ptr, &btime_d, &etime_d, &beams_bath, &beams_amp, &pixels_ss, &error)) !=
			    MB_SUCCESS) {
				mb_error(verbose, error, &message);
				fprintf(stderr, "\nMBIO Error returned from function <mb_read_init>:\n%s\n", message);
				fprintf(stderr, "\nMultibeam File <%s> not initialized for reading\n", ifile);
				fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
				exit(error);
			}
			nfile_read++;

			/* if ofile has been set then there is only one output file, otherwise there
			    is an output file for each input file */
			if (ofile_set == MB_NO || nfile_write == 0) {
				/* initialize writing the output swath sonar file */
				if ((status = mb_write_init(verbose, ofile, MBF_EM710MBA, &ombio_ptr, &obeams_bath, &obeams_amp, &opixels_ss,
				                            &error)) != MB_SUCCESS) {
					mb_error(verbose, error, &message);
					fprintf(stderr, "\nMBIO Error returned from function <mb_write_init>:\n%s\n", message);
					fprintf(stderr, "\nMultibeam File <%s> not initialized for writing\n", ofile);
					fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
					exit(error);
				}
				nfile_write++;

				/* initialize synchronous attitude output file */
				sprintf(stafile, "%s.sta", ofile);
				if ((stafp = fopen(stafile, "w")) == NULL) {
					error = MB_ERROR_OPEN_FAIL;
					fprintf(stderr, "\nUnable to open synchronous attitude data file <%s> for writing\n", stafile);
					fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
					exit(error);
				}
			}

			/* get pointers to data storage */
			imb_io_ptr = (struct mb_io_struct *)imbio_ptr;
			istore_ptr = imb_io_ptr->store_data;
			istore = (struct mbsys_simrad3_struct *)istore_ptr;

			if (error == MB_ERROR_NO_ERROR) {
				beamflag = NULL;
				bath = NULL;
				amp = NULL;
				bathacrosstrack = NULL;
				bathalongtrack = NULL;
				ss = NULL;
				ssacrosstrack = NULL;
				ssalongtrack = NULL;
			}
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(char), (void **)&beamflag, &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bath, &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_AMPLITUDE, sizeof(double), (void **)&amp, &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathacrosstrack,
				                           &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&bathalongtrack,
				                           &error);
			if (error == MB_ERROR_NO_ERROR)
				status = mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ss, &error);
			if (error == MB_ERROR_NO_ERROR)
				status =
				    mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssacrosstrack, &error);
			if (error == MB_ERROR_NO_ERROR)
				status =
				    mb_register_array(verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ssalongtrack, &error);

			/* if error initializing memory then quit */
			if (error != MB_ERROR_NO_ERROR) {
				mb_error(verbose, error, &message);
				fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
				fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
				exit(error);
			}

			/* reset file record counters */
			nrec_0x30_pu_id = 0;
			nrec_0x31_pu_status = 0;
			nrec_0x32_pu_bist = 0;
			nrec_0x33_parameter_extra = 0;
			nrec_0x41_attitude = 0;
			nrec_0x43_clock = 0;
			nrec_0x44_bathymetry = 0;
			nrec_0x45_singlebeam = 0;
			nrec_0x46_rawbeamF = 0;
			nrec_0x47_surfacesoundspeed2 = 0;
			nrec_0x48_heading = 0;
			nrec_0x49_parameter_start = 0;
			nrec_0x4A_tilt = 0;
			nrec_0x4B_echogram = 0;
			nrec_0x4E_rawbeamN = 0;
			nrec_0x4F_quality = 0;
			nrec_0x50_pos = 0;
			nrec_0x52_runtime = 0;
			nrec_0x53_sidescan = 0;
			nrec_0x54_tide = 0;
			nrec_0x55_svp2 = 0;
			nrec_0x56_svp = 0;
			nrec_0x57_surfacesoundspeed = 0;
			nrec_0x58_bathymetry2 = 0;
			nrec_0x59_sidescan2 = 0;
			nrec_0x66_rawbeamf = 0;
			nrec_0x68_height = 0;
			nrec_0x69_parameter_stop = 0;
			nrec_0x6B_water_column = 0;
			nrec_0x6E_network_attitude = 0;
			nrec_0x70_parameter = 0;
			nrec_0x73_surface_sound_speed = 0;
			nrec_0xE1_bathymetry_mbari57 = 0;
			nrec_0xE2_sidescan_mbari57 = 0;
			nrec_0xE3_bathymetry_mbari59 = 0;
			nrec_0xE4_sidescan_mbari59 = 0;
			nrec_0xE5_bathymetry_mbari59 = 0;

			/* read and write data */
			while (error <= MB_ERROR_NO_ERROR) {
				/* reset error */
				status = MB_SUCCESS;
				error = MB_ERROR_NO_ERROR;

				/* read next data record */
				status = mb_get_all(verbose, imbio_ptr, &istore_ptr, &kind, time_i, &time_d, &navlon, &navlat, &speed, &heading,
				                    &distance, &altitude, &sonardepth, &beams_bath, &beams_amp, &pixels_ss, beamflag, bath, amp,
				                    bathacrosstrack, bathalongtrack, ss, ssacrosstrack, ssalongtrack, comment, &error);

				/* some nonfatal errors do not matter */
				if (error < MB_ERROR_NO_ERROR && error > MB_ERROR_UNINTELLIGIBLE) {
					error = MB_ERROR_NO_ERROR;
					status = MB_SUCCESS;
				}

				/* if specified set water column record to error so it will not be output */
				if (watercolumnmode == MBKONSBERGPREPROCESS_WATERCOLUMN_IGNORE && status == MB_SUCCESS &&
				    istore->type == EM3_WATERCOLUMN) {
					error = MB_ERROR_IGNORE;
					status = MB_FAILURE;
				}

				/* keep track of starting and ending time of sonar data for this file */
				if (status == MB_SUCCESS && kind == MB_DATA_DATA) {
					if (nrec_0xE5_bathymetry_mbari59 == 0)
						start_time_d = time_d;
					end_time_d = time_d;
				}

				/* count the record that was just read */
				if (status == MB_SUCCESS && kind == MB_DATA_DATA) {
					/* get survey data structure */
					ping = (struct mbsys_simrad3_ping_struct *)&(istore->pings[istore->ping_index]);

					nrec_0xE5_bathymetry_mbari59++;
					if (ping->png_raw_read == MB_YES)
						nrec_0x4E_rawbeamN++;
					if (ping->png_ss_read == MB_YES)
						nrec_0x59_sidescan2++;
					if (ping->png_quality_read == MB_YES)
						nrec_0x4F_quality++;
				}
				else if (status == MB_SUCCESS) {
					if (istore->type == EM3_PU_ID)
						nrec_0x30_pu_id++;
					if (istore->type == EM3_PU_STATUS)
						nrec_0x31_pu_status++;
					if (istore->type == EM3_PU_BIST)
						nrec_0x32_pu_bist++;
					if (istore->type == EM3_ATTITUDE)
						nrec_0x41_attitude++;
					if (istore->type == EM3_CLOCK)
						nrec_0x43_clock++;
					if (istore->type == EM3_BATH)
						nrec_0x44_bathymetry++;
					if (istore->type == EM3_SBDEPTH)
						nrec_0x45_singlebeam++;
					if (istore->type == EM3_RAWBEAM)
						nrec_0x46_rawbeamF++;
					if (istore->type == EM3_SSV)
						nrec_0x47_surfacesoundspeed2++;
					if (istore->type == EM3_HEADING)
						nrec_0x48_heading++;
					if (istore->type == EM3_START)
						nrec_0x49_parameter_start++;
					if (istore->type == EM3_TILT)
						nrec_0x4A_tilt++;
					if (istore->type == EM3_CBECHO)
						nrec_0x4B_echogram++;
					if (istore->type == EM3_RAWBEAM4)
						nrec_0x4E_rawbeamN++;
					if (istore->type == EM3_QUALITY)
						nrec_0x4F_quality++;
					if (istore->type == EM3_POS)
						nrec_0x50_pos++;
					if (istore->type == EM3_RUN_PARAMETER)
						nrec_0x52_runtime++;
					if (istore->type == EM3_SS)
						nrec_0x53_sidescan++;
					if (istore->type == EM3_TIDE)
						nrec_0x54_tide++;
					if (istore->type == EM3_SVP2)
						nrec_0x55_svp2++;
					if (istore->type == EM3_SVP)
						nrec_0x56_svp++;
					if (istore->type == EM3_SSPINPUT)
						nrec_0x57_surfacesoundspeed++;
					if (istore->type == EM3_BATH2)
						nrec_0x58_bathymetry2++;
					if (istore->type == EM3_SS2)
						nrec_0x59_sidescan2++;
					if (istore->type == EM3_RAWBEAM3)
						nrec_0x66_rawbeamf++;
					if (istore->type == EM3_HEIGHT)
						nrec_0x68_height++;
					if (istore->type == EM3_STOP)
						nrec_0x69_parameter_stop++;
					if (istore->type == EM3_WATERCOLUMN)
						nrec_0x6B_water_column++;
					if (istore->type == EM3_NETATTITUDE)
						nrec_0x6E_network_attitude++;
					if (istore->type == EM3_REMOTE)
						nrec_0x70_parameter++;
					if (istore->type == EM3_SSP)
						nrec_0x73_surface_sound_speed++;
				}

				/* handle multibeam data */
				if (status == MB_SUCCESS && kind == MB_DATA_DATA) {
					/* get survey data structure */
					ping = (struct mbsys_simrad3_ping_struct *)&(istore->pings[istore->ping_index]);

					/* get transducer offsets */
					if (istore->par_stc == 0) {
						tx_x = istore->par_s1x;
						tx_y = istore->par_s1y;
						tx_z = istore->par_s1z;
						tx_h = istore->par_s1h;
						tx_r = istore->par_s1r;
						tx_p = istore->par_s1p;
						rx_x = istore->par_s2x;
						rx_y = istore->par_s2y;
						rx_z = istore->par_s2z;
						rx_h = istore->par_s2h;
						rx_r = istore->par_s2r;
						rx_p = istore->par_s2p;
					}
					else if (istore->par_stc == 1) {
						tx_x = istore->par_s1x;
						tx_y = istore->par_s1y;
						tx_z = istore->par_s1z;
						tx_h = istore->par_s1h;
						tx_r = istore->par_s1r;
						tx_p = istore->par_s1p;
						rx_x = istore->par_s1x;
						rx_y = istore->par_s1y;
						rx_z = istore->par_s1z;
						rx_h = istore->par_s1h;
						rx_r = istore->par_s1r;
						rx_p = istore->par_s1p;
					}
					else if (istore->par_stc == 2 && ping->png_serial == istore->par_serial_1) {
						tx_x = istore->par_s1x;
						tx_y = istore->par_s1y;
						tx_z = istore->par_s1z;
						tx_h = istore->par_s1h;
						tx_r = istore->par_s1r;
						tx_p = istore->par_s1p;
						rx_x = istore->par_s1x;
						rx_y = istore->par_s1y;
						rx_z = istore->par_s1z;
						rx_h = istore->par_s1h;
						rx_r = istore->par_s1r;
						rx_p = istore->par_s1p;
					}
					else if (istore->par_stc == 2 && ping->png_serial == istore->par_serial_2) {
						tx_x = istore->par_s2x;
						tx_y = istore->par_s2y;
						tx_z = istore->par_s2z;
						tx_h = istore->par_s2h;
						tx_r = istore->par_s2r;
						tx_p = istore->par_s2p;
						rx_x = istore->par_s2x;
						rx_y = istore->par_s2y;
						rx_z = istore->par_s2z;
						rx_h = istore->par_s2h;
						rx_r = istore->par_s2r;
						rx_p = istore->par_s2p;
					}
					else if (istore->par_stc == 3 && ping->png_serial == istore->par_serial_1) {
						tx_x = istore->par_s1x;
						tx_y = istore->par_s1y;
						tx_z = istore->par_s1z;
						tx_h = istore->par_s1h;
						tx_r = istore->par_s1r;
						tx_p = istore->par_s1p;
						rx_x = istore->par_s2x;
						rx_y = istore->par_s2y;
						rx_z = istore->par_s2z;
						rx_h = istore->par_s2h;
						rx_r = istore->par_s2r;
						rx_p = istore->par_s2p;
					}
					else if (istore->par_stc == 3 && ping->png_serial == istore->par_serial_2) {
						tx_x = istore->par_s1x;
						tx_y = istore->par_s1y;
						tx_z = istore->par_s1z;
						tx_h = istore->par_s1h;
						tx_r = istore->par_s1r;
						tx_p = istore->par_s1p;
						rx_x = istore->par_s3x;
						rx_y = istore->par_s3y;
						rx_z = istore->par_s3z;
						rx_h = istore->par_s3h;
						rx_r = istore->par_s3r;
						rx_p = istore->par_s3p;
					}
					else if (istore->par_stc == 4 && ping->png_serial == istore->par_serial_1) {
						tx_x = istore->par_s0x;
						tx_y = istore->par_s0y;
						tx_z = istore->par_s0z;
						tx_h = istore->par_s0h;
						tx_r = istore->par_s0r;
						tx_p = istore->par_s0p;
						rx_x = istore->par_s2x;
						rx_y = istore->par_s2y;
						rx_z = istore->par_s2z;
						rx_h = istore->par_s2h;
						rx_r = istore->par_s2r;
						rx_p = istore->par_s2p;
					}
					else if (istore->par_stc == 4 && ping->png_serial == istore->par_serial_2) {
						tx_x = istore->par_s1x;
						tx_y = istore->par_s1y;
						tx_z = istore->par_s1z;
						tx_h = istore->par_s1h;
						tx_r = istore->par_s1r;
						tx_p = istore->par_s1p;
						rx_x = istore->par_s3x;
						rx_y = istore->par_s3y;
						rx_z = istore->par_s3z;
						rx_h = istore->par_s3h;
						rx_r = istore->par_s3r;
						rx_p = istore->par_s3p;
					}

					/* get active sensor offsets */
					if (depthsensor_mode == MBKONSBERGPREPROCESS_ZMODE_UNKNOWN) {
						if (istore->par_dsh[0] == 'I')
							depthsensor_mode = MBKONSBERGPREPROCESS_ZMODE_USE_SENSORDEPTH_ONLY;
						else if (istore->par_dsh[0] == 'N')
							depthsensor_mode = MBKONSBERGPREPROCESS_ZMODE_USE_SENSORDEPTH_AND_HEAVE;
						else
							depthsensor_mode = MBKONSBERGPREPROCESS_ZMODE_USE_HEAVE_ONLY;
					}
					if (sonardepthlever == MB_NO) {
						depth_off_x = istore->par_dsx;
						depth_off_y = istore->par_dsy;
						depth_off_z = istore->par_dsz;

						sonardepthoffset = istore->par_dso;
						depthsensoroffx = tx_x - istore->par_dsx;
						depthsensoroffy = tx_y - istore->par_dsy;
						depthsensoroffz = tx_z - istore->par_dsz;
					}
					if (istore->par_aps == 0) {
						position_off_x = istore->par_p1x;
						position_off_y = istore->par_p1y;
						position_off_z = istore->par_p1z;
					}
					else if (istore->par_aps == 1) {
						position_off_x = istore->par_p2x;
						position_off_y = istore->par_p2y;
						position_off_z = istore->par_p2z;
					}
					else if (istore->par_aps == 2) {
						position_off_x = istore->par_p3x;
						position_off_y = istore->par_p3y;
						position_off_z = istore->par_p3z;
					}
					if (istore->par_aro == 2) {
						rollpitch_off_x = istore->par_msx;
						rollpitch_off_y = istore->par_msy;
						rollpitch_off_z = istore->par_msz;
						rollpitch_off_h = istore->par_msg;
						rollpitch_off_r = istore->par_msr;
						rollpitch_off_p = istore->par_msp;
					}
					else if (istore->par_aro == 3) {
						rollpitch_off_x = istore->par_nsx;
						rollpitch_off_y = istore->par_nsy;
						rollpitch_off_z = istore->par_nsz;
						rollpitch_off_h = istore->par_nsg;
						rollpitch_off_r = istore->par_nsr;
						rollpitch_off_p = istore->par_nsp;
					}
					if (istore->par_ahe == 2) {
						heave_off_x = istore->par_msx;
						heave_off_y = istore->par_msy;
						heave_off_z = istore->par_msz;
						heave_off_h = istore->par_msg;
						heave_off_r = istore->par_msr;
						heave_off_p = istore->par_msp;
					}
					else if (istore->par_ahe == 3) {
						heave_off_x = istore->par_nsx;
						heave_off_y = istore->par_nsy;
						heave_off_z = istore->par_nsz;
						heave_off_h = istore->par_nsg;
						heave_off_r = istore->par_nsr;
						heave_off_p = istore->par_nsp;
					}
					if (istore->par_ahs == 0 || istore->par_ahs == 4) {
						heading_off_x = istore->par_p3x;
						heading_off_y = istore->par_p3y;
						heading_off_z = istore->par_p3z;
						heading_off_h = istore->par_gcg;
						heading_off_r = 0.0;
						heading_off_p = 0.0;
					}
					else if (istore->par_ahs == 1) {
						heading_off_x = istore->par_p1x;
						heading_off_y = istore->par_p1y;
						heading_off_z = istore->par_p1z;
						heading_off_h = istore->par_gcg;
						heading_off_r = 0.0;
						heading_off_p = 0.0;
					}
					else if (istore->par_ahs == 2) {
						heading_off_x = istore->par_msx;
						heading_off_y = istore->par_msy;
						heading_off_z = istore->par_msz;
						heading_off_h = istore->par_msg + istore->par_gcg;
						heading_off_r = istore->par_msr;
						heading_off_p = istore->par_msp;
					}
					else if (istore->par_ahs == 3 && istore->par_nsz != 0.0) {
						heading_off_x = istore->par_nsx;
						heading_off_y = istore->par_nsy;
						heading_off_z = istore->par_nsz;
						heading_off_h = istore->par_nsg + istore->par_gcg;
						heading_off_r = istore->par_nsr;
						heading_off_p = istore->par_nsp;
					}
					else if (istore->par_ahs == 3) {
						heading_off_x = istore->par_p2x;
						heading_off_y = istore->par_p2y;
						heading_off_z = istore->par_p2z;
						heading_off_h = istore->par_gcg;
						heading_off_r = 0.0;
						heading_off_p = 0.0;
					}

					/* merge heading from best available source */
					if (ndat_heading > 0) {
						interp_status = mb_linear_interp_heading(verbose, dat_heading_time_d - 1, dat_heading_heading - 1,
						                                         ndat_heading, time_d, &heading, &jheading, &error);
					}
					else {
						/* if from embedded ancillary data apply installation parameters */
						mb_hedint_interp(verbose, imbio_ptr, time_d, &heading, &error);
					}
					if (heading < 0.0)
						heading += 360.0;
					else if (heading >= 360.0)
						heading -= 360.0;

					/* merge navigation from best available source */
					if (ndat_nav > 0) {
						interp_status = mb_linear_interp_longitude(verbose, dat_nav_time_d - 1, dat_nav_lon - 1, ndat_nav, time_d,
						                                           &navlon, &jnav, &error);
						if (interp_status == MB_SUCCESS)
							interp_status = mb_linear_interp_latitude(verbose, dat_nav_time_d - 1, dat_nav_lat - 1, ndat_nav,
							                                          time_d, &navlat, &jnav, &error);
					}
					else {
						/* if from embedded ancillary data apply installation parameters */
						mb_navint_interp(verbose, imbio_ptr, time_d, heading, 0.0, &navlon, &navlat, &speed, &error);
					}

					/* merge sonardepth from best available source */
					if (nsonardepth > 0) {
						if (interp_status == MB_SUCCESS)
							interp_status = mb_linear_interp(verbose, sonardepth_time_d - 1, sonardepth_sonardepth - 1,
							                                 nsonardepth, time_d, &sonardepth, &jsonardepth, &error);
					}
					else if (ndat_sonardepth > 0) {
						interp_status = mb_linear_interp(verbose, dat_sonardepth_time_d - 1, dat_sonardepth_sonardepth - 1,
						                                 ndat_sonardepth, time_d, &sonardepth, &jsonardepth, &error);
					}
					else {
						mb_depint_interp(verbose, imbio_ptr, time_d, &sonardepth, &error);
					}

					/* get attitude from best available source */
					if (ndat_rph > 0) {
						interp_status = mb_linear_interp(verbose, dat_rph_time_d - 1, dat_rph_roll - 1, ndat_rph, time_d, &roll,
						                                 &jattitude, &error);
						if (interp_status == MB_SUCCESS)
							interp_status = mb_linear_interp(verbose, dat_rph_time_d - 1, dat_rph_pitch - 1, ndat_rph, time_d,
							                                 &pitch, &jattitude, &error);
						if (interp_status == MB_SUCCESS)
							interp_status = mb_linear_interp(verbose, dat_rph_time_d - 1, dat_rph_heave - 1, ndat_rph, time_d,
							                                 &heave, &jattitude, &error);
					}
					else {
						mb_attint_interp(verbose, imbio_ptr, time_d, &heave, &roll, &pitch, &error);
					}

					/* apply sonardepth/heave mode - if on a submerged platform usually
					 * don't use heave - if on a surface platform usually don't use
					 * a variable sonar depth */
					// if (depthsensor_mode == MBKONSBERGPREPROCESS_ZMODE_USE_HEAVE_ONLY)
					//	{
					//	sonardepth = 0.0;
					//	}
					// else if (depthsensor_mode == MBKONSBERGPREPROCESS_ZMODE_USE_SENSORDEPTH_ONLY)
					//	{
					//	heave = 0.0;
					//	}

					/* Disable this section - Kongsberg SIS logs sensordepth values
					 * already compensated for lever arms
					 * 30 Jun 2016 DWC */

					/* apply specified offset between depth sensor and sonar */
					// fprintf(stderr,"time_d:%.3f png_xducer_depth:%.3f  sonardepth:%f   ",
					// time_d,ping->png_xducer_depth,sonardepth);
					// sonardepth += sonardepthoffset
					//			+ depthsensoroffx * sin(DTR * roll)
					//			+ depthsensoroffy * sin(DTR * pitch)
					//			+ depthsensoroffz * cos(DTR * pitch);
					// fprintf(stderr," sonardepth:%f     offset:%f x:%f y:%f z:%f  rph: %f %f %f   diff:%f\n",
					// sonardepth,sonardepthoffset,depthsensoroffx,depthsensoroffy,depthsensoroffz,roll,pitch,heave,
					//(sonardepthoffset + depthsensoroffx * sin(DTR * roll) + depthsensoroffy * sin(DTR * pitch) + depthsensoroffz
					//* cos(DTR * pitch)));

					/* insert navigation */
					if (navlon < -180.0)
						navlon += 360.0;
					else if (navlon > 180.0)
						navlon -= 360.0;
					ping->png_longitude = 10000000 * navlon;
					ping->png_latitude = 20000000 * navlat;

					/* insert sonardepth */
					ping->png_xducer_depth = sonardepth;

					/* insert heading */
					if (heading < 0.0)
						heading += 360.0;
					else if (heading > 360.0)
						heading -= 360.0;
					ping->png_heading = (int)rint(heading * 100);

					/* insert roll pitch and heave */
					ping->png_roll = (int)rint(roll / 0.01);
					ping->png_pitch = (int)rint(pitch / 0.01);
					ping->png_heave = (int)rint(heave / 0.01);

					/* output synchronous attitude */
					fprintf(stafp, "%0.6f\t%0.3f\t%0.3f\n", time_d, roll, pitch);

					/* calculate corrected ranges, angles, and bathymetry for each beam */
					for (i = 0; i < ping->png_nbeams; i++) {
						/* calculate time of transmit and receive */
						transmit_time_d = time_d + (double)ping->png_raw_txoffset[ping->png_raw_rxsector[i]];
						receive_time_d = transmit_time_d + ping->png_raw_rxrange[i];

						/* merge heading from best available source */
						if (ndat_heading > 0) {
							interp_status =
							    mb_linear_interp_heading(verbose, dat_heading_time_d - 1, dat_heading_heading - 1, ndat_heading,
							                             transmit_time_d, &transmit_heading, &jheading, &error);
							interp_status =
							    mb_linear_interp_heading(verbose, dat_heading_time_d - 1, dat_heading_heading - 1, ndat_heading,
							                             receive_time_d, &receive_heading, &jheading, &error);
						}
						else {
							mb_hedint_interp(verbose, imbio_ptr, transmit_time_d, &transmit_heading, &error);
							mb_hedint_interp(verbose, imbio_ptr, receive_time_d, &receive_heading, &error);
						}
						if (transmit_heading < 0.0)
							transmit_heading += 360.0;
						else if (transmit_heading >= 360.0)
							transmit_heading -= 360.0;
						if (receive_heading < 0.0)
							receive_heading += 360.0;
						else if (receive_heading >= 360.0)
							receive_heading -= 360.0;

						/* get attitude from best available source */
						if (ndat_rph > 0) {
							interp_status = mb_linear_interp(verbose, dat_rph_time_d - 1, dat_rph_roll - 1, ndat_rph,
							                                 transmit_time_d, &transmit_roll, &jattitude, &error);
							if (interp_status == MB_SUCCESS)
								interp_status = mb_linear_interp(verbose, dat_rph_time_d - 1, dat_rph_pitch - 1, ndat_rph,
								                                 transmit_time_d, &transmit_pitch, &jattitude, &error);
							if (interp_status == MB_SUCCESS)
								interp_status = mb_linear_interp(verbose, dat_rph_time_d - 1, dat_rph_heave - 1, ndat_rph,
								                                 transmit_time_d, &transmit_heave, &jattitude, &error);
							interp_status = mb_linear_interp(verbose, dat_rph_time_d - 1, dat_rph_roll - 1, ndat_rph,
							                                 receive_time_d, &receive_roll, &jattitude, &error);
							if (interp_status == MB_SUCCESS)
								interp_status = mb_linear_interp(verbose, dat_rph_time_d - 1, dat_rph_pitch - 1, ndat_rph,
								                                 receive_time_d, &receive_pitch, &jattitude, &error);
							if (interp_status == MB_SUCCESS)
								interp_status = mb_linear_interp(verbose, dat_rph_time_d - 1, dat_rph_heave - 1, ndat_rph,
								                                 receive_time_d, &receive_heave, &jattitude, &error);
						}
						else {
							mb_attint_interp(verbose, imbio_ptr, transmit_time_d, &transmit_heave, &transmit_roll,
							                 &transmit_pitch, &error);
							mb_attint_interp(verbose, imbio_ptr, receive_time_d, &receive_heave, &receive_roll, &receive_pitch,
							                 &error);
						}

						/* use sonardepth instead of heave for submerged platforms */
						if (depthsensor_mode == MBKONSBERGPREPROCESS_ZMODE_USE_SENSORDEPTH_ONLY) {
							/* merge sonardepth from best available source */
							if (nsonardepth > 0) {
								interp_status =
								    mb_linear_interp(verbose, sonardepth_time_d - 1, sonardepth_sonardepth - 1, nsonardepth,
								                     transmit_time_d, &transmit_heave, &jsonardepth, &error);
								interp_status =
								    mb_linear_interp(verbose, sonardepth_time_d - 1, sonardepth_sonardepth - 1, nsonardepth,
								                     receive_time_d, &receive_heave, &jsonardepth, &error);
							}
							else if (ndat_sonardepth > 0) {
								interp_status =
								    mb_linear_interp(verbose, sonardepth_time_d - 1, sonardepth_sonardepth - 1, nsonardepth,
								                     transmit_time_d, &transmit_heave, &jsonardepth, &error);
								interp_status =
								    mb_linear_interp(verbose, sonardepth_time_d - 1, sonardepth_sonardepth - 1, nsonardepth,
								                     receive_time_d, &receive_heave, &jsonardepth, &error);
							}
							else {
								interp_status =
								    mb_linear_interp(verbose, sonardepth_time_d - 1, sonardepth_sonardepth - 1, nsonardepth,
								                     transmit_time_d, &transmit_heave, &jsonardepth, &error);
								interp_status =
								    mb_linear_interp(verbose, sonardepth_time_d - 1, sonardepth_sonardepth - 1, nsonardepth,
								                     receive_time_d, &receive_heave, &jsonardepth, &error);
							}
							heave = transmit_heave;
						}

						/* get ssv and range */
						if (ping->png_ssv <= 0)
							ping->png_ssv = 150;
						ping->png_range[i] = ping->png_raw_rxrange[i];

						/* ping->png_bheave[i] is the difference between the heave at the ping timestamp time that is factored
						 * into the ping->png_xducer_depth value and the average heave at the sector transmit time and the beam
						 * receive time */
						ping->png_bheave[i] = 0.5 * (receive_heave + transmit_heave) - heave;
						/* fprintf(stderr,"AAA png_count:%d beam:%d heave_ping:%f i:%d transmit_heave:%f receive_heave:%f
						bheave:%f\n", ping->png_count,i,heave_ping,i,transmit_heave,receive_heave,ping->png_bheave[i]); */

						/* also add in lever arm due to the offset between the motion sensor and
						 * the sonar */
						/* status = mb_lever(verbose,
						            rx_y,
						            rx_x,
						            rx_z,
						            position_off_y,
						            position_off_x,
						            position_off_z,
						            rollpitch_off_y,
						            rollpitch_off_x,
						            rollpitch_off_z,
						            receive_pitch,
						            receive_roll,
						            &lever_x,
						            &lever_y,
						            &lever_z,
						            &error); */
						/* fprintf(stderr,"Lever: roll:%f pitch:%f  lever: %f %f %f\n",roll,pitch,lever_x,lever_y,lever_z); */

						/* calculate beam angles for raytracing using Jon Beaudoin's code based on:
						    Beaudoin, J., Hughes Clarke, J., and Bartlett, J. Application of
						    Surface Sound Speed Measurements in Post-Processing for Multi-Sector
						    Multibeam Echosounders : International Hydrographic Review, v.5, no.3,
						    p.26-31.
						    (http://www.omg.unb.ca/omg/papers/beaudoin_IHR_nov2004.pdf).
						   note complexity if transducer arrays are reverse mounted, as determined
						   by a mount heading angle of about 180 degrees rather than about 0 degrees.
						   If a receive array or a transmit array are reverse mounted then:
						    1) subtract 180 from the heading mount angle of the array
						    2) flip the sign of the pitch and roll mount offsets of the array
						    3) flip the sign of the beam steering angle from that array
						        (reverse TX means flip sign of TX steer, reverse RX
						        means flip sign of RX steer) */
						if (tx_h <= 90.0 || tx_h >= 270.0) {
							tx_align.roll = tx_r;
							tx_align.pitch = tx_p;
							tx_align.heading = tx_h;
							tx_steer = (0.01 * (double)ping->png_raw_txtiltangle[ping->png_raw_rxsector[i]]);
						}
						else {
							tx_align.roll = -tx_r;
							tx_align.pitch = -tx_p;
							tx_align.heading = tx_h - 180.0;
							tx_steer = -(0.01 * (double)ping->png_raw_txtiltangle[ping->png_raw_rxsector[i]]);
						}
						tx_orientation.roll = transmit_roll;
						tx_orientation.pitch = transmit_pitch;
						tx_orientation.heading = transmit_heading;
						if (rx_h <= 90.0 || rx_h >= 270.0) {
							rx_align.roll = rx_r;
							rx_align.pitch = rx_p;
							rx_align.heading = rx_h;
							rx_steer = (0.01 * (double)ping->png_raw_rxpointangle[i]);
						}
						else {
							rx_align.roll = -rx_r;
							rx_align.pitch = -rx_p;
							rx_align.heading = rx_h - 180.0;
							rx_steer = -(0.01 * (double)ping->png_raw_rxpointangle[i]);
						}
						rx_orientation.roll = receive_roll;
						rx_orientation.pitch = receive_pitch;
						rx_orientation.heading = receive_heading;
						reference_heading = heading;

						status = mb_beaudoin(verbose, tx_align, tx_orientation, tx_steer, rx_align, rx_orientation, rx_steer,
						                     reference_heading, &beamAzimuth, &beamDepression, &error);
						ping->png_depression[i] = 90.0 - beamDepression;
						ping->png_azimuth[i] = 90.0 + beamAzimuth;
						if (ping->png_azimuth[i] < 0.0)
							ping->png_azimuth[i] += 360.0;
						// fprintf(stderr,"mb_beaudoin result: i:%d tx steer:%f rph: %f %f %f  rx steer:%f rph: %f %f %f  beam: %f
						// %f\n",  i,tx_steer,tx_orientation.roll,tx_orientation.pitch,tx_orientation.heading,
						// i,rx_steer,rx_orientation.roll,rx_orientation.pitch,rx_orientation.heading,
						// beamDepression,beamAzimuth);
						/* fprintf(stderr,"i:%d %f %f     %f %f\n",
						i,beamDepression,beamAzimuth,ping->png_depression[i],ping->png_azimuth[i]);*/

						/* calculate beamflag */
						detection_mask = (mb_u_char)ping->png_raw_rxdetection[i];
						if (istore->sonar == MBSYS_SIMRAD3_M3 && (ping->png_detection[i] & 128) == 128) {
							ping->png_beamflag[i] = MB_FLAG_NULL;
							ping->png_raw_rxdetection[i] = ping->png_raw_rxdetection[i] | 128;
						}
						else if ((detection_mask & 128) == 128 && (detection_mask & 112) != 0) {
							ping->png_beamflag[i] = MB_FLAG_NULL;
							/* fprintf(stderr,"beam i:%d detection_mask:%d %d quality:%u beamflag:%u\n",
							i,ping->png_raw_rxdetection[i],detection_mask,(mb_u_char)ping->png_raw_rxquality[i],(mb_u_char)ping->png_beamflag[i]);*/
						}
						else if ((detection_mask & 128) == 128) {
							ping->png_beamflag[i] = MB_FLAG_FLAG + MB_FLAG_SONAR;
							/*fprintf(stderr,"beam i:%d detection_mask:%d %d quality:%u beamflag:%u\n",
							i,ping->png_raw_rxdetection[i],detection_mask,(mb_u_char)ping->png_raw_rxquality[i],(mb_u_char)ping->png_beamflag[i]);*/
						}
						else if (ping->png_clean[i] != 0) {
							ping->png_beamflag[i] = MB_FLAG_FLAG + MB_FLAG_SONAR;
						}
						else {
							ping->png_beamflag[i] = MB_FLAG_NONE;
						}

						/* check for NaN value */
						if (isnan(ping->png_depth[i])) {
							ping->png_beamflag[i] = MB_FLAG_NULL;
							ping->png_depth[i] = 0.0;
						}
					}

					/* generate processed sidescan */
					pixel_size = (double *)&imb_io_ptr->saved1;
					swath_width = (double *)&imb_io_ptr->saved2;
					ping->png_pixel_size = 0;
					ping->png_pixels_ss = 0;
					status =
					    mbsys_simrad3_makess(verbose, imbio_ptr, istore_ptr, MB_NO, pixel_size, MB_NO, swath_width, 1, &error);
				}

				/* handle unknown data */
				else if (status == MB_SUCCESS) {
					/*fprintf(stderr,"DATA TYPE OTHER THAN SURVEY: status:%d error:%d kind:%d\n",status,error,kind);*/
				}

				/* handle read error */
				else {
					/*fprintf(stderr,"READ FAILURE: status:%d error:%d kind:%d\n",status,error,kind);*/
				}

				/* print debug statements */
				if (verbose >= 2) {
					fprintf(stderr, "\ndbg2  Ping read in program <%s>\n", program_name);
					fprintf(stderr, "dbg2       kind:           %d\n", kind);
					fprintf(stderr, "dbg2       error:          %d\n", error);
					fprintf(stderr, "dbg2       status:         %d\n", status);
				}

				/*--------------------------------------------
				  write the processed data
				  --------------------------------------------*/

				/* write some data */
				if (error == MB_ERROR_NO_ERROR) {
					status = mb_put_all(verbose, ombio_ptr, istore_ptr, MB_NO, kind, time_i, time_d, navlon, navlat, speed,
					                    heading, obeams_bath, obeams_amp, opixels_ss, beamflag, bath, amp, bathacrosstrack,
					                    bathalongtrack, ss, ssacrosstrack, ssalongtrack, comment, &error);
					if (status != MB_SUCCESS) {
						mb_error(verbose, error, &message);
						fprintf(stderr, "\nMBIO Error returned from function <mb_put>:\n%s\n", message);
						fprintf(stderr, "\nMultibeam Data Not Written To File <%s>\n", ofile);
						fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
						exit(error);
					}
				}
			}

			/* output counts */
			if (output_counts == MB_YES) {
				fprintf(stdout, "\nData records written to: %s\n", ofile);
				fprintf(stdout, "     nrec_0x30_pu_id:         %d\n", nrec_0x30_pu_id);
				fprintf(stdout, "     nrec_0x31_pu_status:          %d\n", nrec_0x31_pu_status);
				fprintf(stdout, "     nrec_0x32_pu_bist:           %d\n", nrec_0x32_pu_bist);
				fprintf(stdout, "     nrec_0x33_parameter_extra:        %d\n", nrec_0x33_parameter_extra);
				fprintf(stdout, "     nrec_0x41_attitude:               %d\n", nrec_0x41_attitude);
				fprintf(stdout, "     nrec_0x43_clock:                  %d\n", nrec_0x43_clock);
				fprintf(stdout, "     nrec_0x44_bathymetry:             %d\n", nrec_0x44_bathymetry);
				fprintf(stdout, "     nrec_0x45_singlebeam:             %d\n", nrec_0x45_singlebeam);
				fprintf(stdout, "     nrec_0x46_rawbeamF:               %d\n", nrec_0x46_rawbeamF);
				fprintf(stdout, "     nrec_0x47_surfacesoundspeed2:     %d\n", nrec_0x47_surfacesoundspeed2);
				fprintf(stdout, "     nrec_0x48_heading:                %d\n", nrec_0x48_heading);
				fprintf(stdout, "     nrec_0x49_parameter_start:        %d\n", nrec_0x49_parameter_start);
				fprintf(stdout, "     nrec_0x4A_tilt:                   %d\n", nrec_0x4A_tilt);
				fprintf(stdout, "     nrec_0x4B_echogram:               %d\n", nrec_0x4B_echogram);
				fprintf(stdout, "     nrec_0x4E_rawbeamN:               %d\n", nrec_0x4E_rawbeamN);
				fprintf(stdout, "     nrec_0x4F_quality:            v   %d\n", nrec_0x4F_quality);
				fprintf(stdout, "     nrec_0x50_pos:                    %d\n", nrec_0x50_pos);
				fprintf(stdout, "     nrec_0x52_runtime:                %d\n", nrec_0x52_runtime);
				fprintf(stdout, "     nrec_0x53_sidescan:               %d\n", nrec_0x53_sidescan);
				fprintf(stdout, "     nrec_0x54_tide:                   %d\n", nrec_0x54_tide);
				fprintf(stdout, "     nrec_0x55_svp2:                   %d\n", nrec_0x55_svp2);
				fprintf(stdout, "     nrec_0x56_svp:                    %d\n", nrec_0x56_svp);
				fprintf(stdout, "     nrec_0x57_surfacesoundspeed:      %d\n", nrec_0x57_surfacesoundspeed);
				fprintf(stdout, "     nrec_0x58_bathymetry2:            %d\n", nrec_0x58_bathymetry2);
				fprintf(stdout, "     nrec_0x59_sidescan2:              %d\n", nrec_0x59_sidescan2);
				fprintf(stdout, "     nrec_0x66_rawbeamf:               %d\n", nrec_0x66_rawbeamf);
				fprintf(stdout, "     nrec_0x68_height:                 %d\n", nrec_0x68_height);
				fprintf(stdout, "     nrec_0x69_parameter_stop:         %d\n", nrec_0x69_parameter_stop);
				fprintf(stdout, "     nrec_0x6B_water_column:           %d\n", nrec_0x6B_water_column);
				fprintf(stdout, "     nrec_0x6E_network_attitude:       %d\n", nrec_0x6E_network_attitude);
				fprintf(stdout, "     nrec_0x70_parameter:              %d\n", nrec_0x70_parameter);
				fprintf(stdout, "     nrec_0x73_surface_sound_speed:    %d\n", nrec_0x73_surface_sound_speed);
				fprintf(stdout, "     nrec_0xE1_bathymetry_mbari57:     %d\n", nrec_0xE1_bathymetry_mbari57);
				fprintf(stdout, "     nrec_0xE2_sidescan_mbari57:       %d\n", nrec_0xE2_sidescan_mbari57);
				fprintf(stdout, "     nrec_0xE3_bathymetry_mbari59:     %d\n", nrec_0xE3_bathymetry_mbari59);
				fprintf(stdout, "     nrec_0xE4_sidescan_mbari59:       %d\n", nrec_0xE4_sidescan_mbari59);
				fprintf(stdout, "     nrec_0xE5_bathymetry_mbari59:     %d\n", nrec_0xE5_bathymetry_mbari59);
			}

			nrec_0x30_pu_id_tot += nrec_0x30_pu_id;
			nrec_0x31_pu_status_tot += nrec_0x31_pu_status;
			nrec_0x32_pu_bist_tot += nrec_0x32_pu_bist;
			nrec_0x33_parameter_extra_tot += nrec_0x33_parameter_extra;
			nrec_0x41_attitude_tot += nrec_0x41_attitude;
			nrec_0x43_clock_tot += nrec_0x43_clock;
			nrec_0x44_bathymetry_tot += nrec_0x44_bathymetry;
			nrec_0x45_singlebeam_tot += nrec_0x45_singlebeam;
			nrec_0x46_rawbeamF_tot += nrec_0x46_rawbeamF;
			nrec_0x47_surfacesoundspeed2_tot += nrec_0x47_surfacesoundspeed2;
			nrec_0x48_heading_tot += nrec_0x48_heading;
			nrec_0x49_parameter_start_tot += nrec_0x49_parameter_start;
			nrec_0x4A_tilt_tot += nrec_0x4A_tilt;
			nrec_0x4B_echogram_tot += nrec_0x4B_echogram;
			nrec_0x4E_rawbeamN_tot += nrec_0x4E_rawbeamN;
			nrec_0x4F_quality_tot += nrec_0x4F_quality;
			nrec_0x50_pos_tot += nrec_0x50_pos;
			nrec_0x52_runtime_tot += nrec_0x52_runtime;
			nrec_0x53_sidescan_tot += nrec_0x53_sidescan;
			nrec_0x54_tide_tot += nrec_0x54_tide;
			nrec_0x55_svp2_tot += nrec_0x55_svp2;
			nrec_0x56_svp_tot += nrec_0x56_svp;
			nrec_0x57_surfacesoundspeed_tot += nrec_0x57_surfacesoundspeed;
			nrec_0x58_bathymetry2_tot += nrec_0x58_bathymetry2;
			nrec_0x59_sidescan2_tot += nrec_0x59_sidescan2;
			nrec_0x66_rawbeamf_tot += nrec_0x66_rawbeamf;
			nrec_0x68_height_tot += nrec_0x68_height;
			nrec_0x69_parameter_stop_tot += nrec_0x69_parameter_stop;
			nrec_0x6B_water_column_tot += nrec_0x6B_water_column;
			nrec_0x6E_network_attitude_tot += nrec_0x6E_network_attitude;
			nrec_0x70_parameter_tot += nrec_0x70_parameter;
			nrec_0x73_surface_sound_speed_tot += nrec_0x73_surface_sound_speed;
			nrec_0xE1_bathymetry_mbari57_tot += nrec_0xE1_bathymetry_mbari57;
			nrec_0xE2_sidescan_mbari57_tot += nrec_0xE2_sidescan_mbari57;
			nrec_0xE3_bathymetry_mbari59_tot += nrec_0xE3_bathymetry_mbari59;
			nrec_0xE4_sidescan_mbari59_tot += nrec_0xE4_sidescan_mbari59;
			nrec_0xE5_bathymetry_mbari59_tot += nrec_0xE5_bathymetry_mbari59;

			/* figure out whether and what to read next */
			if (read_datalist == MB_YES) {
				if ((status = mb_datalist_read(verbose, datalist, ifile, dfile, &format, &file_weight, &error)) == MB_SUCCESS)
					read_data = MB_YES;
				else
					read_data = MB_NO;
			}
			else {
				read_data = MB_NO;
			}

			/* close the input swath file */
			status = mb_close(verbose, &imbio_ptr, &error);

			/* close the output swath file if necessary */
			if (ofile_set == MB_NO || read_data == MB_NO) {
				status = mb_close(verbose, &ombio_ptr, &error);

				/* open up start and end times by two minutes */
				start_time_d -= 120.0;
				end_time_d += 120.0;

				/* output asynchronous heading output file */
				sprintf(athfile, "%s.ath", ofile);
				if ((athfp = fopen(athfile, "w")) != NULL) {
					for (i = 0; i < ndat_heading; i++) {
						if (dat_heading_time_d[i] > start_time_d && dat_heading_time_d[i] < end_time_d)
							fprintf(athfp, "%0.6f\t%7.3f\n", dat_heading_time_d[i], dat_heading_heading[i]);
					}
					fclose(athfp);
				}
				else {
					error = MB_ERROR_OPEN_FAIL;
					fprintf(stderr, "\nUnable to open asynchronous heading data file <%s> for writing\n", athfile);
					fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
					exit(error);
				}

				/* output asynchronous sonardepth output file */
				sprintf(atsfile, "%s.ats", ofile);
				if ((atsfp = fopen(atsfile, "w")) != NULL) {
					for (i = 0; i < ndat_sonardepth; i++) {
						if (dat_sonardepth_time_d[i] > start_time_d && dat_sonardepth_time_d[i] < end_time_d)
							fprintf(atsfp, "%0.6f\t%7.3f\n", dat_sonardepth_time_d[i], dat_sonardepth_sonardepth[i]);
					}
					fclose(atsfp);
				}
				else {
					error = MB_ERROR_OPEN_FAIL;
					fprintf(stderr, "\nUnable to open asynchronous sonardepth data file <%s> for writing\n", atsfile);
					fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
					exit(error);
				}

				/* output asynchronous attitude output file */
				sprintf(atafile, "%s.ata", ofile);
				if ((atafp = fopen(atafile, "w")) != NULL) {
					for (i = 0; i < ndat_rph; i++) {
						if (dat_rph_time_d[i] > start_time_d && dat_rph_time_d[i] < end_time_d)
							fprintf(atafp, "%0.6f\t%0.3f\t%0.3f\n", dat_rph_time_d[i], dat_rph_roll[i], dat_rph_pitch[i]);
					}
					fclose(atafp);
				}
				else {
					error = MB_ERROR_OPEN_FAIL;
					fprintf(stderr, "\nUnable to open asynchronous attitude data file <%s> for writing\n", atafile);
					fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
					exit(error);
				}

				/* close ats and sta files */
				fclose(stafp);

				/* generate inf fnv and fbt files */
				if (status == MB_SUCCESS) {
					status = mb_make_info(verbose, MB_YES, ofile, MBF_EM710MBA, &error);
				}
			}

			/* end loop over files in list */
		}
		if (read_datalist == MB_YES)
			mb_datalist_close(verbose, &datalist, &error);

		/* output counts */
		if (output_counts == MB_YES) {
			fprintf(stdout, "\nTotal files read:  %d\n", nfile_read);
			fprintf(stdout, "Total files written: %d\n", nfile_write);
			fprintf(stdout, "\nTotal data records written from: %s\n", read_file);
			fprintf(stdout, "     nrec_0x30_pu_id_tot:     %d\n", nrec_0x30_pu_id_tot);
			fprintf(stdout, "     nrec_0x31_pu_status_tot:      %d\n", nrec_0x31_pu_status_tot);
			fprintf(stdout, "     nrec_0x32_pu_bist_tot:       %d\n", nrec_0x32_pu_bist_tot);
			fprintf(stdout, "     nrec_0x33_parameter_extra_tot:    %d\n", nrec_0x33_parameter_extra_tot);
			fprintf(stdout, "     nrec_0x41_attitude_tot:           %d\n", nrec_0x41_attitude_tot);
			fprintf(stdout, "     nrec_0x43_clock_tot:              %d\n", nrec_0x43_clock_tot);
			fprintf(stdout, "     nrec_0x44_bathymetry_tot:         %d\n", nrec_0x44_bathymetry_tot);
			fprintf(stdout, "     nrec_0x45_singlebeam_tot:         %d\n", nrec_0x45_singlebeam_tot);
			fprintf(stdout, "     nrec_0x46_rawbeamF_tot:           %d\n", nrec_0x46_rawbeamF_tot);
			fprintf(stdout, "     nrec_0x47_surfacesoundspeed2_tot: %d\n", nrec_0x47_surfacesoundspeed2_tot);
			fprintf(stdout, "     nrec_0x48_heading_tot:            %d\n", nrec_0x48_heading_tot);
			fprintf(stdout, "     nrec_0x49_parameter_start_tot:    %d\n", nrec_0x49_parameter_start_tot);
			fprintf(stdout, "     nrec_0x4A_tilt_tot:               %d\n", nrec_0x4A_tilt_tot);
			fprintf(stdout, "     nrec_0x4B_echogram_tot:           %d\n", nrec_0x4B_echogram_tot);
			fprintf(stdout, "     nrec_0x4E_rawbeamN_tot:           %d\n", nrec_0x4E_rawbeamN_tot);
			fprintf(stdout, "     nrec_0x4F_quality_tot:            %d\n", nrec_0x4F_quality_tot);
			fprintf(stdout, "     nrec_0x50_pos_tot:                %d\n", nrec_0x50_pos_tot);
			fprintf(stdout, "     nrec_0x52_runtime_tot:            %d\n", nrec_0x52_runtime_tot);
			fprintf(stdout, "     nrec_0x53_sidescan_tot:           %d\n", nrec_0x53_sidescan_tot);
			fprintf(stdout, "     nrec_0x54_tide_tot:               %d\n", nrec_0x54_tide_tot);
			fprintf(stdout, "     nrec_0x55_svp2_tot:               %d\n", nrec_0x55_svp2_tot);
			fprintf(stdout, "     nrec_0x56_svp_tot:                %d\n", nrec_0x56_svp_tot);
			fprintf(stdout, "     nrec_0x57_surfacesoundspeed_tot:  %d\n", nrec_0x57_surfacesoundspeed_tot);
			fprintf(stdout, "     nrec_0x58_bathymetry2_tot:        %d\n", nrec_0x58_bathymetry2_tot);
			fprintf(stdout, "     nrec_0x59_sidescan2_tot:          %d\n", nrec_0x59_sidescan2_tot);
			fprintf(stdout, "     nrec_0x66_rawbeamf_tot:           %d\n", nrec_0x66_rawbeamf_tot);
			fprintf(stdout, "     nrec_0x68_height_tot:             %d\n", nrec_0x68_height_tot);
			fprintf(stdout, "     nrec_0x69_parameter_stop_tot:     %d\n", nrec_0x69_parameter_stop_tot);
			fprintf(stdout, "     nrec_0x6B_water_column_tot:       %d\n", nrec_0x6B_water_column_tot);
			fprintf(stdout, "     nrec_0x6E_network_attitude_tot:   %d\n", nrec_0x6E_network_attitude_tot);
			fprintf(stdout, "     nrec_0x70_parameter_tot:          %d\n", nrec_0x70_parameter_tot);
			fprintf(stdout, "     nrec_0x73_surface_sound_speed_tot:%d\n", nrec_0x73_surface_sound_speed_tot);
			fprintf(stdout, "     nrec_0xE1_bathymetry_mbari57_tot: %d\n", nrec_0xE1_bathymetry_mbari57_tot);
			fprintf(stdout, "     nrec_0xE2_sidescan_mbari57_tot:   %d\n", nrec_0xE2_sidescan_mbari57_tot);
			fprintf(stdout, "     nrec_0xE3_bathymetry_mbari59_tot: %d\n", nrec_0xE3_bathymetry_mbari59_tot);
			fprintf(stdout, "     nrec_0xE4_sidescan_mbari59_tot:   %d\n", nrec_0xE4_sidescan_mbari59_tot);
			fprintf(stdout, "     nrec_0xE5_bathymetry_mbari59_tot: %d\n", nrec_0xE5_bathymetry_mbari59_tot);
		}
	}

	/* deallocate navigation arrays */
	if (ndat_nav > 0) {
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_nav_time_d, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_nav_lon, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_nav_lat, &error);
	}
	if (ndat_heading > 0) {
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_heading_time_d, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_heading_heading, &error);
	}
	if (ndat_rph > 0) {
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_rph_time_d, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_rph_roll, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_rph_pitch, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_rph_heave, &error);
	}
	if (ndat_sonardepth > 0) {
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_sonardepth_time_d, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_sonardepth_sonardepth, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&dat_sonardepth_sonardepthfilter, &error);
	}
	if (ntimelag > 0) {
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&timelag_time_d, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&timelag_model, &error);
	}
	if (nsonardepth > 0) {
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&sonardepth_time_d, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&sonardepth_sonardepth, &error);
		status = mb_freed(verbose, __FILE__, __LINE__, (void **)&sonardepth_sonardepthfilter, &error);
	}

	/* check memory */
	if (verbose >= 4)
		status = mb_memory_list(verbose, &error);

	/* print output debug statements */
	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
		fprintf(stderr, "dbg2  Ending status:\n");
		fprintf(stderr, "dbg2       status:  %d\n", status);
	}

	/* end it all */
	exit(error);
}
/*--------------------------------------------------------------------*/
