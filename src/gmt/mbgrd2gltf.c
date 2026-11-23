/*--------------------------------------------------------------------
 *    The MB-system:  mbgrd2gltf.c
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 *    MBgrd2gltf converts a GMT GRD bathymetry grid into a glTF/GLB 3D model,
 *    with optional Draco compression, vertical exaggeration and an HTML viewer.
 *
 *    The program MBgrd2gltf was created by a Capstone Project team at the
 *    California State University Monterey Bay (CSUMB) including Kyle Dowling,
 *    Julian Fortin, Jesse Benavides, Nicolas Porras Falconio. This team was
 *    mentored by Mike McCann, Monterey Bay Aquarium Research Institute.
 *
 *    GMT module port of src/mbgrd2gltf/main.cpp. The driver below is that
 *    file's main() rewritten in C against the GMT option parser and the GMT
 *    reporting API; the conversion pipeline is reached through the plain C
 *    interface in src/mbgrd2gltf/mbgrd2gltf_capi.h, in the same arrangement
 *    used for mbmesh.
 *
 *    Option mapping. GMT has no long-option keyword dictionary for an
 *    out-of-tree module, so the long forms of the original, such as --input,
 *    --no-draco, --draco-precision and --geoviewpoint, are resolved here
 *    before GMT_Create_Options() sees the command line. Every option the
 *    original accepts is accepted, and the --quantize-* and -Q options that
 *    it has replaced are refused with its message.
 */

#define THIS_MODULE_NAME    "mbgrd2gltf"
#define THIS_MODULE_LIB     "mbsystem"
#define THIS_MODULE_PURPOSE "Convert a bathymetry grid to a glTF/GLB 3D model"
/* -I names the input grid file. It is an optional input, not a required one:
 * mbgrd2gltf reads the grid file itself with netCDF and cannot take a grid in
 * memory, and as a required input an external API such as Julia, Python or
 * MATLAB would demand one whenever -I is not given literally, as in -H, --help
 * or --input=FILE. There is deliberately no output key: mbgrd2gltf writes the
 * .glb/.gltf and the optional pages itself and hands nothing back through the
 * API. */
#define THIS_MODULE_KEYS    "IG("
#define THIS_MODULE_NEEDS   ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "mb_define.h"
#include "mb_status.h"

#include "mbgrd2gltf_capi.h"

/* ======================================================================================================== */

#define MBGRD2GLTF_ERROR_MAXLINE   1024
#define MBGRD2GLTF_MESSAGE_MAXLINE 2048

/* The original driver logs through the mbgrd2gltf Logger, whose output the
 * HTML viewer embeds for provenance. Keep using it so that the provenance
 * block still records the run, and mirror each line to GMT so the module
 * obeys -V like every other GMT module. */
static void log_info(struct GMTAPI_CTRL *API, const char *message) {
	mbgrd2gltf_logger_info(message);
	GMT_Report(API, GMT_MSG_INFORMATION, "%s\n", message);
}

/* ======================================================================================================== */

struct MBGRD2GLTF_CTRL {
	struct mbgrd2gltf_options options;
};

static void *New_mbgrd2gltf_Ctrl(struct GMT_CTRL *GMT) {
	struct MBGRD2GLTF_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBGRD2GLTF_CTRL);
	mbgrd2gltf_options_defaults(&Ctrl->options);
	return Ctrl;
}

