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

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported from the library a host loads (the GMT supplement), for hosts that call it directly. */
#if defined(_WIN32)
#define MBMESH_CAPI_EXPORT __declspec(dllexport)
#else
#define MBMESH_CAPI_EXPORT __attribute__((visibility("default")))
#endif

/* Runs the mbmesh program with argc/argv (argv[0] the program name) and returns
 * its exit status. Where the program would end the process on a bad option, it
 * returns that status instead. When captured_output is not NULL, what the program
 * writes to standard output (the --info listing, the usage) is returned there,
 * allocated with malloc() instead of being written; NULL if there is no memory. */
int mbmesh_run_program(int argc, char **argv, char **captured_output);

/* Reads the triangle mesh of a GLB that mbmesh or mbgrd2gltf wrote, Draco-compressed or plain (the
 * reader of mbglb2tiles), and returns it in WGS 84 geodetic coordinates: *vertices holds longitude,
 * latitude (degrees) and height above the ellipsoid (meters) for each of *nvertices vertices, and
 * *triangles three 0-based vertex indices for each of *ntriangles triangles. Both arrays are
 * allocated with malloc() and released with mbmesh_free(). The Draco primitives are decoded on up to
 * threads threads. Returns 0 on success; on failure 1, with the reason in error. */
MBMESH_CAPI_EXPORT int mbmesh_read_glb_geodetic(const char *path, int threads, double **vertices, size_t *nvertices,
                                                int **triangles, size_t *ntriangles, char *error, size_t error_length);
MBMESH_CAPI_EXPORT void mbmesh_free(void *pointer);

#ifdef __cplusplus
}
#endif

#endif /* MBMESH_CAPI_H */
