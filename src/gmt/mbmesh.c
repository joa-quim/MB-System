/*--------------------------------------------------------------------
 *    The MB-system:  mbmesh.c
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * mbmesh generates 3D meshes from swath sonar bathymetry data.
 *
 * GMT module port of src/mbmesh/mbmesh.cpp. The driver below is that file's
 * main(), parse_options(), print_usage() and write_outputs() rewritten in C
 * against the GMT option parser and the GMT reporting API; the reconstruction
 * pipeline itself is reached through the plain C interface in
 * src/mbmesh/mbmesh_capi.h.
 *
 * Option mapping. The original driver's getopt_long table is kept whole: the
 * long forms are recognised by mb_gmt_mark_long_options() and read in parse()
 * in command-line order, each onto the short letter or the case label it
 * carried in the original switch. The original's single-dash legacy aliases
 * (-html, -local-xyz, ...) reach GMT split into a letter and the rest of the
 * word; mark_single_dash_aliases() puts each back into its long form first.
 */

#define THIS_MODULE_NAME    "mbmesh"
#define THIS_MODULE_LIB     "mbsystem"
#define THIS_MODULE_PURPOSE "Generate 3D meshes from swath sonar bathymetry data"
/* -I carries the input datalist, which is the module's one primary resource, so
 * an external API (Julia, Python, MATLAB) binds its input argument to -I. The
 * output key carries the --metadata listing; mbmesh writes mesh.glb and its
 * optional companions into the -O directory itself. */
#define THIS_MODULE_KEYS    "ID{,>D}"
#define THIS_MODULE_NEEDS   ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_define.h"
#include "mb_status.h"

#include "mbmesh_capi.h"
#include "mb_gmt_opts.h"
#include "mb_gmt_text.h"

/* ======================================================================================================== */

#define MBMESH_ERROR_MAXLINE   1024
#define MBMESH_MESSAGE_MAXLINE 2048

/* The original driver writes every stage line to std::cerr as
 *     mbmesh: [stage] message
 * and so does the module: the stage lines follow the program's own -V/--verbose
 * switch, not GMT's verbosity level, and always_print emits the startup and
 * completion lines even when verbose is off, as the original does. Warnings and
 * fatal errors go through GMT_Report. */
static void log_message(struct GMTAPI_CTRL *API, const char *stage, const char *message, bool verbose,
                        bool always_print) {
	(void)API;
	if (!verbose && !always_print) {
		return;
	}
	fprintf(stderr, "mbmesh: [%s] %s\n", stage, message);
}

static void log_warning(struct GMTAPI_CTRL *API, const char *message) {
	GMT_Report(API, GMT_MSG_WARNING, "[warning] %s\n", message);
}

static void log_fatal(struct GMTAPI_CTRL *API, const char *message) {
	GMT_Report(API, GMT_MSG_ERROR, "[fatal] %s\n", message);
}

/* ======================================================================================================== */

struct MBMESH_CTRL {
	struct mbmesh_options options;
};

static void *New_mbmesh_Ctrl(struct GMT_CTRL *GMT) {
	struct MBMESH_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBMESH_CTRL);
	mbmesh_options_defaults(&Ctrl->options);
	return Ctrl;
}