static void Free_mbgrd2gltf_Ctrl(struct GMT_CTRL *GMT, struct MBGRD2GLTF_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

/* Port of print_help(). The text is unchanged. */
static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;

	GMT_Message(API, GMT_TIME_NONE,
	            "MBgrd2gltf converts a bathymetry grid in GMT GRD format into a glTF or GLB\n"
	            "3D model, with Draco mesh compression, vertical exaggeration and colors by\n"
	            "height.\n\n"
	            "The output mesh vertices are positioned in an Earth-Centered, Earth-Fixed\n"
	            "(ECEF) Cartesian coordinate system with units in meters. ECEF is a 3D\n"
	            "right-handed coordinate system with its origin at Earth's center of mass.\n"
	            "A GeoOrigin can be specified to improve rendering precision for localized areas.\n\n");
	GMT_Message(API, GMT_TIME_NONE, "usage: mbgrd2gltf --input FILE [OPTIONS]\n");
	GMT_Message(API, GMT_TIME_NONE, "       mbgrd2gltf -I FILE [OPTIONS]  (legacy style)\n\n");
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;

	GMT_Message(API, GMT_TIME_NONE, "Options:\n");
	GMT_Message(API, GMT_TIME_NONE, "  --input, -I FILE              Input GMT GRD format bathymetry grid file (required)\n");
	GMT_Message(API, GMT_TIME_NONE, "  --no-binary, -A               Write glTF (.gltf, JSON with the buffers embedded) in place of the binary glTF (.glb)\n");
	GMT_Message(API, GMT_TIME_NONE, "                                that is written by default; --binary, -B, -Z are accepted and do nothing\n");
	GMT_Message(API, GMT_TIME_NONE, "  --output, -O ROOT             Output file root [default: input file basename]\n");
	GMT_Message(API, GMT_TIME_NONE, "  --exaggeration, -E NUM        Vertical exaggeration factor, applied about the grid's mean altitude [default: 1.0]\n");
	GMT_Message(API, GMT_TIME_NONE, "  --geoorigin[=LON,LAT,EL]      GeoOrigin for high-precision local coordinates\n");
	GMT_Message(API, GMT_TIME_NONE, "  -G[LON,LAT,EL]                Short form; the value follows -G with no space\n");
	GMT_Message(API, GMT_TIME_NONE, "                                  With values: use specified lon,lat,elev\n");
	GMT_Message(API, GMT_TIME_NONE, "                                  Without values: use grid center and mean altitude\n");
	GMT_Message(API, GMT_TIME_NONE, "                                  Not specified: original ECEF coordinates (default)\n");
	GMT_Message(API, GMT_TIME_NONE, "                                GLB positions are 32-bit floats, so absolute ECEF (magnitudes of about\n");
	GMT_Message(API, GMT_TIME_NONE, "                                6.4 million meters) resolves only about 0.5 m; use a GeoOrigin to\n");
	GMT_Message(API, GMT_TIME_NONE, "                                preserve sub-meter detail\n");
	GMT_Message(API, GMT_TIME_NONE, "  --no-draco, -N                Write the mesh as arrays. By default the mesh is compressed with Draco\n");
	GMT_Message(API, GMT_TIME_NONE, "                                (KHR_draco_mesh_compression), as mbmesh does; --draco, -D are accepted and do nothing\n");
	GMT_Message(API, GMT_TIME_NONE, "  --draco-precision, -T NUM     With Draco, keep positions to this many meters [default: 0.001]: each block (a primitive\n");
	GMT_Message(API, GMT_TIME_NONE, "                                of the file) gets the bits that make the step of its quantization no coarser, between 8 and 30\n");
	GMT_Message(API, GMT_TIME_NONE, "  --draco-normal-bits, -L NUM   With Draco, the bits of each normal, 2-30 [default: 10]; the mesh has no normals\n");
	GMT_Message(API, GMT_TIME_NONE, "                                today, so this changes nothing\n");
	GMT_Message(API, GMT_TIME_NONE, "  --geoviewpoint, -S VALUES     With --html or --cesium-html, add a GeoViewpoint to the page; can be repeated. The values are\n");
	GMT_Message(API, GMT_TIME_NONE, "                                geodetic, in the order of --geoorigin: lon,lat,elev,clon,clat,celev, the camera position\n");
	GMT_Message(API, GMT_TIME_NONE, "                                and the point it looks at, or lon,lat,elev,ax,ay,az,angle,clon,clat,celev, with the\n");
	GMT_Message(API, GMT_TIME_NONE, "                                orientation as an axis and an angle. Heights are above the ellipsoid. The Copy GeoViewpoint\n");
	GMT_Message(API, GMT_TIME_NONE, "                                button of the page gives the ten-number form\n");
	GMT_Message(API, GMT_TIME_NONE, "  --no-colors, -M               Do not color the vertices by height. By default every vertex has a color (COLOR_0) from the\n");
	GMT_Message(API, GMT_TIME_NONE, "                                Haxby table, and the pages have a \"color by height\" check box\n");
	GMT_Message(API, GMT_TIME_NONE, "  --html, -W                    Generate HTML viewer with inline glTF/GLB model and print the command to view it\n");
	GMT_Message(API, GMT_TIME_NONE, "  --cesium-html, -C             Also write a viewer page for CesiumJS, <output>_cesium.html beside the model\n");
	GMT_Message(API, GMT_TIME_NONE, "  --cesium-url, -F URL          The directory of the CesiumJS build of the --cesium-html page, with Cesium.js and\n");
	GMT_Message(API, GMT_TIME_NONE, "                                Widgets/widgets.css in it [https://cesium.com/downloads/cesiumjs/releases/1.114/Build/Cesium]\n");
	GMT_Message(API, GMT_TIME_NONE, "  --verbose, -V                 Enable verbose output\n");
	GMT_Message(API, GMT_TIME_NONE, "  --help, -H                    Print this help message\n\n");
	return GMT_MODULE_USAGE;
}

