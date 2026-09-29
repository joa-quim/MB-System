/*--------------------------------------------------------------------
 *    The MB-system:  mb_getopt.c
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Reentrant getopt_long() for the GMT modules of the mbsystem supplement.
 * See mb_getopt.h.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gmt.h"
#include "mb_getopt.h"

/*--------------------------------------------------------------------*/
void mb_getopt_init(struct mb_getopt_state *state) {
	state->optind = 1;
	state->optarg = NULL;
	state->optopt = 0;
	state->opterr = 1;
	state->sp = 1;
}

/*--------------------------------------------------------------------*/
static int mb_getopt_long_option(struct mb_getopt_state *state, int argc, char *const argv[],
                                 const struct mb_getopt_option *longopts, int *longindex) {
	const char *name = argv[state->optind] + 2;
	const char *equals = strchr(name, '=');
	const size_t namelen = equals != NULL ? (size_t)(equals - name) : strlen(name);

	/* exact match first, then an unambiguous prefix */
	int match = -1;
	bool ambiguous = false;
	for (int i = 0; longopts != NULL && longopts[i].name != NULL; i++) {
		if (strncmp(longopts[i].name, name, namelen) == 0) {
			if (strlen(longopts[i].name) == namelen) {
				match = i;
				ambiguous = false;
				break;
			}
			if (match < 0)
				match = i;
			else
				ambiguous = true;
		}
	}
	if (match < 0 || ambiguous) {
		if (state->opterr)
			fprintf(stderr, "%s: %s option '--%.*s'\n", argv[0], ambiguous ? "ambiguous" : "unrecognized", (int)namelen,
			        name);
		state->optind++;
		state->optopt = 0;
		return '?';
	}

	const struct mb_getopt_option *lo = &longopts[match];
	state->optind++;
	state->optarg = NULL;
	if (equals != NULL) {
		if (lo->has_arg == mb_no_argument) {
			if (state->opterr)
				fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n", argv[0], lo->name);
			state->optopt = lo->val;
			return '?';
		}
		state->optarg = (char *)equals + 1;
	}
	else if (lo->has_arg == mb_required_argument) {
		if (state->optind < argc) {
			state->optarg = argv[state->optind];
			state->optind++;
		}
		else {
			if (state->opterr)
				fprintf(stderr, "%s: option '--%s' requires an argument\n", argv[0], lo->name);
			state->optopt = lo->val;
			return '?';
		}
	}
	if (longindex != NULL)
		*longindex = match;
	if (lo->flag != NULL) {
		*lo->flag = lo->val;
		return 0;
	}
	return lo->val;
}

/*--------------------------------------------------------------------*/
int mb_getopt_long(struct mb_getopt_state *state, int argc, char *const argv[], const char *shortopts,
                   const struct mb_getopt_option *longopts, int *longindex) {
	if (state->optind <= 0) {
		state->optind = 1;
		state->sp = 1;
	}
	state->optarg = NULL;

	if (state->sp == 1) {
		/* skip non-option arguments, as GNU getopt's permutation does */
		while (state->optind < argc && (argv[state->optind][0] != '-' || argv[state->optind][1] == '\0'))
			state->optind++;
		if (state->optind >= argc)
			return -1;
		if (strcmp(argv[state->optind], "--") == 0) {
			state->optind++;
			return -1;
		}
		if (argv[state->optind][1] == '-')
			return mb_getopt_long_option(state, argc, argv, longopts, longindex);
	}

	/* short option */
	const char *arg = argv[state->optind];
	const int c = (unsigned char)arg[state->sp];
	const char *spec = (c != ':') ? strchr(shortopts, c) : NULL;
	if (spec == NULL) {
		if (state->opterr)
			fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], c);
		state->optopt = c;
		if (arg[++state->sp] == '\0') {
			state->optind++;
			state->sp = 1;
		}
		return '?';
	}
	if (spec[1] == ':') {
		if (arg[state->sp + 1] != '\0') {
			state->optarg = (char *)&arg[state->sp + 1];
			state->optind++;
		}
		else if (spec[2] == ':') {
			/* optional argument, none attached */
			state->optind++;
		}
		else if (state->optind + 1 < argc) {
			state->optarg = argv[state->optind + 1];
			state->optind += 2;
		}
		else {
			if (state->opterr)
				fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
			state->optopt = c;
			state->optind++;
			state->sp = 1;
			return '?';
		}
		state->sp = 1;
	}
	else if (arg[++state->sp] == '\0') {
		state->optind++;
		state->sp = 1;
	}
	return c;
}