static void Free_mbmesh_Ctrl(struct GMT_CTRL *GMT, struct MBMESH_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

/* Port of print_usage(). The text is unchanged. */
static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;

	GMT_Message(API, GMT_TIME_NONE, "mbmesh generates 3D meshes from swath sonar bathymetry data.\n\n");
	GMT_Message(API, GMT_TIME_NONE,
	            "usage: mbmesh -I datalist [-R west/east/south/north] [-O outputdir] [-L meters] "
	            "[output options] [-V]\n\n");
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;

	GMT_Message(API, GMT_TIME_NONE, "Required:\n");
	GMT_Message(API, GMT_TIME_NONE, "  -I, --input <datalist>       Input MB-System datalist file\n\n");
	GMT_Message(API, GMT_TIME_NONE, "Optional:\n");
	GMT_Message(API, GMT_TIME_NONE, "  -O, --output <outputdir>     Output directory [mbmesh_output]\n");
	GMT_Message(API, GMT_TIME_NONE, "  -R, --bounds <w/e/s/n>       Geographic bounds in degrees\n");
	GMT_Message(API, GMT_TIME_NONE, "  -L, --lod <meters>           Requested smallest feature size; auto if omitted\n");
	GMT_Message(API, GMT_TIME_NONE, "      --decimate <meters>      Enable voxel-grid point decimation\n");
	GMT_Message(API, GMT_TIME_NONE, "      --metadata, --info       Print dataset metadata and exit\n");
	GMT_Message(API, GMT_TIME_NONE, "  -V, --verbose                Enable progress and diagnostic logging\n");
	GMT_Message(API, GMT_TIME_NONE, "  -H, -h, --help               Print this help message\n\n");
	GMT_Message(API, GMT_TIME_NONE, "Output options:\n");
	GMT_Message(API, GMT_TIME_NONE, "      --html                   Also write mesh.html and launch local X3DOM viewer\n");
	GMT_Message(API, GMT_TIME_NONE, "      --xyz                    Also write local and ECEF XYZ point clouds\n");
	GMT_Message(API, GMT_TIME_NONE, "      --local-xyz              Also write pointcloud-local.xyz\n");
	GMT_Message(API, GMT_TIME_NONE, "      --ecef-xyz               Also write pointcloud-ecef.xyz\n");
	GMT_Message(API, GMT_TIME_NONE, "      --oriented-ply           Also write oriented-pointcloud-ecef.ply\n");
	GMT_Message(API, GMT_TIME_NONE, "      --pointcloud-glb         Also write pointcloud.glb\n");
	GMT_Message(API, GMT_TIME_NONE, "      --normal-glb             Also write normals.glb\n");
	GMT_Message(API, GMT_TIME_NONE, "      --origin-glb             Also write origins.glb\n");
	GMT_Message(API, GMT_TIME_NONE, "      --raw-mesh-glb           Also write raw_mesh.glb before support trimming\n");
	GMT_Message(API, GMT_TIME_NONE, "      --diagnostics            Also write pointcloud, normal, and origin GLBs\n");
	GMT_Message(API, GMT_TIME_NONE, "      --all-outputs            Write all optional outputs\n\n");
	GMT_Message(API, GMT_TIME_NONE, "The default output is mesh.glb.\n");
	GMT_Message(API, GMT_TIME_NONE,
	            "Single-dash long output options such as -html are accepted for legacy compatibility.\n");
	return GMT_MODULE_USAGE;
}

/* ======================================================================================================== */

/* The long-only switches of the original driver have no short equivalent, so
 * parse_mbmesh() applies them by name through this function. Each entry is the long option
 * and the case label it carried in the original getopt_long switch. */
static bool parse_long_only_option(struct mbmesh_options *o, const char *name) {
	if (strcmp(name, "metadata") == 0 || strcmp(name, "info") == 0) {
		o->metadata_requested = true;                 /* case 1000 */
	}
	else if (strcmp(name, "html") == 0) {
		o->write_html = true;                         /* case 1001 */
	}
	else if (strcmp(name, "local-xyz") == 0) {
		o->write_local_xyz = true;                    /* case 1002 */
	}
	else if (strcmp(name, "ecef-xyz") == 0) {
		o->write_ecef_xyz = true;                     /* case 1003 */
	}
	else if (strcmp(name, "xyz") == 0) {
		o->write_local_xyz = true;                    /* case 1004 */
		o->write_ecef_xyz = true;
	}
	else if (strcmp(name, "oriented-ply") == 0) {
		o->write_oriented_ply = true;                 /* case 1005 */
	}
	else if (strcmp(name, "pointcloud-glb") == 0) {
		o->write_pointcloud_glb = true;               /* case 1006 */
	}
	else if (strcmp(name, "normal-glb") == 0) {
		o->write_normal_glb = true;                   /* case 1007 */
	}
	else if (strcmp(name, "origin-glb") == 0) {
		o->write_origin_glb = true;                   /* case 1008 */
	}
	else if (strcmp(name, "raw-mesh-glb") == 0) {
		o->write_raw_mesh_glb = true;                 /* case 1009 */
	}
	else if (strcmp(name, "diagnostics") == 0) {
		o->write_pointcloud_glb = true;               /* case 1011 */
		o->write_normal_glb = true;
		o->write_origin_glb = true;
	}
	else if (strcmp(name, "all-outputs") == 0) {
		o->write_html = true;                         /* case 1012 */
		o->write_local_xyz = true;
		o->write_ecef_xyz = true;
		o->write_oriented_ply = true;
		o->write_pointcloud_glb = true;
		o->write_normal_glb = true;
		o->write_origin_glb = true;
		o->write_raw_mesh_glb = true;
	}
	else {
		return false;
	}
	return true;
}

