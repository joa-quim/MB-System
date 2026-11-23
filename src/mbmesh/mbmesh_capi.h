/*--------------------------------------------------------------------
 *    The MB-system:  mbmesh_capi.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Plain C interface to the mbmesh program, for the GMT module src/gmt/mbmesh.c.
 *
 * The module hands over the command line of the program and the program runs
 * as it does on its own: mbmesh_program.cpp builds it into this library. Every
 * option of the program, its checks and its messages are then those of the
 * module too.
 */

#ifndef MBMESH_CAPI_H
#define MBMESH_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the mbmesh program with argc/argv (argv[0] the program name) and returns
 * its exit status. Where the program would end the process on a bad option, it
 * returns that status instead. When captured_output is not NULL, what the program
 * writes to standard output (the --info listing, the usage) is returned there,
 * allocated with malloc() instead of being written; NULL if there is no memory. */
int mbmesh_run_program(int argc, char **argv, char **captured_output);

#ifdef __cplusplus
}
#endif

#endif /* MBMESH_CAPI_H */
