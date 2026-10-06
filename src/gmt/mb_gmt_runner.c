/*--------------------------------------------------------------------
 *    The MB-system:	mb_gmt_runner.c
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * The plugin's program runner for libmbio (mb_set_program_runner, mb_define.h).
 *
 * When a data file has no up-to-date .inf / .fbt / .fnv, libmbio's mb_make_info() makes them by
 * running mbinfo, mbcopy and mblist. Standalone, those are the separate executables, run through
 * system(). Inside this plugin there are none -- the programs are modules of this very library --
 * so mbgrid and friends got no bounds on a plugin-only install. This runner calls the module
 * functions directly, in a short GMT session of their own.
 *
 * It is installed when the library is loaded (a constructor), so it covers every module without
 * any of them having to remember it.
 */

#include "gmt_dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb_define.h"

EXTERN_MSC int GMT_mbinfo(void *V_API, int mode, void *args);
EXTERN_MSC int GMT_mbcopy(void *V_API, int mode, void *args);
EXTERN_MSC int GMT_mblist(void *V_API, int mode, void *args);

static int mb_gmt_run_program(const char *program, const char *args, const char *outfile) {
	int (*module)(void *, int, void *) = NULL;
	if (strcmp(program, "mbinfo") == 0) module = GMT_mbinfo;
	else if (strcmp(program, "mbcopy") == 0) module = GMT_mbcopy;
	else if (strcmp(program, "mblist") == 0) module = GMT_mblist;
	if (module == NULL) {
		fprintf(stderr, "mb_gmt_run_program: %s is not a module of this plugin\n", program);
		return -1;
	}

	/* Output to a file: mblist's own -X<file>, then appended to `outfile`. Not GMT's ">> file": outside
	   an external session mblist writes its listing straight to stdout, and the stdout written that way
	   leaves the next GMT_Create_Session of this process failing (mb_write_gmt_grd's, in mbgrid). */
	char cmd[3 * MB_PATH_MAXLINE];
	char tmpfile[MB_PATH_MAXLINE + 8] = "";
	if (outfile != NULL) {
		if (module != GMT_mblist) {
			fprintf(stderr, "mb_gmt_run_program: %s cannot write to a file\n", program);
			return -1;
		}
		snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", outfile);
		snprintf(cmd, sizeof(cmd), "%s -X%s", args, tmpfile);
	}
	else
		snprintf(cmd, sizeof(cmd), "%s", args);

	/* A session of its own: the module that asked is in the middle of its run. NOGDALCLOSE, or
	   destroying it would shut GDAL down under the outer session too. */
	void *API = GMT_Create_Session("mbsystem", GMT_PAD_DEFAULT, GMT_SESSION_NOEXIT | GMT_SESSION_NOHISTORY | GMT_SESSION_NOGDALCLOSE, NULL);
	if (API == NULL) return -1;
	int status = module(API, GMT_MODULE_CMD, cmd);
	GMT_Destroy_Session(API);

	if (outfile != NULL) {
		if (status == 0) {
			FILE *in = fopen(tmpfile, "rb");
			FILE *out = fopen(outfile, "ab");
			if (in == NULL || out == NULL)
				status = -1;
			else {
				char buf[8192];
				size_t n;
				while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
					if (fwrite(buf, 1, n, out) != n) {
						status = -1;
						break;
					}
			}
			if (in != NULL) fclose(in);
			if (out != NULL) fclose(out);
		}
		remove(tmpfile);
	}
	return status;
}

/* GMT unloads a custom library when the session that loaded it ends, while libmbio stays: the runner
   is withdrawn with the library, so libmbio never calls into code that is gone (it falls back to
   system() until the plugin is loaded, and installs it, again). */
static void mb_gmt_runner_uninstall(void) {
	mb_set_program_runner(NULL);
}

static void mb_gmt_runner_install(void) {
	mb_set_program_runner(mb_gmt_run_program);
#if defined(_MSC_VER)
	atexit(mb_gmt_runner_uninstall);	/* in a DLL, the CRT runs these when the DLL is unloaded */
#endif
}

#if defined(_MSC_VER)
/* MSVC: a pointer in .CRT$XCU is called by the CRT when the DLL loads; /include keeps it from being
   dropped as unreferenced. */
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) void (*mb_gmt_runner_install_ptr)(void) = mb_gmt_runner_install;
#if defined(_WIN64)
#pragma comment(linker, "/include:mb_gmt_runner_install_ptr")
#else
#pragma comment(linker, "/include:_mb_gmt_runner_install_ptr")
#endif
#else
/* GCC, Clang (MinGW included) */
__attribute__((constructor)) static void mb_gmt_runner_ctor(void) {
	mb_gmt_runner_install();
}
__attribute__((destructor)) static void mb_gmt_runner_dtor(void) {
	mb_gmt_runner_uninstall();
}
#endif