/* --decimate takes an argument, so it is handled separately (case 1010). */
static bool parse_long_only_option_with_argument(struct mbmesh_options *o, const char *name, const char *argument) {
	if (strcmp(name, "decimate") == 0) {
		o->decimation_decimate = true;
		o->decimation_cell_size = strtod(argument, NULL);
		o->decimation_requested = true;
		return true;
	}
	return false;
}


/* The original driver's getopt_long table, in its order, and what each entry is:
 * the short letter it returned, or 0 for the long-only switches (cases 1000-1012),
 * which parse_long_only_option() and parse_long_only_option_with_argument() apply. */
static const struct MB_GMT_LONGOPT_DEF long_options[] = {
	{"input", true},
	{"output", true},
	{"bounds", true},
	{"lod", true},
	{"level-of-detail", true},
	{"metadata", false},
	{"info", false},
	{"html", false},
	{"local-xyz", false},
	{"ecef-xyz", false},
	{"xyz", false},
	{"oriented-ply", false},
	{"pointcloud-glb", false},
	{"normal-glb", false},
	{"origin-glb", false},
	{"raw-mesh-glb", false},
	{"decimate", true},
	{"diagnostics", false},
	{"all-outputs", false},
	{"verbose", false},
	{"help", false},
	{NULL, false}};
static const char long_letter[] = {'I', 'O', 'R', 'L', 'L', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'V', 'H'};

/* The original's single_dash_long_options: each word is accepted as the
 * double-dash form of the same name. GMT has split "-html" into option 'h' with
 * argument "tml"; put such an option back as the long option "html" so that
 * mb_gmt_mark_long_options() takes it, together with the value that follows
 * -decimate. Called before mb_gmt_mark_long_options(). */
static void mark_single_dash_aliases(struct GMT_OPTION *options) {
	static const char *single_dash_long_options[] = {
	    "html", "local-xyz", "ecef-xyz", "xyz", "oriented-ply", "pointcloud-glb",
	    "normal-glb", "origin-glb", "raw-mesh-glb", "decimate", "diagnostics", "all-outputs"};
	struct GMT_OPTION *opt;
	size_t i;

	for (opt = options; opt; opt = opt->next) {
		if (opt->option == GMT_OPT_PARAMETER || opt->option == GMT_OPT_INFILE || opt->arg == NULL) continue;
		for (i = 0; i < sizeof(single_dash_long_options) / sizeof(single_dash_long_options[0]); i++) {
			const char *name = single_dash_long_options[i];
			if (name[0] == opt->option && strcmp(name + 1, opt->arg) == 0) {
				char *arg = strdup(name);
				if (arg == NULL) break;
				free(opt->arg);
				opt->arg = arg;
				opt->option = GMT_OPT_PARAMETER;
				break;
			}
		}
	}
}

/* Port of parse_options(): the original's switch, over the short options and the
 * long options in command-line order. */
