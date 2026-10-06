/*--------------------------------------------------------------------
 *    The MB-system:	mb_gmt_simrad.c
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/* Simrad -> Simrad2 store translation shared by the GMT modules; see mb_gmt_simrad.h.
   The code is the mbcopy module's (mbcopy.cc's mbcopy_simrad_to_simrad2), unchanged. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_gmt_simrad.h"
#include "mb_define.h"
#include "mb_io.h"
#include "mb_status.h"

/*--------------------------------------------------------------------*/
static int mbcopy_simrad_time_convert(struct GMTAPI_CTRL *API, int verbose,
									  int year, int month, int day, int hour, int minute, int second, int centisecond,
									  int *date, int *msec, int *error) {
	if (verbose >= 2) {
		GMT_Report(API, GMT_MSG_NORMAL, "\ndbg2  MBcopy function <%s> called\n", __func__);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2  Input arguments:\n");
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       verbose:    %d\n", verbose);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       year:       %d\n", year);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       month:      %d\n", month);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       day:        %d\n", day);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       hour:       %d\n", hour);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       minute:     %d\n", minute);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       second:     %d\n", second);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       centisecond:%d\n", centisecond);
	}

	/* get time */
	int time_i[7];
	mb_fix_y2k(verbose, year, &time_i[0]);
	time_i[1] = month;
	time_i[2] = day;
	time_i[3] = hour;
	time_i[4] = minute;
	time_i[5] = second;
	time_i[6] = 10000 * centisecond;
	*date = 10000 * time_i[0] + 100 * time_i[1] + time_i[2];
	*msec = 3600000 * time_i[3] + 60000 * time_i[4] + 1000 * time_i[5] + 0.001 * time_i[6];

	const int status = MB_SUCCESS;

	if (verbose >= 2) {
		GMT_Report(API, GMT_MSG_NORMAL, "\ndbg2  MBcopy function <%s> completed\n", __func__);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2  Return values:\n");
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       date:       %d\n", *date);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       msec:       %d\n", *msec);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       error:      %d\n", *error);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2  Return status:\n");
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       status:     %d\n", status);
	}

	return (status);
}
/*--------------------------------------------------------------------*/
int mb_gmt_simrad_to_simrad2(struct GMTAPI_CTRL *API, int verbose,
									struct mbsys_simrad_struct *istore,
									struct mbsys_simrad2_struct *ostore, int *error) {
	if (verbose >= 2) {
		GMT_Report(API, GMT_MSG_NORMAL, "\ndbg2  MBcopy function <%s> called\n", __func__);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2  Input arguments:\n");
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       verbose:    %d\n", verbose);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       istore:     %p\n", (void *)istore);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       ostore:     %p\n", (void *)ostore);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       kind:       %d\n", istore->kind);
	}

	int status = MB_SUCCESS;
	struct mbsys_simrad_survey_struct *iping;
	struct mbsys_simrad2_ping_struct *oping;
	double *angles_simrad;
	double bath_offset;
	double alpha, beta, theta, phi;
	int istep = 0;

	/* copy the data  */
	if (istore != NULL && ostore != NULL && (void *)istore != (void *)ostore) {
		/* type of data record */
		ostore->kind = istore->kind;
		ostore->type = EM2_NONE;
		if (istore->kind == MB_DATA_DATA)
			ostore->type = EM2_BATH;
		else if (istore->kind == MB_DATA_COMMENT)
			ostore->type = EM2_START;
		else if (istore->kind == MB_DATA_START)
			ostore->type = EM2_START;
		else if (istore->kind == MB_DATA_STOP)
			ostore->type = EM2_STOP2;
		else if (istore->kind == MB_DATA_NAV)
			ostore->type = EM2_POS;
		else if (istore->kind == MB_DATA_VELOCITY_PROFILE)
			ostore->type = EM2_SVP;
		if (istore->sonar == MBSYS_SIMRAD_EM12S)
			ostore->sonar = MBSYS_SIMRAD2_EM12S;
		else if (istore->sonar == MBSYS_SIMRAD_EM12D)
			ostore->sonar = MBSYS_SIMRAD2_EM12D;
		else if (istore->sonar == MBSYS_SIMRAD_EM1000)
			ostore->sonar = MBSYS_SIMRAD2_EM1000;
		else if (istore->sonar == MBSYS_SIMRAD_EM121)
			ostore->sonar = MBSYS_SIMRAD2_EM121;

		/* time stamp */
		mbcopy_simrad_time_convert(API, verbose, istore->year, istore->month, istore->day, istore->hour, istore->minute,
								   istore->second, istore->centisecond, &ostore->date, &ostore->msec, error);

		/* installation parameter values */
		ostore->par_date = 0; /* installation parameter date = year*10000 + month*100 + day
					  Feb 26, 1995 = 19950226 */
		ostore->par_msec = 0; /* installation parameter time since midnight in msec
					  08:12:51.234 = 29570234 */
		mbcopy_simrad_time_convert(API, verbose, istore->par_year, istore->par_month, istore->par_day, istore->par_hour,
								   istore->par_minute, istore->par_second, istore->par_centisecond, &ostore->par_date,
								   &ostore->par_msec, error);
		ostore->par_line_num = istore->survey_line; /* survey line number */
		ostore->par_serial_1 = 0;                   /* system 1 serial number */
		ostore->par_serial_2 = 0;                   /* system 2 serial number */
		ostore->par_wlz = 0.0;                      /* water line vertical location (m) */
		ostore->par_smh = 0;                        /* system main head serial number */
		if (istore->sonar == MBSYS_SIMRAD_EM100) {
			ostore->par_s1z = istore->em100_td; /* transducer 1 vertical location (m) */
			ostore->par_s1x = istore->em100_tx; /* transducer 1 along location (m) */
			ostore->par_s1y = istore->em100_ty; /* transducer 1 athwart location (m) */
		}
		else if (istore->sonar == MBSYS_SIMRAD_EM1000) {
			ostore->par_s1z = istore->em1000_td; /* transducer 1 vertical location (m) */
			ostore->par_s1x = istore->em1000_tx; /* transducer 1 along location (m) */
			ostore->par_s1y = istore->em1000_ty; /* transducer 1 athwart location (m) */
		}
		else {
			ostore->par_s1z = istore->em12_td; /* transducer 1 vertical location (m) */
			ostore->par_s1x = istore->em12_tx; /* transducer 1 along location (m) */
			ostore->par_s1y = istore->em12_ty; /* transducer 1 athwart location (m) */
		}
		ostore->par_s1h = istore->heading_offset; /* transducer 1 heading (deg) */
		ostore->par_s1r = istore->roll_offset;    /* transducer 1 roll (m) */
		ostore->par_s1p = istore->pitch_offset;   /* transducer 1 pitch (m) */
		ostore->par_s1n = 0;                      /* transducer 1 number of modules */
		ostore->par_s2z = 0.0;                    /* transducer 2 vertical location (m) */
		ostore->par_s2x = 0.0;                    /* transducer 2 along location (m) */
		ostore->par_s2y = 0.0;                    /* transducer 2 athwart location (m) */
		ostore->par_s2h = 0.0;                    /* transducer 2 heading (deg) */
		ostore->par_s2r = 0.0;                    /* transducer 2 roll (m) */
		ostore->par_s2p = 0.0;                    /* transducer 2 pitch (m) */
		ostore->par_s2n = 0;                      /* transducer 2 number of modules */
		ostore->par_go1 = 0.0;                    /* system (sonar head 1) gain offset */
		ostore->par_go2 = 0.0;                    /* sonar head 2 gain offset */
		for (int i = 0; i < 16; i++) {
			ostore->par_tsv[i] = '\0';
			ostore->par_rsv[i] = '\0';
			ostore->par_bsv[i] = '\0';
			ostore->par_psv[i] = '\0';
			ostore->par_osv[i] = '\0';
		}
		ostore->par_dsd = 0.0;
		ostore->par_dso = 0.0;
		ostore->par_dsf = 0.0;
		ostore->par_dsh[0] = 'I';
		ostore->par_dsh[1] = 'N';
		ostore->par_aps = 0;
		ostore->par_p1m = 0;
		ostore->par_p1t = 0;
		ostore->par_p1z = 0.0;
		ostore->par_p1x = 0.0;
		ostore->par_p1y = 0.0;
		ostore->par_p1d = istore->pos_delay;
		for (int i = 0; i < 16; i++) {
			ostore->par_p1g[i] = '\0';
		}
		ostore->par_p2m = 0;
		ostore->par_p2t = 0;
		ostore->par_p2z = 0.0;
		ostore->par_p2x = 0.0;
		ostore->par_p2y = 0.0;
		ostore->par_p2d = 0.0;
		for (int i = 0; i < 16; i++) {
			ostore->par_p2g[i] = '\0';
		}
		ostore->par_p3m = 0;
		ostore->par_p3t = 0;
		ostore->par_p3z = 0.0;
		ostore->par_p3x = 0.0;
		ostore->par_p3y = 0.0;
		ostore->par_p3d = 0.0;
		for (int i = 0; i < 16; i++) {
			ostore->par_p3g[i] = '\0';
		}
		ostore->par_msz = 0.0;
		ostore->par_msx = 0.0;
		ostore->par_msy = 0.0;
		ostore->par_mrp[0] = 'H';
		ostore->par_mrp[1] = 'O';
		ostore->par_msd = 0.0;
		ostore->par_msr = 0.0;
		ostore->par_msp = 0.0;
		ostore->par_msg = 0.0;
		ostore->par_gcg = 0.0;
		for (int i = 0; i < 4; i++) {
			ostore->par_cpr[i] = '\0';
		}
		for (int i = 0; i < MBSYS_SIMRAD2_COMMENT_LENGTH; i++) {
			ostore->par_rop[i] = '\0';
			ostore->par_sid[i] = '\0';
			ostore->par_pll[i] = '\0';
			ostore->par_com[i] = '\0';
		}

		/* runtime parameter values */
		ostore->run_date = 0;
		ostore->run_msec = 0;
		ostore->run_ping_count = 0;
		ostore->run_serial = 0;
		ostore->run_status = 0;
		ostore->run_mode = 0;
		ostore->run_filter_id = 0;
		ostore->run_min_depth = 0;
		ostore->run_max_depth = 0;
		ostore->run_absorption = 0;

		ostore->run_tran_pulse = 0;
		if (istore->sonar == MBSYS_SIMRAD_EM12S || istore->sonar == MBSYS_SIMRAD_EM12D)
			ostore->run_tran_beam = 17;
		else if (istore->sonar == MBSYS_SIMRAD_EM1000)
			ostore->run_tran_beam = 33;
		else if (istore->sonar == MBSYS_SIMRAD_EM121)
			ostore->run_tran_beam = 10;
		ostore->run_tran_pow = 0;
		if (istore->sonar == MBSYS_SIMRAD_EM12S || istore->sonar == MBSYS_SIMRAD_EM12D)
			ostore->run_rec_beam = 35;
		else if (istore->sonar == MBSYS_SIMRAD_EM1000)
			ostore->run_rec_beam = 33;
		else if (istore->sonar == MBSYS_SIMRAD_EM121)
			ostore->run_rec_beam = 10;
		ostore->run_rec_band = 0;
		ostore->run_rec_gain = 0;
		ostore->run_tvg_cross = 0;
		ostore->run_ssv_source = 0;
		ostore->run_max_swath = 0;
		ostore->run_beam_space = 0;
		ostore->run_swath_angle = 0;
		ostore->run_stab_mode = 0;
		for (int i = 0; i < 4; i++) {
			ostore->run_spare[i] = '\0';
		}

		/* sound velocity profile */
		ostore->svp_use_date = 0;
		ostore->svp_use_msec = 0;
		mbcopy_simrad_time_convert(API, verbose, istore->svp_year, istore->svp_month, istore->svp_day, istore->svp_hour,
								   istore->svp_minute, istore->svp_second, istore->svp_centisecond, &ostore->svp_use_date,
								   &ostore->svp_use_msec, error);
		ostore->svp_count = 0;
		ostore->svp_serial = 0;
		ostore->svp_origin_date = 0;
		ostore->svp_origin_msec = 0;
		ostore->svp_num = istore->svp_num;
		ostore->svp_depth_res = 100;
		for (int i = 0; i < MBSYS_SIMRAD_MAXSVP; i++) {
			ostore->svp_depth[i] = istore->svp_depth[i];
			ostore->svp_vel[i] = istore->svp_vel[i];
		}

		/* position */
		ostore->pos_date = 0;
		ostore->pos_msec = 0;
		mbcopy_simrad_time_convert(API, verbose, istore->pos_year, istore->pos_month, istore->pos_day, istore->pos_hour,
								   istore->pos_minute, istore->pos_second, istore->pos_centisecond, &ostore->pos_date,
								   &ostore->pos_msec, error);
		ostore->pos_count = 0;
		ostore->pos_serial = 0;
		ostore->pos_latitude = 20000000 * istore->pos_latitude;
		ostore->pos_longitude = 10000000 * istore->pos_longitude;
		ostore->pos_quality = 0;
		ostore->pos_speed = (int)(istore->speed / 0.036);
		ostore->pos_course = 0xFFFF;
		ostore->pos_heading = (int)(istore->line_heading * 100);
		ostore->pos_system = 129; /* don't use istore->pos_type; setting pos_system=129 makes nav system1 the active sensor */
		ostore->pos_input_size = 0;
		for (int i = 0; i < 256; i++) {
			ostore->pos_input[i] = 0;
		}

		/* height */
		ostore->hgt_date = 0;
		ostore->hgt_msec = 0;
		ostore->hgt_count = 0;
		ostore->hgt_serial = 0;
		ostore->hgt_height = 0;
		ostore->hgt_type = 0;

		/* tide */
		ostore->tid_date = 0;
		ostore->tid_msec = 0;
		ostore->tid_count = 0;
		ostore->tid_serial = 0;
		ostore->tid_origin_date = 0;
		ostore->tid_origin_msec = 0;
		ostore->tid_tide = 0;

		/* clock */
		ostore->clk_date = 0;
		ostore->clk_msec = 0;
		ostore->clk_count = 0;
		ostore->clk_serial = 0;
		ostore->clk_origin_date = 0;
		ostore->clk_origin_msec = 0;
		ostore->clk_1_pps_use = 0;

		/* allocate memory for data structure if needed */
		if (istore->kind == MB_DATA_DATA && ostore->ping == NULL)
			status = mb_mallocd(verbose, __FILE__, __LINE__, sizeof(struct mbsys_simrad2_ping_struct),
								(void **)&(ostore->ping), error);

		if (istore->kind == MB_DATA_DATA && istore->ping != NULL && ostore->ping != NULL) {
			/* get data structure pointer */
			iping = (struct mbsys_simrad_survey_struct *)istore->ping;
			oping = (struct mbsys_simrad2_ping_struct *)ostore->ping;

			/* set beam widths for EM121 */
			if (istore->sonar == MBSYS_SIMRAD_EM121) {
				if (iping->bath_mode == 3) {
					ostore->run_tran_beam = 40;
					ostore->run_rec_beam = 40;
				}
				else if (iping->bath_mode == 2) {
					ostore->run_tran_beam = 20;
					ostore->run_rec_beam = 20;
				}
				else {
					ostore->run_tran_beam = 10;
					ostore->run_rec_beam = 10;
				}
			}

			/* initialize everything */
			oping->png_date = ostore->date;
			oping->png_msec = ostore->msec;
			oping->png_count = iping->ping_number;
			oping->png_serial = iping->swath_id;
			oping->png_latitude = 20000000 * iping->latitude;
			oping->png_longitude = 10000000 * iping->longitude;
			oping->png_speed = 0xFFFF;
			if (ostore->sonar == MBSYS_SIMRAD2_EM121)
				oping->png_heading = iping->heading;
			else
				oping->png_heading = 10 * iping->heading;
			oping->png_ssv = iping->sound_vel;
			oping->png_xducer_depth = iping->ping_heave + (int)(100 * ostore->par_s1z);
			bath_offset = 0.01 * oping->png_xducer_depth;
			/* transmit transducer depth (0.01 m)
				- The transmit transducer depth plus the
				depth offset multiplier times 65536 cm
				should be added to the beam depths to
				derive the depths re the water line.
				The depth offset multiplier will usually
				be zero, except when the EM3000 sonar
				head is on an underwater vehicle at a
				depth greater than about 650 m. Note that
				the offset multiplier will be negative
				(-1) if the actual heave is large enough
				to bring the transmit transducer above
				the water line. */
			if (oping->png_xducer_depth > 0)
				oping->png_offset_multiplier = 0;
			else {
				oping->png_offset_multiplier = -1;
				oping->png_xducer_depth = oping->png_xducer_depth + 65536;
			}

			/* beam data */
			oping->png_nbeams_max = iping->beams_bath;
			oping->png_nbeams = iping->beams_bath;
			if ((ostore->sonar == MBSYS_SIMRAD2_EM12S || ostore->sonar == MBSYS_SIMRAD2_EM12D) && iping->bath_res == 1) {
				oping->png_depth_res = 10;
				oping->png_distance_res = 20;
				oping->png_sample_rate = 5000;
			}
			else if ((ostore->sonar == MBSYS_SIMRAD2_EM12S || ostore->sonar == MBSYS_SIMRAD2_EM12D) && iping->bath_res == 2) {
				oping->png_depth_res = 20;
				oping->png_distance_res = 50;
				oping->png_sample_rate = 1250;
			}
			else if (ostore->sonar == MBSYS_SIMRAD2_EM1000) {
				oping->png_depth_res = 2;
				oping->png_distance_res = 10;
				oping->png_sample_rate = 20000;
			}
			else if (ostore->sonar == MBSYS_SIMRAD2_EM121) {
				oping->png_depth_res = iping->depth_res;
				oping->png_distance_res = iping->across_res;
				oping->png_sample_rate = (int)(1.0 / (0.0001 * iping->range_res));
			}

			/* get predefined table of beam angles using sonar type and operational mode */
			bool interleave = false;
			int nbeams = 0;
			mbsys_simrad_beamangles(verbose, (void *)istore,
									&interleave, &nbeams, &angles_simrad, error);

			if (*error == MB_ERROR_NO_ERROR && nbeams > 0 && angles_simrad != NULL && oping->png_nbeams <= nbeams) {
				/* if interleaved get center beam */
				if (interleave) {
					if (iping->bath_mode == 12 && abs(iping->bath_acrosstrack[28]) < abs(iping->bath_acrosstrack[29]))
						istep = 1;
					else if (iping->bath_mode == 13 && abs(iping->bath_acrosstrack[31]) < abs(iping->bath_acrosstrack[30]))
						istep = 1;
					else if (oping->png_nbeams > 1 && abs(iping->bath_acrosstrack[oping->png_nbeams / 2 - 1]) <
							 abs(iping->bath_acrosstrack[oping->png_nbeams / 2]))
						istep = 1;
					else
						istep = 0;
				}

				/* set beam values */
				for (int i = 0; i < oping->png_nbeams; i++) {
					oping->png_depth[i] = (int)((unsigned short)iping->bath[i]);
					if (oping->png_depth[i] != 0)
						oping->png_depth[i] -= (int)(bath_offset / (0.01 * oping->png_depth_res));
					oping->png_acrosstrack[i] = iping->bath_acrosstrack[i];
					oping->png_alongtrack[i] = iping->bath_alongtrack[i];

					alpha = 0.01 * iping->pitch;
					if (istore->sonar == MBSYS_SIMRAD_EM1000 && iping->bath_mode == 13) {
						beta = 90.0 - angles_simrad[2 * nbeams - 1 - (2 * i + istep)];
					}
					else if (istore->sonar == MBSYS_SIMRAD_EM1000 && interleave) {
						beta = 90.0 + angles_simrad[2 * i + istep];
					}
					else if (istore->sonar == MBSYS_SIMRAD_EM1000) {
						beta = 90.0 + angles_simrad[i];
					}
					else {
						beta = 90.0 + angles_simrad[i];
					}
					mb_rollpitch_to_takeoff(verbose, alpha, beta, &theta, &phi, error);
					oping->png_depression[i] = (int)(100 * (90.0 - theta));
					oping->png_azimuth[i] = (int)(100 * (90.0 - phi));
					if (oping->png_azimuth[i] < 0)
						oping->png_azimuth[i] += 36000;
					oping->png_range[i] = iping->tt[i];
					oping->png_quality[i] = iping->quality[i];
					oping->png_window[i] = 0;
					oping->png_amp[i] = iping->amp[i];
					oping->png_beam_num[i] = i + 1;
					if (iping->bath[i] > 0)
						oping->png_beamflag[i] = MB_FLAG_NONE;
					else
						oping->png_beamflag[i] = MB_FLAG_NULL;
				}

				/* raw travel time and angle data */
				oping->png_raw1_read = false;
				oping->png_raw2_read = false;
				oping->png_raw_nbeams = 0;

				/* get raw pixel size to be stored in oping->png_max_range */
				if (iping->pixels_ssraw > 0)
					oping->png_ss_read = true;
				else
					oping->png_ss_read = false;
				oping->png_ss_date = oping->png_date;
				oping->png_ss_msec = oping->png_msec;
				if (istore->sonar == MBSYS_SIMRAD_EM12D || istore->sonar == MBSYS_SIMRAD_EM12S ||
					istore->sonar == MBSYS_SIMRAD_EM121) {
					if (iping->ss_mode == 1)
						oping->png_max_range = 60;
					else if (iping->ss_mode == 2)
						oping->png_max_range = 240;
					else if (iping->bath_mode == 1 || iping->bath_mode == 3)
						oping->png_max_range = 60;
					else
						oping->png_max_range = 240;
				}
				else if (istore->sonar == MBSYS_SIMRAD_EM1000) {
					if (iping->ss_mode == 3)
						oping->png_max_range = 30;
					else if (iping->ss_mode == 4)
						oping->png_max_range = 30;
					else if (iping->ss_mode == 5)
						oping->png_max_range = 15;
					else
						oping->png_max_range = 15;
				}

				/* sidescan */
				oping->png_r_zero = 0;
				oping->png_r_zero_corr = 0;
				oping->png_tvg_start = 0;
				oping->png_tvg_stop = 0;
				oping->png_bsn = 0;
				oping->png_bso = 0;
				if (ostore->sonar == MBSYS_SIMRAD2_EM121)
					oping->png_tx = 10 * iping->beam_width;
				else if (ostore->sonar == MBSYS_SIMRAD2_EM12S || ostore->sonar == MBSYS_SIMRAD2_EM12D)
					oping->png_tx = 17;
				else if (ostore->sonar == MBSYS_SIMRAD2_EM1000)
					oping->png_tx = 33;
				oping->png_tvg_crossover = 0;
				oping->png_nbeams_ss = oping->png_nbeams;
				oping->png_npixels = iping->pixels_ssraw;
				for (int i = 0; i < oping->png_nbeams_ss; i++) {
					oping->png_beam_index[i] = i;
					oping->png_sort_direction[i] = 0;
					oping->png_beam_samples[i] = iping->beam_samples[i];
					oping->png_start_sample[i] = iping->beam_start_sample[i];
					oping->png_center_sample[i] = iping->beam_center_sample[i];
				}
				for (int i = 0; i < oping->png_npixels; i++) {
					oping->png_ssraw[i] = iping->ssraw[i];
				}
				oping->png_pixel_size = iping->pixel_size;
				oping->png_pixels_ss = iping->pixels_ss;
				for (int i = 0; i < oping->png_pixels_ss; i++) {
					if (iping->ss[i] != 0) {
						oping->png_ss[i] = iping->ss[i];
						oping->png_ssalongtrack[i] = iping->ssalongtrack[i];
					}
					else {
						oping->png_ss[i] = EM2_INVALID_AMP;
						oping->png_ssalongtrack[i] = EM2_INVALID_AMP;
					}
				}
			}
		}
	}

	if (verbose >= 2) {
		GMT_Report(API, GMT_MSG_NORMAL, "\ndbg2  MBcopy function <%s> completed\n", __func__);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2  Return values:\n");
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       error:      %d\n", *error);
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2  Return status:\n");
		GMT_Report(API, GMT_MSG_NORMAL, "dbg2       status:     %d\n", status);
	}

	return (status);
}