/*--------------------------------------------------------------------*/
static int mb_getopt_args_add(int *argc, int *alloc, char ***argv, const char *text, size_t len) {
	if (*argc + 2 > *alloc) {
		const int n = *alloc > 0 ? 2 * *alloc : 16;
		char **grown = (char **)realloc(*argv, n * sizeof(char *));
		if (grown == NULL)
			return -1;
		*argv = grown;
		*alloc = n;
	}
	char *s = (char *)malloc(len + 1);
	if (s == NULL)
		return -1;
	memcpy(s, text, len);
	s[len] = '\0';
	(*argv)[(*argc)++] = s;
	(*argv)[*argc] = NULL;
	return 0;
}

/*--------------------------------------------------------------------*/
int mb_getopt_args_build(const char *program, int mode, void *args, char ***argv_out) {
	int argc = 0;
	int alloc = 0;
	char **argv = NULL;
	mb_getopt_args_add(&argc, &alloc, &argv, program, strlen(program));

	if (mode > 0 && args != NULL) {
		/* argv[] array of mode entries (the gmt executable) */
		char **in = (char **)args;
		for (int i = 0; i < mode; i++)
			if (in[i] != NULL)
				mb_getopt_args_add(&argc, &alloc, &argv, in[i], strlen(in[i]));
	}
	else if (mode == 0 && args != NULL) {
		/* one command string (the C API): split on blanks, honouring quotes */
		const char *p = (const char *)args;
		char *token = (char *)malloc(strlen(p) + 1);
		if (token != NULL) {
			while (*p != '\0') {
				while (*p == ' ' || *p == '\t')
					p++;
				if (*p == '\0')
					break;
				size_t n = 0;
				char quote = '\0';
				while (*p != '\0' && (quote != '\0' || (*p != ' ' && *p != '\t'))) {
					if (quote == '\0' && (*p == '"' || *p == '\''))
						quote = *p;
					else if (quote != '\0' && *p == quote)
						quote = '\0';
					else
						token[n++] = *p;
					p++;
				}
				mb_getopt_args_add(&argc, &alloc, &argv, token, n);
			}
			free(token);
		}
	}
	else if (mode < 0 && args != NULL) {
		/* a GMT option list (the external interfaces) */
		for (struct GMT_OPTION *opt = (struct GMT_OPTION *)args; opt != NULL; opt = opt->next) {
			const char *value = opt->arg != NULL ? opt->arg : "";
			if (opt->option == GMT_OPT_INFILE) {
				mb_getopt_args_add(&argc, &alloc, &argv, value, strlen(value));
			}
			else {
				/* "--name=value" was kept by GMT as option '-' with argument "name=value" */
				const size_t n = strlen(value) + 3;
				char *s = (char *)malloc(n + 1);
				if (s == NULL)
					continue;
				snprintf(s, n + 1, "-%c%s", opt->option, value);
				mb_getopt_args_add(&argc, &alloc, &argv, s, strlen(s));
				free(s);
			}
		}
	}
	*argv_out = argv;
	return argc;
}

/*--------------------------------------------------------------------*/
void mb_getopt_args_free(int argc, char **argv) {
	if (argv == NULL)
		return;
	for (int i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
}
