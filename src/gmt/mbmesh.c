/*--------------------------------------------------------------------
 *    The MB-system:  mbmesh.c
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * mbmesh generates 3D meshes from swath sonar bathymetry data.
 *
 * GMT module of src/mbmesh/mbmesh.cpp. The program itself is built into the
 * mbmesh_pipeline library by src/mbmesh/mbmesh_program.cpp and is run through
 * the plain C interface in src/mbmesh/mbmesh_capi.h, so the module has every
 * option of the program, with its checks, its messages and its log, and keeps
 * them as the program changes. The module only turns GMT's option list back
 * into the program's command line.
 *
 * Option mapping. GMT has no long-option keyword dictionary for an out-of-tree
 * module; mb_gmt_mark_long_options() keeps the program's long options, listed
 * below, out of GMT's --PAR=value handling, and they are handed over in
 * command-line order as --name[=value]. The program's single-dash spellings of
 * some of them, -html, -tileset and others, reach GMT split into a letter and
 * the rest of the word; mark_single_dash_aliases() puts each back into its long
 * form first. Short options go over as -<letter><value>.
 */

#define THIS_MODULE_NAME    "mbmesh"
#define THIS_MODULE_LIB     "mbsystem"
#define THIS_MODULE_PURPOSE "Generate 3D meshes from swath sonar bathymetry data"
/* -I carries the input datalist, which is the module's one primary resource, so
 * an external API such as Julia, Python or MATLAB binds its input argument to -I.
 * The output key carries the --info listing; mbmesh writes its meshes, tilesets
 * and pages itself. */
#define THIS_MODULE_KEYS    "ID{,>D}"
#define THIS_MODULE_NEEDS   ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

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

/* The usage is the program's own, so that it always lists the options the
 * program has. */
static int usage(struct GMTAPI_CTRL *API, int level) {
	char *help_argv[] = {"mbmesh", "--help", NULL};
	char *text = NULL;

	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;

	GMT_Message(API, GMT_TIME_NONE, "usage: mbmesh -I <datalist> [options]\n\n");
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;

	(void)mbmesh_run_program(2, help_argv, &text);
	if (text != NULL) {
		GMT_Message(API, GMT_TIME_NONE, "%s", text);
		free(text);
	}
	return GMT_MODULE_USAGE;
}

/* ======================================================================================================== */

/* The long options of the getopt_long table of the program, parse_options() in
 * src/mbmesh/mbmesh.cpp, with has_arg set for its required_argument ones.
 * --geoorigin and --lift take an optional argument, which must be attached as
 * in --lift=100, so they are listed without one. --metadata is not the
 * program's, but it is handed over so that the program can say that it was
 * renamed --info. */
static const struct MB_GMT_LONGOPT_DEF long_options[] = {
	{"input", true},
	{"output", true},
	{"bounds", true},
	{"lod", true},
	{"level-of-detail", true},
	{"info", false},
	{"metadata", false},
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
	{"geoorigin", false},
	{"chunk-size", true},
	{"chunk-halo", true},
	{"label-chunks", false},
	{"seam-blend", true},
	{"seam-method", true},
	{"no-draco", false},
	{"draco-precision", true},
	{"draco-normal-bits", true},
	{"chunk", true},
	{"max-chunks", true},
	{"threads", true},
	{"track-glb", false},
	{"tileset", false},
	{"x3dom-url", true},
	{"cesium-html", false},
	{"cesium-url", true},
	{"poisson-solver", true},
	{"poisson-iterations", true},
	{"geoviewpoint", true},
	{"lift", false},
	{"no-colors", false},
	{"pointcloud-colormap", true},
	{"spill-points", true},
	{"void-size", true},
	{"island-size", true},
	{"primitive-size", true},
	{"diagnostics", false},
	{"all-outputs", false},
	{"verbose", false},
	{"help", false},
	{NULL, false}};

/* The single_dash_long_options of the program: each word is accepted as the
 * double-dash form of the same name. GMT has split "-html" into option 'h' with
 * argument "tml"; put such an option back as the long option "html" so that
 * mb_gmt_mark_long_options() takes it, together with the value that follows
 * -decimate. Called before mb_gmt_mark_long_options(). */
