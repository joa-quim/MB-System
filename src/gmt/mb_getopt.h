/*--------------------------------------------------------------------
 *    The MB-system:  mb_getopt.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Reentrant getopt_long() for the GMT modules of the mbsystem supplement.
 *
 * Programs ported to GMT modules keep their original getopt_long() option
 * loop. getopt_long() keeps its state in globals (optind, optarg), so a
 * second run in the same GMT session - the normal case for GMT.jl, PyGMT
 * or MATLAB - would parse nothing. This version keeps all of its state in
 * a caller-owned structure and behaves like GNU getopt_long(): short
 * options with or without an attached or separate argument, long options
 * as --name, --name=value or --name value, unambiguous long-name prefixes,
 * and non-option arguments skipped over.
 *
 * mb_getopt_args_build() turns the three argument shapes a GMT module can
 * receive (argv[] array, command string, GMT option list) into one argv[].
 */

#ifndef MB_GETOPT_H_
#define MB_GETOPT_H_

#define mb_no_argument 0
#define mb_required_argument 1
#define mb_optional_argument 2

struct mb_getopt_option {
	const char *name;
	int has_arg;
	int *flag;
	int val;
};

struct mb_getopt_state {
	int optind;   /* index of the next argv element to scan */
	char *optarg; /* argument of the option just returned */
	int optopt;   /* option character that caused an error */
	int opterr;   /* print error messages when nonzero */
	int sp;       /* position inside a group of short options */
};

/* initialise a parsing state (optind = 1, opterr = 1) */
void mb_getopt_init(struct mb_getopt_state *state);

/* the reentrant getopt_long() */
int mb_getopt_long(struct mb_getopt_state *state, int argc, char *const argv[], const char *shortopts,
                   const struct mb_getopt_option *longopts, int *longindex);

/* Build argv[] (argv[0] = program) from a GMT module's (mode, args):
 * mode > 0: args is an argv[] array of mode entries; mode == 0 (GMT_MODULE_CMD):
 * args is one command string; mode < 0: args is a struct GMT_OPTION list.
 * Returns argc; *argv_out and its strings are freed by mb_getopt_args_free(). */
int mb_getopt_args_build(const char *program, int mode, void *args, char ***argv_out);
void mb_getopt_args_free(int argc, char **argv);

#endif