static int parse_mbmesh(struct GMT_CTRL *GMT, struct MBMESH_CTRL *Ctrl, struct GMT_OPTION *options) {
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt = NULL;
	struct GMTAPI_CTRL *API = GMT->parent;
	struct mbmesh_options *o = &Ctrl->options;

	for (opt = options; opt; opt = opt->next) {
		char option = opt->option;
		const char *arg = opt->arg;
		if (option == MB_GMT_LONGOPT) {	/* a program long option: the letter it is */
			const char *value;
			const int k = mb_gmt_long_option(opt, long_options, &value);
			if (k < 0) {
				GMT_Report(API, GMT_MSG_ERROR, "Option --%s %s\n", opt->arg, k == -2 ? "requires an argument" : "is not recognized");
				n_errors++;
				continue;
			}
			if (long_letter[k] == 0) {
				if (long_options[k].has_arg)
					(void)parse_long_only_option_with_argument(o, long_options[k].name, value);
				else
					(void)parse_long_only_option(o, long_options[k].name);
				continue;
			}
			option = long_letter[k];
			arg = value;
		}
		switch (option) {

		case '<':
			/* The original falls back to the first positional argument when no
			 * -I was given: "if (options.input_datalist.empty() && optind < argc)". */
			if (o->input_datalist[0] == '\0') {
				strncpy(o->input_datalist, arg, MBMESH_PATH_MAXLINE - 1);
			}
			break;

		case 'I':
			strncpy(o->input_datalist, arg, MBMESH_PATH_MAXLINE - 1);
			break;

		case 'O':
			strncpy(o->output_directory, arg, MBMESH_PATH_MAXLINE - 1);
			break;

		case 'R': {
			double west = 0.0, east = 0.0, south = 0.0, north = 0.0;
			if (sscanf(arg, "%lf/%lf/%lf/%lf", &west, &east, &south, &north) == 4) {
				o->degrees_W = west;
				o->degrees_E = east;
				o->degrees_S = south;
				o->degrees_N = north;
				o->use_bounds = true;
			}
			else {
				char message[MBMESH_MESSAGE_MAXLINE];
				snprintf(message, sizeof(message), "invalid bounds argument: %s", arg);
				log_warning(API, message);
			}
			break;
		}

		case 'L':
			o->level_of_detail = strtod(arg, NULL);
			o->level_of_detail_requested = true;
			break;

		case 'V':
			o->verbose = true;
			break;

		case 'H':
		case 'h':
			o->help_requested = true;
			break;

		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}

	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

/* ======================================================================================================== */

/* Port of write_outputs(). Each optional product is written in the same order
 * as the original, and each success is logged with the same "wrote <path>"
 * line. The PointCloud that the original builds from the decimated samples is
 * built on the C++ side of the bridge, from the same decimated cloud. */
static bool write_outputs(struct GMTAPI_CTRL *API, const mbmesh_mesh_t *mesh, const mbmesh_mesh_t *raw_mesh,
                          const mbmesh_oriented_t *oriented_points, const mbmesh_collected_t *collected_points,
                          const mbmesh_preprocessed_t *preprocessed, const struct mbmesh_options *options,
                          char *error, size_t error_length) {
	char path[MBMESH_PATH_MAXLINE];
	char message[MBMESH_MESSAGE_MAXLINE];

	if (options->output_directory[0] == '\0') {
		if (error != NULL) {
			snprintf(error, error_length, "Output directory path is empty");
		}
		return false;
	}

	if (!mbmesh_create_output_directory(options->output_directory, error, error_length)) {
		return false;
	}

	if (options->write_ecef_xyz) {
		mbmesh_path_join(options->output_directory, "pointcloud-ecef.xyz", path, sizeof(path));
		if (!mbmesh_write_ecef_xyz_pointcloud(collected_points, preprocessed, path, error, error_length)) {
			return false;
		}
		snprintf(message, sizeof(message), "wrote %s", path);
		log_message(API, "output", message, options->verbose, false);
	}

	if (options->write_local_xyz) {
		mbmesh_path_join(options->output_directory, "pointcloud-local.xyz", path, sizeof(path));
		if (!mbmesh_write_local_xyz_pointcloud(collected_points, path, error, error_length)) {
			return false;
		}
		snprintf(message, sizeof(message), "wrote %s", path);
		log_message(API, "output", message, options->verbose, false);
	}

	if (options->write_oriented_ply) {
		mbmesh_path_join(options->output_directory, "oriented-pointcloud-ecef.ply", path, sizeof(path));
		if (!mbmesh_write_ply_oriented_pointcloud(oriented_points, preprocessed, path, error, error_length)) {
			return false;
		}
		snprintf(message, sizeof(message), "wrote %s", path);
		log_message(API, "output", message, options->verbose, false);
	}

	if (options->write_pointcloud_glb) {
		mbmesh_path_join(options->output_directory, "pointcloud.glb", path, sizeof(path));
		if (!mbmesh_write_pointcloud_glb_file(path, collected_points, error, error_length)) {
			return false;
		}
		snprintf(message, sizeof(message), "wrote %s", path);
		log_message(API, "output", message, options->verbose, false);
	}

	if (options->write_normal_glb) {
		mbmesh_path_join(options->output_directory, "normals.glb", path, sizeof(path));
		if (!mbmesh_write_normal_lines_glb_file(path, oriented_points, 0.25, error, error_length)) {
			return false;
		}
		snprintf(message, sizeof(message), "wrote %s", path);
		log_message(API, "output", message, options->verbose, false);
	}

	if (options->write_origin_glb) {
		mbmesh_path_join(options->output_directory, "origins.glb", path, sizeof(path));
		if (!mbmesh_write_origin_ray_lines_glb_file(path, collected_points, 0.25, error, error_length)) {
			return false;
		}
		snprintf(message, sizeof(message), "wrote %s", path);
		log_message(API, "output", message, options->verbose, false);
	}

	if (options->write_raw_mesh_glb) {
		mbmesh_path_join(options->output_directory, "raw_mesh.glb", path, sizeof(path));
		if (!mbmesh_write_mesh_glb_file(path, raw_mesh, error, error_length)) {
			return false;
		}
		snprintf(message, sizeof(message), "wrote %s", path);
		log_message(API, "output", message, options->verbose, false);
	}

	mbmesh_path_join(options->output_directory, "mesh.glb", path, sizeof(path));
	if (!mbmesh_write_mesh_glb_file(path, mesh, error, error_length)) {
		return false;
	}
	snprintf(message, sizeof(message), "wrote %s", path);
	log_message(API, "output", message, options->verbose, false);

	if (options->write_html) {
		mbmesh_path_join(options->output_directory, "mesh.html", path, sizeof(path));
		if (!mbmesh_write_glb_x3dom_file(path, "mesh.glb", error, error_length)) {
			return false;
		}
		snprintf(message, sizeof(message), "wrote %s", path);
		log_message(API, "output", message, options->verbose, false);
	}

	return true;
}

/* ======================================================================================================== */

/* Quotes a value for the shell, doubling up on the single-quote escape the way
 * the original shell_quote() does. */
static void shell_quote(const char *value, char *quoted, size_t quoted_length) {
	size_t out = 0;
	size_t i;

	if (quoted_length == 0) {
		return;
	}
	if (out + 1 < quoted_length) {
		quoted[out++] = '\'';
	}
	for (i = 0; value[i] != '\0'; i++) {
		if (value[i] == '\'') {
			const char *escape = "'\\''";
			size_t j;
			for (j = 0; escape[j] != '\0' && out + 1 < quoted_length; j++) {
				quoted[out++] = escape[j];
			}
		}
		else if (out + 1 < quoted_length) {
			quoted[out++] = value[i];
		}
	}
	if (out + 1 < quoted_length) {
		quoted[out++] = '\'';
	}
	quoted[out] = '\0';
}

/* Port of launch_html_viewer_server(). */
static bool launch_html_viewer_server(struct GMTAPI_CTRL *API, const char *directory, const char *html_filename) {
	const int port = 8000;
	char quoted_directory[MBMESH_PATH_MAXLINE * 4];
	char server_command[MBMESH_MESSAGE_MAXLINE * 4];
	char url[MBMESH_PATH_MAXLINE];
	char open_command[MBMESH_MESSAGE_MAXLINE];
	int server_status;
	int open_status;

	(void)API;

	if (directory[0] == '\0' || html_filename[0] == '\0') {
		return false;
	}

	shell_quote(directory, quoted_directory, sizeof(quoted_directory));

	snprintf(server_command, sizeof(server_command),
	         "python3 -m http.server %d --bind 127.0.0.1 --directory %s >/tmp/mbmesh_http.log 2>&1 &", port,
	         quoted_directory);

	server_status = system(server_command);
	if (server_status != 0) {
		return false;
	}

	snprintf(url, sizeof(url), "http://127.0.0.1:%d/%s", port, html_filename);
	snprintf(open_command, sizeof(open_command), "python3 -c \"import webbrowser; webbrowser.open('%s')\"", url);

	open_status = system(open_command);
	if (open_status != 0) {
		return false;
	}

	fprintf(stderr, "mbmesh: [output] started Python web server at %s\n", url);
	fprintf(stderr, "mbmesh: [output] server logs: /tmp/mbmesh_http.log\n");
	return true;
}

/* ======================================================================================================== */

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code)  { mbmesh_mesh_destroy(clean_mesh); mbmesh_mesh_destroy(raw_mesh); \
                        mbmesh_grid_destroy(poisson_surface); mbmesh_oriented_destroy(oriented_points); \
                        mbmesh_collected_destroy(decimated_points); mbmesh_collected_destroy(collected_points); \
                        mbmesh_preprocessed_destroy(preprocessed); \
                        Free_mbmesh_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }

EXTERN_MSC int GMT_mbmesh(void *V_API, int mode, void *args);

/* Port of main(). */
int GMT_mbmesh(void *V_API, int mode, void *args) {

	struct MBMESH_CTRL *Ctrl = NULL;
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct mbmesh_options *o = NULL;

	/* Every handle is declared here and NULL-initialised so that Return() can
	 * release whatever has been built so far, whichever stage fails. */
	mbmesh_preprocessed_t *preprocessed = NULL;
	mbmesh_collected_t *collected_points = NULL;
	mbmesh_collected_t *decimated_points = NULL;
	mbmesh_oriented_t *oriented_points = NULL;
	mbmesh_grid_t *poisson_surface = NULL;
	mbmesh_mesh_t *raw_mesh = NULL;
	mbmesh_mesh_t *clean_mesh = NULL;

	struct mbmesh_trimming_diagnostics trimming_diagnostics;
	char error[MBMESH_ERROR_MAXLINE] = "";
	char message[MBMESH_MESSAGE_MAXLINE];
	char path[MBMESH_PATH_MAXLINE];
	size_t input_sample_count = 0;
	int parse_status;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 1: no options is a run of the program (which then reports the empty datalist) */
	if ((parse_status = gmt_report_usage(API, options, 1, usage)) != GMT_NOERROR) bailout(parse_status);

	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS, NULL,
	                           &options, &GMT_cpy)) == NULL)
		bailout(API->error);
	/* getopt's "-I datalist" form, then the program's long options (and their
	 * single-dash spellings) kept out of GMT's --PAR=value handling */
	mb_gmt_join_separated_values(API, &options, "IORL");
	mark_single_dash_aliases(options);
	mb_gmt_mark_long_options(API, &options, long_options);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	Ctrl = (struct MBMESH_CTRL *)New_mbmesh_Ctrl(GMT);
	if ((parse_status = parse_mbmesh(GMT, Ctrl, options)) != 0) Return(parse_status);

	o = &Ctrl->options;

	if (o->help_requested) Return(usage(API, GMT_USAGE));

	snprintf(message, sizeof(message), "input=%s output=%s", o->input_datalist, o->output_directory);
	log_message(API, "startup", message, o->verbose, true);

	if (o->input_datalist[0] == '\0') {
		log_fatal(API, "input datalist path is empty; use -I <path>");
		Return(GMT_RUNTIME_ERROR);
	}

	snprintf(message, sizeof(message), "reading datalist %s", o->input_datalist);
	log_message(API, "input", message, o->verbose, false);

	if (!mbmesh_preprocess_datalist(o, &preprocessed, error, sizeof(error))) {
		log_fatal(API, error);
		Return(GMT_RUNTIME_ERROR);
	}

	snprintf(message, sizeof(message), "completed: accepted %llu of %llu soundings from %llu files",
	         (unsigned long long)mbmesh_preprocessed_accepted_points(preprocessed),
	         (unsigned long long)mbmesh_preprocessed_soundings_read(preprocessed),
	         (unsigned long long)mbmesh_preprocessed_files_read(preprocessed));
	log_message(API, "input", message, o->verbose, false);

	if (o->metadata_requested) {
		char *text = mbmesh_datalist_metadata_text(preprocessed, o);
		struct MB_GMT_TEXT *T;
		if (text == NULL) {
			GMT_Report(API, GMT_MSG_ERROR, "Out of memory\n");
			Return(GMT_MEMORY_ERROR);
		}
		if ((T = mb_gmt_text_begin(GMT, options)) == NULL) {
			free(text);
			Return(API->error);
		}
		mb_gmt_text_put(T, "%s", text);
		free(text);
		if (mb_gmt_text_end(T)) Return(API->error);
		Return(GMT_NOERROR);
	}

	collected_points = mbmesh_preprocessed_take_points(preprocessed);

	/* ====================================================================================================
	 * Point Decimation
	 * ==================================================================================================== */

	input_sample_count = mbmesh_collected_size(collected_points);

	if (o->decimation_decimate) {

		snprintf(message, sizeof(message), "starting: cell_size=%f", o->decimation_cell_size);
		log_message(API, "decimation", message, o->verbose, false);

		/* point_decimation() consumes its input, so the handle is spent here. */
		decimated_points = mbmesh_point_decimation(collected_points, o);
		collected_points = NULL;

		if (mbmesh_collected_size(decimated_points) == 0) {
			log_fatal(API, "point decimation produced no samples");
			Return(GMT_RUNTIME_ERROR);
		}
		snprintf(message, sizeof(message), "completed: retained %llu of %llu samples",
		         (unsigned long long)mbmesh_collected_size(decimated_points),
		         (unsigned long long)input_sample_count);
		log_message(API, "decimation", message, o->verbose, false);

	}
	else {
		snprintf(message, sizeof(message), "disabled; using %llu input samples", (unsigned long long)input_sample_count);
		log_message(API, "decimation", message, o->verbose, false);
		decimated_points = collected_points;
		collected_points = NULL;
	}

	/* ====================================================================================================
	 * Normal Estimation
	 * ==================================================================================================== */

	if (o->verbose) {
		const bool using_search_radius = o->normals_search_radius > 0.0;
		snprintf(message, sizeof(message),
		         "starting: search_radius=%g, radius_mode=%s, k_nearest=%llu, fallback_min_neighbors=%llu, "
		         "fallback_to_knearest=%s",
		         o->normals_search_radius, using_search_radius ? "enabled" : "disabled",
		         (unsigned long long)o->normals_k, (unsigned long long)o->normals_minimum_neighbors,
		         using_search_radius ? "when radius neighborhood < minimum_neighbors" : "always");
		log_message(API, "normal-estimation", message, o->verbose, false);
	}

	oriented_points = mbmesh_normal_estimation(decimated_points, o);

	if (mbmesh_oriented_size(oriented_points) == 0) {
		log_fatal(API, "normal estimation produced no oriented samples");
		Return(GMT_RUNTIME_ERROR);
	}
	snprintf(message, sizeof(message), "completed: oriented %llu samples",
	         (unsigned long long)mbmesh_oriented_size(oriented_points));
	log_message(API, "normal-estimation", message, o->verbose, false);

	/* ====================================================================================================
	 * Screened Poisson
	 * ==================================================================================================== */

	if (o->verbose) {
		snprintf(message, sizeof(message),
		         "starting: cell_size=%g, padding=%g, splat_radius=%g, iterations=%d, screening=%s, "
		         "screening_weight=%g",
		         o->poisson_cell_size, o->poisson_padding, o->poisson_normal_splat_radius,
		         o->poisson_solver_iterations, o->poisson_use_screening ? "enabled" : "disabled",
		         o->poisson_screening_weight);
		log_message(API, "poisson", message, o->verbose, false);
	}

	poisson_surface = mbmesh_screened_poisson(oriented_points, o);

	if (mbmesh_grid_value_count(poisson_surface) == 0) {
		log_fatal(API, "screened Poisson reconstruction produced an empty field");
		Return(GMT_RUNTIME_ERROR);
	}

	snprintf(message, sizeof(message), "completed: grid=%llux%llux%llu", (unsigned long long)mbmesh_grid_nx(poisson_surface),
	         (unsigned long long)mbmesh_grid_ny(poisson_surface), (unsigned long long)mbmesh_grid_nz(poisson_surface));
	log_message(API, "poisson", message, o->verbose, false);

	/* ====================================================================================================
	 * Marching Cubes
	 * ==================================================================================================== */

	snprintf(message, sizeof(message), "starting: iso_value=%f", o->marching_cubes_iso_value);
	log_message(API, "marching-cubes", message, o->verbose, false);

	raw_mesh = mbmesh_marching_cubes(poisson_surface, o);

	if (mbmesh_mesh_vertex_count(raw_mesh) == 0 || mbmesh_mesh_index_count(raw_mesh) == 0) {
		log_fatal(API, "marching cubes produced an empty mesh");
		Return(GMT_RUNTIME_ERROR);
	}

	snprintf(message, sizeof(message), "completed: %llu vertices, %llu triangles",
	         (unsigned long long)mbmesh_mesh_vertex_count(raw_mesh),
	         (unsigned long long)(mbmesh_mesh_index_count(raw_mesh) / 3));
	log_message(API, "marching-cubes", message, o->verbose, false);

	/* ====================================================================================================
	 * Support Trimming
	 * ==================================================================================================== */

	memset(&trimming_diagnostics, 0, sizeof(trimming_diagnostics));

	if (o->trimming_enabled) {
		if (o->verbose) {
			char radius[64];
			char normal_offset[64];
			char alignment[64];

			if (o->trimming_support_radius > 0.0)
				snprintf(radius, sizeof(radius), "%f", o->trimming_support_radius);
			else
				snprintf(radius, sizeof(radius), "auto");

			if (o->trimming_max_normal_offset > 0.0)
				snprintf(normal_offset, sizeof(normal_offset), "%f", o->trimming_max_normal_offset);
			else
				snprintf(normal_offset, sizeof(normal_offset), "auto");

			if (o->trimming_minimum_normal_alignment > 0.0)
				snprintf(alignment, sizeof(alignment), "%f", o->trimming_minimum_normal_alignment);
			else
				snprintf(alignment, sizeof(alignment), "disabled");

			snprintf(message, sizeof(message),
			         "starting: radius=%s, normal_offset=%s, minimum_neighbors=%d, minimum_normal_alignment=%s",
			         radius, normal_offset, o->trimming_minimum_neighbors, alignment);
			log_message(API, "support-trimming", message, o->verbose, false);
		}
		clean_mesh = mbmesh_support_trimming(raw_mesh, oriented_points, o, &trimming_diagnostics);
	}
	else {
		log_message(API, "support-trimming", "disabled; using raw mesh", o->verbose, false);
		clean_mesh = mbmesh_mesh_copy(raw_mesh);
	}

	if (mbmesh_mesh_vertex_count(clean_mesh) == 0 || mbmesh_mesh_index_count(clean_mesh) == 0) {
		log_fatal(API, "support trimming removed the entire mesh; adjust the trimming thresholds or disable trimming");
		Return(GMT_RUNTIME_ERROR);
	}

	if (o->verbose && o->trimming_enabled) {
		snprintf(message, sizeof(message),
		         "completed: spacing=%g, resolved_radius=%g, resolved_normal_offset=%g, supported_vertices=%llu/%llu, "
		         "rejected=(neighbors=%llu, offset=%llu, alignment=%llu), output=%llu vertices, %llu triangles",
		         trimming_diagnostics.estimated_point_spacing, trimming_diagnostics.resolved_support_radius,
		         trimming_diagnostics.resolved_max_normal_offset,
		         (unsigned long long)trimming_diagnostics.supported_vertices,
		         (unsigned long long)trimming_diagnostics.input_vertices,
		         (unsigned long long)trimming_diagnostics.rejected_for_neighbors,
		         (unsigned long long)trimming_diagnostics.rejected_for_normal_offset,
		         (unsigned long long)trimming_diagnostics.rejected_for_normal_alignment,
		         (unsigned long long)mbmesh_mesh_vertex_count(clean_mesh),
		         (unsigned long long)(mbmesh_mesh_index_count(clean_mesh) / 3));
		log_message(API, "support-trimming", message, o->verbose, false);
	}

	/* ====================================================================================================
	 * Write Outputs
	 * ==================================================================================================== */

	if (o->verbose) {
		snprintf(message, sizeof(message),
		         "starting: directory=%s, glb=enabled, html=%s, local_xyz=%s, ecef_xyz=%s, oriented_ply=%s, "
		         "pointcloud_glb=%s, normal_glb=%s, origin_glb=%s, raw_mesh_glb=%s",
		         o->output_directory, o->write_html ? "enabled" : "disabled",
		         o->write_local_xyz ? "enabled" : "disabled", o->write_ecef_xyz ? "enabled" : "disabled",
		         o->write_oriented_ply ? "enabled" : "disabled", o->write_pointcloud_glb ? "enabled" : "disabled",
		         o->write_normal_glb ? "enabled" : "disabled", o->write_origin_glb ? "enabled" : "disabled",
		         o->write_raw_mesh_glb ? "enabled" : "disabled");
		log_message(API, "output", message, o->verbose, false);
	}

	if (!write_outputs(API, clean_mesh, raw_mesh, oriented_points, decimated_points, preprocessed, o, error,
	                   sizeof(error))) {
		log_fatal(API, error);
		Return(GMT_RUNTIME_ERROR);
	}

	log_message(API, "output", "completed", o->verbose, false);
	mbmesh_path_join(o->output_directory, "mesh.glb", path, sizeof(path));
	snprintf(message, sizeof(message), "wrote %s", path);
	log_message(API, "complete", message, o->verbose, true);

	/* ====================================================================================================
	 * Launch Local Server for X3DOM Viewer
	 * ==================================================================================================== */

	if (o->write_html && !launch_html_viewer_server(API, o->output_directory, "mesh.html")) {
		log_warning(API, "failed to auto-launch Python web server/viewer");
	}

	Return(GMT_NOERROR);
}
