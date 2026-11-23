/*--------------------------------------------------------------------
 *    The MB-system:  mbmesh_program.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * The mbmesh program as a function of the mbmesh_pipeline library: see
 * mbmesh_program.cpp. Used by mbmesh_capi.cpp only.
 */

#ifndef MBMESH_PROGRAM_H
#define MBMESH_PROGRAM_H

// What std::exit(status) throws inside the program, in place of ending the process.
struct mbmesh_program_exit_request {
    int status;
};

// The main() of mbmesh.cpp.
int mbmesh_program_main(int argc, char **argv);

#endif
