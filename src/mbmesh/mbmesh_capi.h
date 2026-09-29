/*--------------------------------------------------------------------
 *    The MB-system:  mbmesh_capi.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Plain C interface to the mbmesh reconstruction pipeline.
 *
 * The pipeline itself (datalist reading, decimation, normal estimation,
 * screened Poisson, marching cubes, support trimming, and the GLB/XYZ/PLY/HTML
 * writers) is C++ and uses std::vector, std::string and std::filesystem
 * throughout. The GMT module src/gmt/mbmesh.c is C, so it reaches the pipeline
 * through the opaque handles and POD structures declared here; the
 * implementation in mbmesh_capi.cpp is the only C++ involved.
 *
 * Every intermediate product of the pipeline is an opaque handle owned by the
 * caller and released with the matching *_destroy function. The counts that
 * the driver reports at each stage are exposed as accessors so that the whole
 * of the original mbmesh.cpp driver - its stage sequencing, its diagnostics,
 * and its failure handling - can live in C.
 */

#ifndef MBMESH_CAPI_H
#define MBMESH_CAPI_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MBMESH_PATH_MAXLINE 1024

/* Mirror of the scalar fields of the C++ Options structure. The nested
 * algorithm option structures are flattened in here because the driver reports
 * their resolved values, and mbmesh_preprocess_datalist() rewrites them:
 * preprocess_datalist() applies the spacing-driven defaults and the Poisson
 * grid budget to the options it is handed. */
struct mbmesh_options {
	bool help_requested;
	bool metadata_requested;
	bool verbose;

	bool write_html;
	bool write_local_xyz;
	bool write_ecef_xyz;
	bool write_oriented_ply;
	bool write_pointcloud_glb;
	bool write_normal_glb;
	bool write_origin_glb;
	bool write_raw_mesh_glb;

	char input_datalist[MBMESH_PATH_MAXLINE];
	char output_directory[MBMESH_PATH_MAXLINE];

	bool use_bounds;
	double degrees_W;
	double degrees_E;
	double degrees_S;
	double degrees_N;

	/* Smallest feature size, in local meters, that mbmesh should try to preserve. */
	double level_of_detail;
	bool level_of_detail_requested;
	bool decimation_requested;

	/* PointDecimationOptions */
	bool decimation_decimate;
	double decimation_cell_size;

	/* NormalEstimationOptions */
	size_t normals_k;
	double normals_search_radius;
	size_t normals_minimum_neighbors;

	/* ScreenedPoissonOptions */
	double poisson_cell_size;
	double poisson_padding;
	double poisson_normal_splat_radius;
	double poisson_screening_weight;
	int poisson_solver_iterations;
	bool poisson_use_screening;
	double poisson_iso_value;

	/* MarchingCubesOptions */
	double marching_cubes_iso_value;

	/* SupportTrimmingOptions */
	bool trimming_enabled;
	double trimming_support_radius;
	double trimming_max_normal_offset;
	int trimming_minimum_neighbors;
	double trimming_minimum_normal_alignment;
};

/* Mirror of SupportTrimmingDiagnostics. */
struct mbmesh_trimming_diagnostics {
	double estimated_point_spacing;
	double resolved_support_radius;
	double resolved_max_normal_offset;

	size_t input_vertices;
	size_t supported_vertices;
	size_t rejected_for_neighbors;
	size_t rejected_for_normal_offset;
	size_t rejected_for_normal_alignment;

	size_t input_triangles;
	size_t output_vertices;
	size_t output_triangles;
};

/* Opaque handles for the pipeline's intermediate products. */
typedef struct mbmesh_preprocessed_s mbmesh_preprocessed_t;
typedef struct mbmesh_collected_s mbmesh_collected_t;
typedef struct mbmesh_oriented_s mbmesh_oriented_t;
typedef struct mbmesh_grid_s mbmesh_grid_t;
typedef struct mbmesh_mesh_s mbmesh_mesh_t;

/* Fills options with the defaults of the C++ Options structure. */
void mbmesh_options_defaults(struct mbmesh_options *options);

/* Reads the datalist and resolves the spacing-driven defaults. On success the
 * resolved algorithm options are written back into *options, exactly as
 * preprocess_datalist() does for its C++ caller. Returns false and fills error
 * (a buffer of error_length bytes) on failure. */
bool mbmesh_preprocess_datalist(struct mbmesh_options *options, mbmesh_preprocessed_t **preprocessed, char *error,
                                size_t error_length);

void mbmesh_preprocessed_destroy(mbmesh_preprocessed_t *preprocessed);

