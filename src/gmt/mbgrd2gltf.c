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
 *    out-of-tree module, so the long forms the original accepted (--input,
 *    --binary, --quantize-position, ...) are resolved here before
 *    GMT_Create_Options() sees the command line. Every option the original
 *    accepted is still accepted.
 */

#define THIS_MODULE_NAME    "mbgrd2gltf"
#define THIS_MODULE_LIB     "mbsystem"
#define THIS_MODULE_PURPOSE "Convert a bathymetry grid to a glTF/GLB 3D model"
/* -I carries the input grid, which is the module's one primary resource, so an
 * external API (Julia, Python, MATLAB) binds its input argument to -I. There is
 * deliberately no output key: mbgrd2gltf writes the .glb/.gltf and the optional
 * .html itself and hands nothing back through the API. */
#define THIS_MODULE_KEYS    "IG{"
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
	            "3D model, with optional Draco mesh compression, vertical exaggeration and\n"
	            "binary output format.\n\n"
	            "The output mesh vertices are positioned in an Earth-Centered, Earth-Fixed\n"
	            "(ECEF) Cartesian coordinate system with units in meters. ECEF is a 3D\n"
	            "right-handed coordinate system with its origin at Earth's center of mass.\n"
	            "A GeoOrigin can be specified to improve rendering precision for localized areas.\n\n");
	GMT_Message(API, GMT_TIME_NONE, "usage: mbgrd2gltf --input FILE [OPTIONS]\n");
	GMT_Message(API, GMT_TIME_NONE, "       mbgrd2gltf -I FILE [OPTIONS]  (legacy style)\n\n");
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;

	GMT_Message(API, GMT_TIME_NONE, "Options:\n");
	GMT_Message(API, GMT_TIME_NONE, "  --input, -I FILE              Input GMT GRD format bathymetry grid file (required)\n");
	GMT_Message(API, GMT_TIME_NONE, "  --binary, -B                  Output in binary glTF (GLB) format\n");
	GMT_Message(API, GMT_TIME_NONE, "                                  (-Z is the GMT spelling; GMT reserves -B)\n");
	GMT_Message(API, GMT_TIME_NONE, "  --output, -O ROOT             Output file root [default: input file basename]\n");
	GMT_Message(API, GMT_TIME_NONE, "  --exaggeration, -E NUM        Vertical exaggeration factor [default: 1.0]\n");
	GMT_Message(API, GMT_TIME_NONE, "  --geoorigin, -G [LON,LAT,EL]  GeoOrigin for high-precision local coordinates\n");
	GMT_Message(API, GMT_TIME_NONE, "                                  With values: use specified lon,lat,elev\n");
	GMT_Message(API, GMT_TIME_NONE, "                                  Without values: use grid center and mean altitude\n");
	GMT_Message(API, GMT_TIME_NONE, "                                  Not specified: original ECEF coordinates (default)\n");
	GMT_Message(API, GMT_TIME_NONE, "  --draco, -D                   Enable Draco mesh compression\n");
	GMT_Message(API, GMT_TIME_NONE, "  --quantize-position NUM       Draco position quantization bits (2-30) [default: 16]\n");
	GMT_Message(API, GMT_TIME_NONE, "  --quantize-normal NUM         Draco normal quantization bits (2-30) [default: 7]\n");
	GMT_Message(API, GMT_TIME_NONE, "  --quantize-texcoord NUM       Draco texcoord quantization bits (2-30) [default: 10]\n");
	GMT_Message(API, GMT_TIME_NONE, "  --quantize-color NUM          Draco color quantization bits (2-30) [default: 8]\n");
	GMT_Message(API, GMT_TIME_NONE, "                      (-Qp, -Qn, -Qt, -Qc for legacy style)\n");
	GMT_Message(API, GMT_TIME_NONE, "  --html, -W                    Generate HTML viewer with inline glTF/GLB model\n");
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