static void mark_single_dash_aliases(struct GMT_OPTION *options) {
	static const char *single_dash_long_options[] = {
	    "html", "cesium-html", "local-xyz", "ecef-xyz", "xyz", "oriented-ply", "pointcloud-glb", "normal-glb",
	    "origin-glb", "track-glb", "tileset", "raw-mesh-glb", "decimate", "diagnostics", "all-outputs", "metadata"};
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

/* ======================================================================================================== */

/* A word of the command line of the program: prefix followed by text. */
static char *make_word(const char *prefix, const char *text) {
	const size_t length = strlen(prefix) + strlen(text) + 1;
	char *word = (char *)malloc(length);
	if (word != NULL) snprintf(word, length, "%s%s", prefix, text);
	return word;
}

static void free_words(int argc, char **argv) {
	int i;
	if (argv == NULL) return;
	for (i = 0; i < argc; i++) free(argv[i]);
	free(argv);
}

/* Whether a marked long option, "name[=value]", is the option name. */
static bool is_long_option(const char *arg, const char *name) {
	const size_t length = strcspn(arg, "=");
	return length == strlen(name) && strncmp(arg, name, length) == 0;
}

/* The command line of the program from the option list of GMT, in command-line
 * order. Sets *help when the usage was asked for and *info for --info. Returns
 * argc, or -1 when there is no memory. */
static int build_program_args(struct GMT_OPTION *options, char ***argv_out, bool *help, bool *info) {
	struct GMT_OPTION *opt;
	int count = 1;
	int argc = 0;
	char **argv;

	*help = false;
	*info = false;
	*argv_out = NULL;
	for (opt = options; opt; opt = opt->next) count++;
	if ((argv = (char **)calloc((size_t)count + 1, sizeof(char *))) == NULL) return -1;

	if ((argv[argc++] = make_word("", "mbmesh")) == NULL) {
		free_words(argc, argv);
		return -1;
	}
	for (opt = options; opt; opt = opt->next) {
		const char *arg = (opt->arg != NULL) ? opt->arg : "";
		const char letter[3] = {'-', opt->option, '\0'};
		char *word = NULL;

		if (opt->option == MB_GMT_LONGOPT) {
			if (is_long_option(arg, "help")) *help = true;
			if (is_long_option(arg, "info")) *info = true;
			word = make_word("--", arg);
		}
		else if (opt->option == GMT_OPT_INFILE) {
			word = make_word("", arg);   /* a positional argument, as the program sees it */
		}
		else if (opt->option == GMT_OPT_OUTFILE || opt->option == GMT_OPT_PARAMETER) {
			continue;                    /* GMT's own: the output of --info, and --PAR=value settings */
		}
		else if (opt->option == 'V') {
			word = make_word("-V", "");  /* the -V of the program takes no level */
		}
		else {
			if (opt->option == 'H' || opt->option == 'h') *help = true;
			word = make_word(letter, arg);
		}
		if (word == NULL) {
			free_words(argc, argv);
			return -1;
		}
		argv[argc++] = word;
	}
	*argv_out = argv;
	return argc;
}

/* ======================================================================================================== */

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code)  { free_words(program_argc, program_argv); free(output); \
                        gmt_end_module(GMT, GMT_cpy); bailout(code); }

EXTERN_MSC int GMT_mbmesh(void *V_API, int mode, void *args);

int GMT_mbmesh(void *V_API, int mode, void *args) {

	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);

	char **program_argv = NULL;
	int program_argc = 0;
	char *output = NULL;
	bool help = false;
	bool info = false;
	int status;

	if (API == NULL) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 0: no options shows the usage, as the program does */
	if ((status = gmt_report_usage(API, options, 0, usage)) != GMT_NOERROR) bailout(status);

	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS, NULL,
	                           &options, &GMT_cpy)) == NULL)
		bailout(API->error);
	/* the "-I datalist" form of getopt, then the long options of the program and
	 * their single-dash spellings, kept out of the --PAR=value handling of GMT */
	mb_gmt_join_separated_values(API, &options, "IORL");
	mark_single_dash_aliases(options);
	mb_gmt_mark_long_options(API, &options, long_options);
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);

	if ((program_argc = build_program_args(options, &program_argv, &help, &info)) < 0) {
		program_argc = 0;
		GMT_Report(API, GMT_MSG_ERROR, "Out of memory\n");
		Return(GMT_MEMORY_ERROR);
	}

	if (help) Return(usage(API, GMT_USAGE));

	/* --info: the listing goes to the output of the module, so an external API gets it back */
	status = mbmesh_run_program(program_argc, program_argv, info ? &output : NULL);
	if (status != 0) Return(GMT_RUNTIME_ERROR);

	if (info) {
		struct MB_GMT_TEXT *T;
		if (output == NULL) {
			GMT_Report(API, GMT_MSG_ERROR, "Out of memory\n");
			Return(GMT_MEMORY_ERROR);
		}
		if ((T = mb_gmt_text_begin(GMT, options)) == NULL) Return(API->error);
		mb_gmt_text_put(T, "%s", output);
		if (mb_gmt_text_end(T)) Return(API->error);
	}

	Return(GMT_NOERROR);
}