/* Read-side accessors used by the driver's "input" stage report. */
size_t mbmesh_preprocessed_accepted_points(const mbmesh_preprocessed_t *preprocessed);
size_t mbmesh_preprocessed_soundings_read(const mbmesh_preprocessed_t *preprocessed);
size_t mbmesh_preprocessed_files_read(const mbmesh_preprocessed_t *preprocessed);

void mbmesh_print_datalist_metadata(const mbmesh_preprocessed_t *preprocessed, const struct mbmesh_options *options);

/* Takes the collected point cloud out of the preprocessed result, leaving the
 * result's own cloud empty (the C++ driver does this with std::move). */
mbmesh_collected_t *mbmesh_preprocessed_take_points(mbmesh_preprocessed_t *preprocessed);

size_t mbmesh_collected_size(const mbmesh_collected_t *collected);
void mbmesh_collected_destroy(mbmesh_collected_t *collected);

/* Consumes collected (which must not be used afterwards) and returns the
 * decimated cloud, mirroring point_decimation()'s by-value parameter. */
mbmesh_collected_t *mbmesh_point_decimation(mbmesh_collected_t *collected, const struct mbmesh_options *options);

mbmesh_oriented_t *mbmesh_normal_estimation(const mbmesh_collected_t *collected, const struct mbmesh_options *options);
size_t mbmesh_oriented_size(const mbmesh_oriented_t *oriented);
void mbmesh_oriented_destroy(mbmesh_oriented_t *oriented);

mbmesh_grid_t *mbmesh_screened_poisson(const mbmesh_oriented_t *oriented, const struct mbmesh_options *options);
size_t mbmesh_grid_value_count(const mbmesh_grid_t *grid);
size_t mbmesh_grid_nx(const mbmesh_grid_t *grid);
size_t mbmesh_grid_ny(const mbmesh_grid_t *grid);
size_t mbmesh_grid_nz(const mbmesh_grid_t *grid);
void mbmesh_grid_destroy(mbmesh_grid_t *grid);

mbmesh_mesh_t *mbmesh_marching_cubes(const mbmesh_grid_t *grid, const struct mbmesh_options *options);
size_t mbmesh_mesh_vertex_count(const mbmesh_mesh_t *mesh);
size_t mbmesh_mesh_index_count(const mbmesh_mesh_t *mesh);
void mbmesh_mesh_destroy(mbmesh_mesh_t *mesh);

/* Returns the trimmed mesh, filling diagnostics when it is not NULL. */
mbmesh_mesh_t *mbmesh_support_trimming(const mbmesh_mesh_t *raw_mesh, const mbmesh_oriented_t *oriented,
                                       const struct mbmesh_options *options,
                                       struct mbmesh_trimming_diagnostics *diagnostics);

/* Returns a copy of a mesh, for the branch where trimming is disabled and the
 * driver uses the raw mesh as the clean mesh. */
mbmesh_mesh_t *mbmesh_mesh_copy(const mbmesh_mesh_t *mesh);

/* Creates the output directory. Returns false and fills error on failure. */
bool mbmesh_create_output_directory(const char *directory, char *error, size_t error_length);

/* Joins a directory and a file name with the platform's separator. */
void mbmesh_path_join(const char *directory, const char *filename, char *path, size_t path_length);

/* The individual writers. Each returns false and fills error on failure.
 * mbmesh_write_*_pointcloud take the decimated cloud because the driver builds
 * its PointCloud from exactly those samples. */
bool mbmesh_write_ecef_xyz_pointcloud(const mbmesh_collected_t *decimated, const mbmesh_preprocessed_t *preprocessed,
                                      const char *path, char *error, size_t error_length);
bool mbmesh_write_local_xyz_pointcloud(const mbmesh_collected_t *decimated, const char *path, char *error,
                                       size_t error_length);
bool mbmesh_write_ply_oriented_pointcloud(const mbmesh_oriented_t *oriented, const mbmesh_preprocessed_t *preprocessed,
                                          const char *path, char *error, size_t error_length);
bool mbmesh_write_pointcloud_glb_file(const char *path, const mbmesh_collected_t *decimated, char *error,
                                      size_t error_length);
bool mbmesh_write_normal_lines_glb_file(const char *path, const mbmesh_oriented_t *oriented, double scale, char *error,
                                        size_t error_length);
bool mbmesh_write_origin_ray_lines_glb_file(const char *path, const mbmesh_collected_t *collected, double scale,
                                            char *error, size_t error_length);
bool mbmesh_write_mesh_glb_file(const char *path, const mbmesh_mesh_t *mesh, char *error, size_t error_length);
bool mbmesh_write_glb_x3dom_file(const char *path, const char *glb_filename, char *error, size_t error_length);

#ifdef __cplusplus
}
#endif

#endif /* MBMESH_CAPI_H */