/* -Q selects which Draco quantization the value applies to: p, n, t or c. */
static bool parse_quantization(struct mbgrd2gltf_options *o, const char *arg) {
	int value = 0;
	int index;

	if (arg == NULL || arg[0] == '\0') return false;
	switch (arg[0]) {
		case 'p': index = 0; break;
		case 'n': index = 1; break;
		case 't': index = 2; break;
		case 'c': index = 3; break;
		default: return false;
	}
	if (sscanf(arg + 1, "%d", &value) != 1) return false;
	o->draco_quantization[index] = value;
	return true;
}

/* Port of the option parsing. GMT hands each option over already split into a
 * letter and its argument; the long forms reach this switch through
 * preparse_long_options() below. */
static int parse_mbgrd2gltf(struct GMT_CTRL *GMT, struct MBGRD2GLTF_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt = NULL;
	struct GMTAPI_CTRL *API = GMT->parent;
	struct mbgrd2gltf_options *o = &Ctrl->options;

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
		 * the legacy -B into -Z, so both spellings still work. */
		case 'Z':
			o->is_binary_output = true;
			break;

		case 'D':
			o->is_draco_compressed = true;
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

		case 'Q':
			if (!parse_quantization(o, opt->arg)) {
				GMT_Report(API, GMT_MSG_NORMAL,
				           "Syntax error -Q option: expected p, n, t or c followed by a bit count\n");
				n_errors++;
			}
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

	return n_errors ? GMT_PARSE_ERROR : GMT_OK;
}

/* ======================================================================================================== */

/* The long options the original accepted, mapped to the short forms GMT can
 * parse. --quantize-* have no short equivalent of their own, so they are
 * folded into -Q with the selector letter the legacy style uses. */
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
		else if (strcmp(name, "draco") == 0)        emit = "D";
		else if (strcmp(name, "html") == 0)         emit = "W";
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
		else if (strcmp(name, "quantize-position") == 0) emit = "Qp";
		else if (strcmp(name, "quantize-normal") == 0)   emit = "Qn";
		else if (strcmp(name, "quantize-texcoord") == 0) emit = "Qt";
		else if (strcmp(name, "quantize-color") == 0)    emit = "Qc";

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
			const char *text = (joined != NULL) ? joined
			                                    : ((mode == GMT_MODULE_CMD) ? (const char *)args : NULL);
			if (text != NULL) remaining_args = preparse_long_options(&staged, text);
			free(joined);
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

	if (o->is_draco_compressed && !mbgrd2gltf_draco_quantization_valid(o)) {
		GMT_Report(API, GMT_MSG_ERROR, "Draco quantization bits must be between 2 and 30\n");
		Return(GMT_RUNTIME_ERROR);
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
		         "Draco quantization bits - Position: %d Normal: %d Texcoord: %d Color: %d",
		         o->draco_quantization[0], o->draco_quantization[1], o->draco_quantization[2],
		         o->draco_quantization[3]);
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

	if (!mbgrd2gltf_write_gltf(geometry, o, error, sizeof(error))) {
		GMT_Report(API, GMT_MSG_ERROR, "%s\n", error);
		Return(GMT_RUNTIME_ERROR);
	}

	snprintf(message, sizeof(message), "Successfully wrote glTF file to %s (%.3f MB)", output_file,
	         file_size_mb(output_file));
	log_info(API, message);

	/* Generate HTML viewer if requested */
	if (o->is_html_output) {
		char timestamp[64] = "";
		const time_t now = time(NULL);
		struct tm gmt_time;
		const char *output_dir;
		const char *html_name;
		char dir_buffer[MBGRD2GLTF_PATH_MAXLINE];
		char html_buffer[MBGRD2GLTF_PATH_MAXLINE + 8];
		char *last_slash;

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

		/* Extract directory path for web server instructions */
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
		snprintf(html_buffer, sizeof(html_buffer), "%s.html", html_name);

		log_info(API, "To view the HTML file (avoiding CORS issues):");
		log_info(API, "  Start a local web server in the output directory:");
		snprintf(message, sizeof(message), "    cd %s", output_dir);
		log_info(API, message);
		log_info(API, "    python3 -m http.server 8000");
		snprintf(message, sizeof(message), "  Then open: http://localhost:8000/%s", html_buffer);
		log_info(API, message);
	}

	Return(GMT_NOERROR);
}
