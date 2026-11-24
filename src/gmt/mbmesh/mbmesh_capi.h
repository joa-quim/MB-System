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
/* Runs the mbmesh program as mbmesh_run_program() does and hands back the mesh it makes, in memory: in WGS 84
 * geodetic coordinates, as mbmesh_read_glb_geodetic() gives a GLB's. Nothing is written unless save_files is not 0:
 * then the program writes its files (mesh.glb, the chunks, what its options ask for) in its output directory as
 * usual; otherwise nothing is written, not even a directory. Both arrays are released with
 * mbmesh_free(). Returns the program's exit status, and 0 meshes when it made none. */
MBMESH_CAPI_EXPORT int mbmesh_run_mesh(int argc, char **argv, int save_files, double **vertices, size_t *nvertices,
                                       int **triangles, size_t *ntriangles);
/* As mbmesh_run_mesh(), with the soundings taken from memory instead of the file -I names (which then only names
 * the input in the program's messages): npoints of them, longitude and latitude (degrees) and height (meters,
 * positive up) each, read as MBIO's generic lon lat topography points are. */
MBMESH_CAPI_EXPORT int mbmesh_run_mesh_points(int argc, char **argv, const double *points, size_t npoints, int save_files,
                                              double **vertices, size_t *nvertices, int **triangles, size_t *ntriangles);
/* Writes a triangle mesh as a GLB the way mbmesh writes mesh.glb (MB-System's writer, Draco-compressed): vertices
 * holds nvertices x, y, z, triangles ntriangles triples of 0-based vertex indices. When geographic is not 0, x, y, z
 * are longitude, latitude (degrees) and height above the ellipsoid (meters): the file is georeferenced as mbmesh's,
 * the positions relative to the middle of the mesh, which its metadata names; otherwise they are written as they are.
 * Returns 0 on success; on failure 1, with the reason in error. */
MBMESH_CAPI_EXPORT int mbmesh_write_glb(const char *path, const double *vertices, size_t nvertices, const int *triangles,
                                        size_t ntriangles, int geographic, char *error, size_t error_length);
MBMESH_CAPI_EXPORT void mbmesh_free(void *pointer);

#ifdef __cplusplus
}
#endif

#endif /* MBMESH_CAPI_H */
