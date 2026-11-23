/*--------------------------------------------------------------------
 *    The MB-system:  mbgrd2gltf_capi.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Plain C interface to the mbgrd2gltf conversion pipeline.
 *
 * The pipeline (grid reading, geometry generation, glTF/GLB writing, optional
 * Draco compression and HTML viewer) is C++ and uses std::string, std::vector
 * and exceptions throughout. The GMT module src/gmt/mbgrd2gltf.c is C, so it
 * reaches the pipeline through the opaque handles and POD structure declared
 * here; the implementation in mbgrd2gltf_capi.cpp is the only C++ involved.
 *
 * This mirrors the arrangement already used for mbmesh
 * (src/mbmesh/mbmesh_capi.h): each stage is a separate call returning an
 * opaque handle, so the whole of the original main() - its sequencing, its
 * logging and its error handling - lives in the C driver, and the bridge
 * carries no logic of its own. Every entry point catches the C++ exceptions
 * main() used to catch and reports them through the error buffer.
 */

#ifndef MBGRD2GLTF_CAPI_H
#define MBGRD2GLTF_CAPI_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MBGRD2GLTF_PATH_MAXLINE 1024
#define MBGRD2GLTF_GEOVIEWPOINT_MAX 16
#define MBGRD2GLTF_GEOVIEWPOINT_MAXLINE 512

/* Mirror of the state of the C++ Options class. */
struct mbgrd2gltf_options {
	char input_filepath[MBGRD2GLTF_PATH_MAXLINE];
	char output_filepath[MBGRD2GLTF_PATH_MAXLINE];

	double exaggeration;
	double geoorigin_lon;
	double geoorigin_lat;
	double geoorigin_elev;

	bool is_binary_output;   /* GLB unless --no-binary */
	bool is_help;
	bool is_verbose;
	bool is_exaggeration_set;
	bool is_output_folder_set;
	bool is_draco_compressed;   /* on unless --no-draco */
	bool is_geoorigin_set;
	bool is_geoorigin_auto;
	bool is_html_output;
	bool is_cesium_html_output;   /* --cesium-html */
	bool colors;                  /* COLOR_0 by height, unless --no-colors */

	/* mbdraco::Settings: the step of the positions in meters, and the bits of a normal */
	double draco_precision;
	int draco_normal_bits;

	char cesium_url[MBGRD2GLTF_PATH_MAXLINE];   /* empty for the default build */

	/* --geoviewpoint values, as given, each checked by mbgrd2gltf_geoviewpoint_valid() */
	int n_geoviewpoints;
	char geoviewpoints[MBGRD2GLTF_GEOVIEWPOINT_MAX][MBGRD2GLTF_GEOVIEWPOINT_MAXLINE];
};

typedef struct mbgrd2gltf_bathymetry_s mbgrd2gltf_bathymetry_t;
typedef struct mbgrd2gltf_geometry_s mbgrd2gltf_geometry_t;

/* Fills options with the defaults of the C++ Options class. */
void mbgrd2gltf_options_defaults(struct mbgrd2gltf_options *options);

/* Derives the output file root from the input path, the way the original
 * option parser does when -O is not given. */
void mbgrd2gltf_default_output_root(const char *input_filepath, char *output_root, size_t length);

/* The checks of the program's option parser, with its messages: each returns
 * false and fills reason when text is not a valid value. */
bool mbgrd2gltf_parse_draco_precision(const char *text, double *precision, char *reason, size_t reason_length);
bool mbgrd2gltf_parse_draco_normal_bits(const char *text, int *bits, char *reason, size_t reason_length);
bool mbgrd2gltf_geoviewpoint_valid(const char *text, char *reason, size_t reason_length);

/* The logger is a process-wide singleton in the C++ code. The driver drives it
 * so that its own progress lines end up in the same capture buffer that the
 * HTML viewer embeds for provenance. */
void mbgrd2gltf_logger_set_verbose(bool verbose);
void mbgrd2gltf_logger_start_capture(void);
void mbgrd2gltf_logger_info(const char *message);

/* Pipeline stages. Each returns false and fills error (a buffer of
 * error_length bytes) when the C++ side throws. */
bool mbgrd2gltf_bathymetry_create(const struct mbgrd2gltf_options *options,
                                  mbgrd2gltf_bathymetry_t **bathymetry, char *error, size_t error_length);
void mbgrd2gltf_bathymetry_destroy(mbgrd2gltf_bathymetry_t *bathymetry);

bool mbgrd2gltf_geometry_create(const mbgrd2gltf_bathymetry_t *bathymetry,
                                const struct mbgrd2gltf_options *options,
                                mbgrd2gltf_geometry_t **geometry, char *error, size_t error_length);
void mbgrd2gltf_geometry_destroy(mbgrd2gltf_geometry_t *geometry);

/* Writes the model, and records in the geometry handle where the model is and
 * how it was made (the georeference and provenance that the program's main()
 * gathers), which the pages written after it repeat. */
bool mbgrd2gltf_write_gltf(const mbgrd2gltf_bathymetry_t *bathymetry, mbgrd2gltf_geometry_t *geometry,
                           const struct mbgrd2gltf_options *options, const char *command_line,
                           char *error, size_t error_length);

/* The pages; call them after mbgrd2gltf_write_gltf(). */
bool mbgrd2gltf_write_html(const mbgrd2gltf_bathymetry_t *bathymetry, const mbgrd2gltf_geometry_t *geometry,
                           const struct mbgrd2gltf_options *options, const char *command_line,
                           const char *timestamp, char *error, size_t error_length);
bool mbgrd2gltf_write_cesium_html(const mbgrd2gltf_geometry_t *geometry, const struct mbgrd2gltf_options *options,
                                  char *error, size_t error_length);

#ifdef __cplusplus
}
#endif

#endif /* MBGRD2GLTF_CAPI_H */