/* ======================================================================================================== */

/* -G takes an optional lon,lat,elev triple: with values it pins the GeoOrigin,
 * without them the grid centre and mean altitude are used. */
static bool parse_geoorigin(struct mbgrd2gltf_options *o, const char *arg) {
	if (arg == NULL || arg[0] == '\0') {
		o->is_geoorigin_auto = true;
		o->is_geoorigin_set = true;
		return true;
	}
	if (sscanf(arg, "%lf,%lf,%lf", &o->geoorigin_lon, &o->geoorigin_lat, &o->geoorigin_elev) == 3) {
		o->is_geoorigin_set = true;
		o->is_geoorigin_auto = false;
		return true;
	}
	return false;
}

/* Port of the option parsing. GMT hands each option over already split into a
 * letter and its argument; the long forms reach this switch through
 * preparse_long_options() below. */
static int parse_mbgrd2gltf(struct GMT_CTRL *GMT, struct MBGRD2GLTF_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt = NULL;
	struct GMTAPI_CTRL *API = GMT->parent;
	struct mbgrd2gltf_options *o = &Ctrl->options;
	char reason[MBGRD2GLTF_ERROR_MAXLINE];

	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {

		case '<':
			/* A bare grid name stands in for -I. */
			if (o->input_filepath[0] == '\0') {
				strncpy(o->input_filepath, opt->arg, MBGRD2GLTF_PATH_MAXLINE - 1);
			}
			break;

		case 'I':
			strncpy(o->input_filepath, opt->arg, MBGRD2GLTF_PATH_MAXLINE - 1);
			break;

		case 'O':
			strncpy(o->output_filepath, opt->arg, MBGRD2GLTF_PATH_MAXLINE - 1);
			o->is_output_folder_set = true;
			break;

		/* -Z, not -B: GMT reserves -B for the map frame and intercepts it before
		 * the module sees it. preparse_long_options() rewrites both --binary and
		 * the legacy -B into -Z, so both spellings still work. GLB is the
		 * default, so it changes nothing; -A, the short form of --no-binary,
		 * writes glTF. */
		case 'Z':
			o->is_binary_output = true;
			break;

		case 'A':
			o->is_binary_output = false;
			break;

		/* Draco is on by default; -D is accepted and changes nothing */
		case 'D':
			o->is_draco_compressed = true;
			break;

		case 'N':
			o->is_draco_compressed = false;
			break;

		case 'T':
			if (!mbgrd2gltf_parse_draco_precision(opt->arg, &o->draco_precision, reason, sizeof(reason))) {
				GMT_Report(API, GMT_MSG_ERROR, "Invalid --draco-precision: %s\n", reason);
				n_errors++;
			}
			break;

		case 'L':
			if (!mbgrd2gltf_parse_draco_normal_bits(opt->arg, &o->draco_normal_bits, reason, sizeof(reason))) {
				GMT_Report(API, GMT_MSG_ERROR, "Invalid --draco-normal-bits: %s\n", reason);
				n_errors++;
			}
			break;

		case 'S':
			if (opt->arg[0] == '\0') {
				GMT_Report(API, GMT_MSG_ERROR,
				           "--geoviewpoint requires a value, attached: --geoviewpoint=lon,lat,elev,clon,clat,celev\n");
				n_errors++;
			}
			else if (!mbgrd2gltf_geoviewpoint_valid(opt->arg, reason, sizeof(reason))) {
				GMT_Report(API, GMT_MSG_ERROR, "Invalid --geoviewpoint: %s\n", reason);
				n_errors++;
			}
			else if (o->n_geoviewpoints >= MBGRD2GLTF_GEOVIEWPOINT_MAX) {
				GMT_Report(API, GMT_MSG_ERROR, "At most %d --geoviewpoint values can be given\n",
				           MBGRD2GLTF_GEOVIEWPOINT_MAX);
				n_errors++;
			}
			else if (strlen(opt->arg) >= MBGRD2GLTF_GEOVIEWPOINT_MAXLINE) {
				GMT_Report(API, GMT_MSG_ERROR, "Invalid --geoviewpoint: the value is too long\n");
				n_errors++;
			}
			else {
				snprintf(o->geoviewpoints[o->n_geoviewpoints], MBGRD2GLTF_GEOVIEWPOINT_MAXLINE, "%s", opt->arg);
				o->n_geoviewpoints++;
			}
			break;

		case 'M':
			o->colors = false;
			break;

		case 'C':
			o->is_cesium_html_output = true;
			break;

		case 'F':
			if (opt->arg[0] == '\0') {
				GMT_Report(API, GMT_MSG_ERROR, "--cesium-url requires the directory of a CesiumJS build\n");
				n_errors++;
			}
			else {
				snprintf(o->cesium_url, MBGRD2GLTF_PATH_MAXLINE, "%s", opt->arg);
			}
			break;

		case 'E':
			if (sscanf(opt->arg, "%lf", &o->exaggeration) == 1) {
				o->is_exaggeration_set = true;
			}
			else {
				GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -E option: expected a number\n");
				n_errors++;
			}
			break;

		case 'G':
			if (!parse_geoorigin(o, opt->arg)) {
				GMT_Report(API, GMT_MSG_NORMAL, "Syntax error -G option: expected lon,lat,elev or no value\n");
				n_errors++;
			}
			break;

		/* the quantization bits of the original, which it now refuses */
		case 'Q':
			GMT_Report(API, GMT_MSG_ERROR,
			           "-Q and --quantize-* are replaced by --draco-precision (the step of the positions in meters, "
			           "for each block) and --draco-normal-bits, which mbmesh takes as well\n");
			n_errors++;
			break;

		case 'W':
			o->is_html_output = true;
			break;

		case 'H':
		case 'h':
			o->is_help = true;
			break;

		default:
			n_errors += gmt_default_error(GMT, opt->option);
			break;
		}
	}

	if (o->n_geoviewpoints > 0 && !o->is_html_output && !o->is_cesium_html_output) {
		GMT_Report(API, GMT_MSG_ERROR,
		           "--geoviewpoint adds a viewpoint to the page that --html or --cesium-html writes; use one of them too\n");
		n_errors++;
	}

	return n_errors ? GMT_PARSE_ERROR : GMT_OK;
}

/* ======================================================================================================== */

/* The long options the original accepts, mapped to the short forms GMT can
 * parse. The replaced --quantize-* options become -Q, which is refused. */
/*--------------------------------------------------------------------*/
/* Joins the argv[] form of a module's arguments into the single string that
 * preparse_long_options() works on. GMT hands a module its arguments either
 * as an argv[] array of mode entries (mode > 0, which is what the gmt
 * executable does), as a single command string (mode == GMT_MODULE_CMD,
 * which is what the C API and the external interfaces do), or as a
 * ready-made option list (mode < 0). Returns NULL for the shapes that are
 * not an argv[] array, so the caller can fall back to them. */
static char *join_args(int mode, void *args) {
	char **argv = (char **)args;
	size_t total = 1;
	int i;
	char *joined = NULL;

	if (mode <= 0 || args == NULL) return NULL;
	for (i = 0; i < mode; i++) total += strlen(argv[i]) + 1;
	joined = (char *)calloc(total, sizeof(char));
	if (joined == NULL) return NULL;
	for (i = 0; i < mode; i++) {
		if (i > 0) strcat(joined, " ");
		strcat(joined, argv[i]);
	}
	return joined;
}

static char *preparse_long_options(struct mbgrd2gltf_options *o, const char *args) {
	const size_t length = (args != NULL) ? strlen(args) : 0;
	/* worst case each token gains "-Qp", so allow a few extra bytes per token */
	char *rewritten = (char *)calloc(2 * length + 8, sizeof(char));
	char *copy = (char *)calloc(length + 2, sizeof(char));
	size_t out = 0;
	char *token = NULL;
	char *saveptr = NULL;
	char pending_short[4] = "";

	if (rewritten == NULL || copy == NULL) {
		free(rewritten);
		free(copy);
		return NULL;
	}
	if (length == 0) {
		free(copy);
		return rewritten;
	}
	memcpy(copy, args, length);

	for (token = strtok_r(copy, " \t", &saveptr); token != NULL; token = strtok_r(NULL, " \t", &saveptr)) {
		char name[128];
		const char *value = NULL;
		char *equals = NULL;
		const char *emit = NULL;

		/* a long option that took its value from the following token */
		if (pending_short[0] != '\0') {
			if (out > 0) rewritten[out++] = ' ';
			rewritten[out++] = '-';
			memcpy(rewritten + out, pending_short, strlen(pending_short));
			out += strlen(pending_short);
			memcpy(rewritten + out, token, strlen(token));
			out += strlen(token);
			pending_short[0] = '\0';
			continue;
		}

		if (!(token[0] == '-' && token[1] == '-' && token[2] != '\0')) {
			/* The legacy -B (binary output) would be eaten by GMT as the map-frame
			 * option, so it becomes -Z here just as --binary does. */
			if (token[0] == '-' && token[1] == 'B' && token[2] == '\0') {
				if (out > 0) rewritten[out++] = ' ';
				rewritten[out++] = '-';
				rewritten[out++] = 'Z';
				continue;
			}
			if (out > 0) rewritten[out++] = ' ';
			memcpy(rewritten + out, token, strlen(token));
			out += strlen(token);
			continue;
		}

		strncpy(name, token + 2, sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
		equals = strchr(name, '=');
		if (equals != NULL) {
			*equals = '\0';
			value = equals + 1;
		}

		/* flags */
		if (strcmp(name, "binary") == 0)            emit = "Z";   /* -B is reserved by GMT */
		else if (strcmp(name, "no-binary") == 0)    emit = "A";
		else if (strcmp(name, "draco") == 0)        emit = "D";
		else if (strcmp(name, "no-draco") == 0)     emit = "N";
		else if (strcmp(name, "no-colors") == 0)    emit = "M";
		else if (strcmp(name, "html") == 0)         emit = "W";
		else if (strcmp(name, "cesium-html") == 0)  emit = "C";
		else if (strcmp(name, "verbose") == 0)      emit = "V";
		if (emit != NULL) {
			if (out > 0) rewritten[out++] = ' ';
			rewritten[out++] = '-';
			rewritten[out++] = emit[0];
			continue;
		}

		/* --help is acted on here rather than rewritten, because -h is a GMT
		 * common option and would be intercepted before this module sees it. */
		if (strcmp(name, "help") == 0) {
			o->is_help = true;
			continue;
		}

		/* value-taking options */
		if (strcmp(name, "input") == 0)                  emit = "I";
		else if (strcmp(name, "output") == 0)            emit = "O";
		else if (strcmp(name, "exaggeration") == 0)      emit = "E";
		else if (strcmp(name, "geoorigin") == 0)         emit = "G";
		else if (strcmp(name, "draco-precision") == 0)   emit = "T";
		else if (strcmp(name, "draco-normal-bits") == 0) emit = "L";
		else if (strcmp(name, "geoviewpoint") == 0)      emit = "S";
		else if (strcmp(name, "cesium-url") == 0)        emit = "F";
		/* replaced by --draco-precision; -Q is refused with the original's message */
		else if (strncmp(name, "quantize-", 9) == 0)     emit = "Q";

		if (emit == NULL) {
			/* not ours: hand it to GMT unchanged */
			if (out > 0) rewritten[out++] = ' ';
			memcpy(rewritten + out, token, strlen(token));
			out += strlen(token);
			continue;
		}

		/* --geoorigin is the one option whose value is optional */
		if (value == NULL && strcmp(name, "geoorigin") == 0) {
			if (out > 0) rewritten[out++] = ' ';
			rewritten[out++] = '-';
			rewritten[out++] = 'G';
			continue;
		}

		if (value != NULL) {
			if (out > 0) rewritten[out++] = ' ';
			rewritten[out++] = '-';
			memcpy(rewritten + out, emit, strlen(emit));
			out += strlen(emit);
			memcpy(rewritten + out, value, strlen(value));
			out += strlen(value);
		}
		else {
			strncpy(pending_short, emit, sizeof(pending_short) - 1);
			pending_short[sizeof(pending_short) - 1] = '\0';
		}
	}

	rewritten[out] = '\0';
	free(copy);
	return rewritten;
}

/* ======================================================================================================== */

/* Size of a file in MB, or 0.0 when it cannot be stat'ed - the same thing the
 * original reports. */
static double file_size_mb(const char *path) {
	struct stat st;

	if (stat(path, &st) != 0) return 0.0;
	return (double)st.st_size / (1024.0 * 1024.0);
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code)  { mbgrd2gltf_geometry_destroy(geometry); mbgrd2gltf_bathymetry_destroy(bathymetry); \
                        free(remaining_args); Free_mbgrd2gltf_Ctrl(GMT, Ctrl); \
                        gmt_end_module(GMT, GMT_cpy); bailout(code); }

EXTERN_MSC int GMT_mbgrd2gltf(void *V_API, int mode, void *args);

/* Port of main(). */
int GMT_mbgrd2gltf(void *V_API, int mode, void *args) {

	struct MBGRD2GLTF_CTRL *Ctrl = NULL;
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct mbgrd2gltf_options *o = NULL;

	mbgrd2gltf_bathymetry_t *bathymetry = NULL;
	mbgrd2gltf_geometry_t *geometry = NULL;
	char *remaining_args = NULL;

	char error[MBGRD2GLTF_ERROR_MAXLINE] = "";
	char message[MBGRD2GLTF_MESSAGE_MAXLINE];
	char command_line[MBGRD2GLTF_MESSAGE_MAXLINE];
	char output_file[MBGRD2GLTF_PATH_MAXLINE + 8];
	int parse_status;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);

	/* Resolve the long options before GMT_Create_Options() sees the command
	 * line: GMT has no keyword dictionary for this module and would reject
	 * them all, including --input and --output. */
	{
		struct mbgrd2gltf_options staged;

		mbgrd2gltf_options_defaults(&staged);
		{
			char *joined = join_args(mode, args);
			/* An external API such as GMT.jl hands over an option list, in which a
			 * long option is a "--" parameter: make it text again so that the long
			 * options are resolved here as well. */
			char *listed = (mode == GMT_MODULE_OPT && args != NULL) ? GMT_Create_Cmd(API, (struct GMT_OPTION *)args) : NULL;
			const char *text = (joined != NULL) ? joined
			                 : ((mode == GMT_MODULE_CMD) ? (const char *)args : listed);
			if (text != NULL) remaining_args = preparse_long_options(&staged, text);
			free(joined);
			if (listed != NULL) GMT_Destroy_Cmd(API, &listed);
		}

		options = GMT_Create_Options(API, (remaining_args != NULL) ? GMT_MODULE_CMD : mode,
		                             (remaining_args != NULL) ? (void *)remaining_args : args);
		if (API->error) {
			free(remaining_args);
			return API->error;
		}
		if (!options || options->option == GMT_OPT_USAGE) {
			free(remaining_args);
			bailout(usage(API, GMT_USAGE));
		}
		if (options->option == GMT_OPT_SYNOPSIS) {
			free(remaining_args);
			bailout(usage(API, GMT_SYNOPSIS));
		}

		if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS,
		                           NULL, &options, &GMT_cpy)) == NULL) {
			free(remaining_args);
			bailout(API->error);
		}
		if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

		Ctrl = (struct MBGRD2GLTF_CTRL *)New_mbgrd2gltf_Ctrl(GMT);
		Ctrl->options.is_help = staged.is_help;
	}

	if ((parse_status = parse_mbgrd2gltf(GMT, Ctrl, options)) != 0) Return(parse_status);

	o = &Ctrl->options;
	o->is_verbose = GMT->common.V.active ? true : false;

	if (o->is_help) {
		usage(API, GMT_USAGE);
		Return(GMT_NOERROR);
	}

	if (o->input_filepath[0] == '\0') {
		GMT_Report(API, GMT_MSG_ERROR, "No input grid given; use -I <file>\n");
		Return(GMT_RUNTIME_ERROR);
	}

	/* Without -O the output root is the input basename, as the original
	 * option parser derives it. */
	if (o->output_filepath[0] == '\0') {
		mbgrd2gltf_default_output_root(o->input_filepath, o->output_filepath, MBGRD2GLTF_PATH_MAXLINE);
	}

	/* Configure logger based on verbose flag */
	mbgrd2gltf_logger_set_verbose(o->is_verbose);

	/* Start capturing log messages for provenance in HTML output */
	mbgrd2gltf_logger_start_capture();

	/* Log the command line */
	snprintf(command_line, sizeof(command_line), "%s %s", THIS_MODULE_NAME,
	         (remaining_args != NULL) ? remaining_args : "");
	snprintf(message, sizeof(message), "Command: %s", command_line);
	log_info(API, message);

	snprintf(message, sizeof(message), "Starting mbgrd2gltf processing for %s (%.3f MB)", o->input_filepath,
	         file_size_mb(o->input_filepath));
	log_info(API, message);

	snprintf(message, sizeof(message), "Binary output: %s Draco compression: %s",
	         o->is_binary_output ? "enabled," : "disabled,", o->is_draco_compressed ? "enabled" : "disabled");
	log_info(API, message);

	if (o->is_draco_compressed) {
		snprintf(message, sizeof(message),
		         "Draco: positions kept to %g m in each block (--draco-precision), normals in %d bits (--draco-normal-bits)",
		         o->draco_precision, o->draco_normal_bits);
		log_info(API, message);
	}

	if (!mbgrd2gltf_bathymetry_create(o, &bathymetry, error, sizeof(error))) {
		GMT_Report(API, GMT_MSG_ERROR, "%s\n", error);
		Return(GMT_RUNTIME_ERROR);
	}

	log_info(API, "Generating 3D geometry from 2D bathymetric grid data");
	snprintf(message, sizeof(message), "Vertical exaggeration: %g", o->exaggeration);
	log_info(API, message);

	if (!mbgrd2gltf_geometry_create(bathymetry, o, &geometry, error, sizeof(error))) {
		GMT_Report(API, GMT_MSG_ERROR, "%s\n", error);
		Return(GMT_RUNTIME_ERROR);
	}

	snprintf(output_file, sizeof(output_file), "%s%s", o->output_filepath,
	         o->is_binary_output ? ".glb" : ".gltf");

	if (!mbgrd2gltf_write_gltf(bathymetry, geometry, o, command_line, error, sizeof(error))) {
		GMT_Report(API, GMT_MSG_ERROR, "%s\n", error);
		Return(GMT_RUNTIME_ERROR);
	}

	snprintf(message, sizeof(message), "Successfully wrote glTF file to %s (%.3f MB)", output_file,
	         file_size_mb(output_file));
	log_info(API, message);

	/* Generate the viewer pages if requested */
	if (o->is_html_output || o->is_cesium_html_output) {
		const char *output_dir;
		const char *html_name;
		char dir_buffer[MBGRD2GLTF_PATH_MAXLINE];
		char page_names[2][MBGRD2GLTF_PATH_MAXLINE + 16];
		int n_pages = 0;
		char *last_slash;
		int i;

		/* The pages load the model over HTTP, which browsers require for local
		 * files; log the command to run rather than starting a server. */
		snprintf(dir_buffer, sizeof(dir_buffer), "%s", o->output_filepath);
		last_slash = strrchr(dir_buffer, '/');
		if (last_slash == NULL) last_slash = strrchr(dir_buffer, '\\');
		if (last_slash != NULL) {
			*last_slash = '\0';
			output_dir = dir_buffer;
			html_name = last_slash + 1;
		}
		else {
			output_dir = ".";
			html_name = o->output_filepath;
		}

		if (o->is_html_output) {
			char timestamp[64] = "";
			const time_t now = time(NULL);
			struct tm gmt_time;

			/* Get current timestamp in ISO 8601 format */
#ifdef _WIN32
			gmtime_s(&gmt_time, &now);
#else
			gmtime_r(&now, &gmt_time);
#endif
			strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &gmt_time);

			if (!mbgrd2gltf_write_html(bathymetry, geometry, o, command_line, timestamp, error, sizeof(error))) {
				GMT_Report(API, GMT_MSG_ERROR, "%s\n", error);
				Return(GMT_RUNTIME_ERROR);
			}
			snprintf(page_names[n_pages++], sizeof(page_names[0]), "%s.html", html_name);
		}
		if (o->is_cesium_html_output) {
			if (!mbgrd2gltf_write_cesium_html(geometry, o, error, sizeof(error))) {
				GMT_Report(API, GMT_MSG_ERROR, "%s\n", error);
				Return(GMT_RUNTIME_ERROR);
			}
			snprintf(page_names[n_pages++], sizeof(page_names[0]), "%s_cesium.html", html_name);
		}

		snprintf(message, sizeof(message),
		         "To view %s, run this command in a terminal (Ctrl-C stops it), then open the URL%s:",
		         n_pages > 1 ? "the pages" : page_names[0], n_pages > 1 ? "s" : "");
		log_info(API, message);
		snprintf(message, sizeof(message), "  python3 -m http.server 8000 --bind 127.0.0.1 --directory \"%s\"", output_dir);
		log_info(API, message);
		for (i = 0; i < n_pages; i++) {
			snprintf(message, sizeof(message), "  http://127.0.0.1:8000/%s", page_names[i]);
			log_info(API, message);
		}
	}

	Return(GMT_NOERROR);
}
