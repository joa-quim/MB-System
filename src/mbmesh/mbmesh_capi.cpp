/*--------------------------------------------------------------------
 *    The MB-system:  mbmesh_capi.cpp
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Implementation of the plain C interface declared in mbmesh_capi.h.
 *
 * This file is deliberately thin: it owns no pipeline logic of its own. It
 * translates between the POD structures the GMT module uses and the C++ types
 * of the mbmesh library, and it hands back opaque handles. All sequencing,
 * reporting and error handling stays in the driver (src/gmt/mbmesh.c).
 */

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "settings.h"

#include "io/xyz_writer.h"
#include "io/glb_writer.h"
#include "io/x3dom_writer.h"

#include "algorithms/point_decimation.h"
#include "algorithms/normal_estimation.h"
#include "algorithms/screened_poisson.h"
#include "algorithms/marching_cubes.h"
#include "algorithms/support_trimming.h"

#include "mbmesh_capi.h"

/* The opaque handles are plain wrappers around the C++ values. */
struct mbmesh_preprocessed_s {
	PreprocessedDatalist value;
};

struct mbmesh_collected_s {
	CollectedPointCloud value;
};

struct mbmesh_oriented_s {
	OrientedPointCloud value;
};

struct mbmesh_grid_s {
	ScalarGrid3D value;
};

struct mbmesh_mesh_s {
	Mesh value;
};

namespace {

void copy_error(const std::string &message, char *error, size_t error_length) {
	if (error == nullptr || error_length == 0) {
		return;
	}
	std::strncpy(error, message.c_str(), error_length - 1);
	error[error_length - 1] = '\0';
}

/* Builds the C++ Options from the POD structure. Only the fields the pipeline
 * reads are transferred; the rest (help_requested and the write_* flags) are
 * acted on by the driver itself. */
Options to_cxx_options(const struct mbmesh_options &in) {
	Options options;

	options.help_requested = in.help_requested;
	options.metadata_requested = in.metadata_requested;
	options.verbose = in.verbose;

	options.write_html = in.write_html;
	options.write_local_xyz = in.write_local_xyz;
	options.write_ecef_xyz = in.write_ecef_xyz;
	options.write_oriented_ply = in.write_oriented_ply;
	options.write_pointcloud_glb = in.write_pointcloud_glb;
	options.write_normal_glb = in.write_normal_glb;
	options.write_origin_glb = in.write_origin_glb;
	options.write_raw_mesh_glb = in.write_raw_mesh_glb;

	options.input_datalist = std::filesystem::path(in.input_datalist);
	options.output_directory = std::filesystem::path(in.output_directory);

	options.use_bounds = in.use_bounds;
	options.bounds.degrees_W = in.degrees_W;
	options.bounds.degrees_E = in.degrees_E;
	options.bounds.degrees_S = in.degrees_S;
	options.bounds.degrees_N = in.degrees_N;

	options.level_of_detail = in.level_of_detail;
	options.level_of_detail_requested = in.level_of_detail_requested;
	options.decimation_requested = in.decimation_requested;

	options.decimation.decimate = in.decimation_decimate;
	options.decimation.cell_size = in.decimation_cell_size;

	options.normals.k = in.normals_k;
	options.normals.search_radius = in.normals_search_radius;
	options.normals.minimum_neighbors = in.normals_minimum_neighbors;

	options.poisson.cell_size = in.poisson_cell_size;
	options.poisson.padding = in.poisson_padding;
	options.poisson.normal_splat_radius = in.poisson_normal_splat_radius;
	options.poisson.screening_weight = in.poisson_screening_weight;
	options.poisson.solver_iterations = in.poisson_solver_iterations;
	options.poisson.use_screening = in.poisson_use_screening;
	options.poisson.iso_value = in.poisson_iso_value;

	options.marching_cubes.iso_value = in.marching_cubes_iso_value;

	options.trimming.enabled = in.trimming_enabled;
	options.trimming.support_radius = in.trimming_support_radius;
	options.trimming.max_normal_offset = in.trimming_max_normal_offset;
	options.trimming.minimum_neighbors = in.trimming_minimum_neighbors;
	options.trimming.minimum_normal_alignment = in.trimming_minimum_normal_alignment;

	return options;
}

/* Writes the values that preprocess_datalist() may have resolved back into the
 * POD structure, so that the driver reports what the pipeline will actually
 * use rather than what was asked for. */
void from_cxx_options(const Options &options, struct mbmesh_options *out) {
	out->level_of_detail = options.level_of_detail;
	out->level_of_detail_requested = options.level_of_detail_requested;
	out->decimation_requested = options.decimation_requested;

	out->decimation_decimate = options.decimation.decimate;
	out->decimation_cell_size = options.decimation.cell_size;

	out->normals_k = options.normals.k;
	out->normals_search_radius = options.normals.search_radius;
	out->normals_minimum_neighbors = options.normals.minimum_neighbors;

	out->poisson_cell_size = options.poisson.cell_size;
	out->poisson_padding = options.poisson.padding;
	out->poisson_normal_splat_radius = options.poisson.normal_splat_radius;
	out->poisson_screening_weight = options.poisson.screening_weight;
	out->poisson_solver_iterations = options.poisson.solver_iterations;
	out->poisson_use_screening = options.poisson.use_screening;
	out->poisson_iso_value = options.poisson.iso_value;

	out->marching_cubes_iso_value = options.marching_cubes.iso_value;

	out->trimming_enabled = options.trimming.enabled;
	out->trimming_support_radius = options.trimming.support_radius;
	out->trimming_max_normal_offset = options.trimming.max_normal_offset;
	out->trimming_minimum_neighbors = options.trimming.minimum_neighbors;
	out->trimming_minimum_normal_alignment = options.trimming.minimum_normal_alignment;
}

/* The driver's PointCloud is built from the decimated samples: see the
 * output_points loop in the original mbmesh.cpp. */
PointCloud points_of(const CollectedPointCloud &collected_points) {
	PointCloud points;
	points.reserve(collected_points.size());
	for (const CollectedPoint &collected_point : collected_points) {
		points.push_back(collected_point.point);
	}
	return points;
}

} /* namespace */

extern "C" {

void mbmesh_options_defaults(struct mbmesh_options *options) {
	const Options defaults;
	std::memset(options, 0, sizeof(*options));

	options->help_requested = defaults.help_requested;
	options->metadata_requested = defaults.metadata_requested;
	options->verbose = defaults.verbose;

	options->write_html = defaults.write_html;
	options->write_local_xyz = defaults.write_local_xyz;
	options->write_ecef_xyz = defaults.write_ecef_xyz;
	options->write_oriented_ply = defaults.write_oriented_ply;
	options->write_pointcloud_glb = defaults.write_pointcloud_glb;
	options->write_normal_glb = defaults.write_normal_glb;
	options->write_origin_glb = defaults.write_origin_glb;
	options->write_raw_mesh_glb = defaults.write_raw_mesh_glb;

	std::strncpy(options->input_datalist, defaults.input_datalist.string().c_str(), MBMESH_PATH_MAXLINE - 1);
	std::strncpy(options->output_directory, defaults.output_directory.string().c_str(), MBMESH_PATH_MAXLINE - 1);

	options->use_bounds = defaults.use_bounds;
	options->degrees_W = defaults.bounds.degrees_W;
	options->degrees_E = defaults.bounds.degrees_E;
	options->degrees_S = defaults.bounds.degrees_S;
	options->degrees_N = defaults.bounds.degrees_N;

	from_cxx_options(defaults, options);
}

bool mbmesh_preprocess_datalist(struct mbmesh_options *options, mbmesh_preprocessed_t **preprocessed, char *error,
                                size_t error_length) {
	Options cxx_options = to_cxx_options(*options);
	mbmesh_preprocessed_t *handle = new mbmesh_preprocessed_s();
	std::string message;

	if (!preprocess_datalist(cxx_options, &handle->value, &message)) {
		copy_error(message, error, error_length);
		delete handle;
		*preprocessed = nullptr;
		return false;
	}

	from_cxx_options(cxx_options, options);
	*preprocessed = handle;
	return true;
}

void mbmesh_preprocessed_destroy(mbmesh_preprocessed_t *preprocessed) { delete preprocessed; }

size_t mbmesh_preprocessed_accepted_points(const mbmesh_preprocessed_t *preprocessed) {
	return preprocessed->value.read_result.points.size();
}

size_t mbmesh_preprocessed_soundings_read(const mbmesh_preprocessed_t *preprocessed) {
	return preprocessed->value.read_result.stats.soundings_read;
}

size_t mbmesh_preprocessed_files_read(const mbmesh_preprocessed_t *preprocessed) {
	return preprocessed->value.read_result.stats.files_read;
}

char *mbmesh_datalist_metadata_text(const mbmesh_preprocessed_t *preprocessed, const struct mbmesh_options *options) {
	/* the program's own printer, unchanged, with std::cout pointed at a string */
	std::ostringstream text;
	std::streambuf *saved = std::cout.rdbuf(text.rdbuf());
	const std::streamsize precision = std::cout.precision();
	print_datalist_metadata(preprocessed->value, to_cxx_options(*options));
	std::cout.precision(precision);
	std::cout.rdbuf(saved);
	const std::string s = text.str();
	char *result = static_cast<char *>(std::malloc(s.size() + 1));
	if (result != nullptr) std::memcpy(result, s.c_str(), s.size() + 1);
	return result;
}

mbmesh_collected_t *mbmesh_preprocessed_take_points(mbmesh_preprocessed_t *preprocessed) {
	mbmesh_collected_t *handle = new mbmesh_collected_s();
	handle->value = std::move(preprocessed->value.read_result.points);
	return handle;
}

size_t mbmesh_collected_size(const mbmesh_collected_t *collected) { return collected->value.size(); }

void mbmesh_collected_destroy(mbmesh_collected_t *collected) { delete collected; }

mbmesh_collected_t *mbmesh_point_decimation(mbmesh_collected_t *collected, const struct mbmesh_options *options) {
	const Options cxx_options = to_cxx_options(*options);
	mbmesh_collected_t *handle = new mbmesh_collected_s();
	handle->value = point_decimation(std::move(collected->value), cxx_options.decimation);
	delete collected;
	return handle;
}

mbmesh_oriented_t *mbmesh_normal_estimation(const mbmesh_collected_t *collected, const struct mbmesh_options *options) {
	const Options cxx_options = to_cxx_options(*options);
	mbmesh_oriented_t *handle = new mbmesh_oriented_s();
	handle->value = normal_estimation(collected->value, cxx_options.normals);
	return handle;
}

size_t mbmesh_oriented_size(const mbmesh_oriented_t *oriented) { return oriented->value.size(); }

void mbmesh_oriented_destroy(mbmesh_oriented_t *oriented) { delete oriented; }

mbmesh_grid_t *mbmesh_screened_poisson(const mbmesh_oriented_t *oriented, const struct mbmesh_options *options) {
	const Options cxx_options = to_cxx_options(*options);
	mbmesh_grid_t *handle = new mbmesh_grid_s();
	handle->value = screened_poisson(oriented->value, cxx_options.poisson);
	return handle;
}

size_t mbmesh_grid_value_count(const mbmesh_grid_t *grid) { return grid->value.values.size(); }
size_t mbmesh_grid_nx(const mbmesh_grid_t *grid) { return grid->value.nx; }
size_t mbmesh_grid_ny(const mbmesh_grid_t *grid) { return grid->value.ny; }
size_t mbmesh_grid_nz(const mbmesh_grid_t *grid) { return grid->value.nz; }
void mbmesh_grid_destroy(mbmesh_grid_t *grid) { delete grid; }

mbmesh_mesh_t *mbmesh_marching_cubes(const mbmesh_grid_t *grid, const struct mbmesh_options *options) {
	const Options cxx_options = to_cxx_options(*options);
	mbmesh_mesh_t *handle = new mbmesh_mesh_s();
	handle->value = marching_cubes(grid->value, cxx_options.marching_cubes);
	return handle;
}

size_t mbmesh_mesh_vertex_count(const mbmesh_mesh_t *mesh) { return mesh->value.vertices.size(); }
size_t mbmesh_mesh_index_count(const mbmesh_mesh_t *mesh) { return mesh->value.indices.size(); }
void mbmesh_mesh_destroy(mbmesh_mesh_t *mesh) { delete mesh; }

mbmesh_mesh_t *mbmesh_support_trimming(const mbmesh_mesh_t *raw_mesh, const mbmesh_oriented_t *oriented,
                                       const struct mbmesh_options *options,
                                       struct mbmesh_trimming_diagnostics *diagnostics) {
	const Options cxx_options = to_cxx_options(*options);
	SupportTrimmingDiagnostics trimming_diagnostics;
	mbmesh_mesh_t *handle = new mbmesh_mesh_s();

	handle->value = support_trimming(raw_mesh->value, oriented->value, cxx_options.trimming, &trimming_diagnostics);

	if (diagnostics != nullptr) {
		diagnostics->estimated_point_spacing = trimming_diagnostics.estimated_point_spacing;
		diagnostics->resolved_support_radius = trimming_diagnostics.resolved_support_radius;
		diagnostics->resolved_max_normal_offset = trimming_diagnostics.resolved_max_normal_offset;
		diagnostics->input_vertices = trimming_diagnostics.input_vertices;
		diagnostics->supported_vertices = trimming_diagnostics.supported_vertices;
		diagnostics->rejected_for_neighbors = trimming_diagnostics.rejected_for_neighbors;
		diagnostics->rejected_for_normal_offset = trimming_diagnostics.rejected_for_normal_offset;
		diagnostics->rejected_for_normal_alignment = trimming_diagnostics.rejected_for_normal_alignment;
		diagnostics->input_triangles = trimming_diagnostics.input_triangles;
		diagnostics->output_vertices = trimming_diagnostics.output_vertices;
		diagnostics->output_triangles = trimming_diagnostics.output_triangles;
	}

	return handle;
}

mbmesh_mesh_t *mbmesh_mesh_copy(const mbmesh_mesh_t *mesh) {
	mbmesh_mesh_t *handle = new mbmesh_mesh_s();
	handle->value = mesh->value;
	return handle;
}

bool mbmesh_create_output_directory(const char *directory, char *error, size_t error_length) {
	std::error_code filesystem_error;
	const std::filesystem::path path(directory);

	std::filesystem::create_directories(path, filesystem_error);

	if (filesystem_error) {
		copy_error("Error creating output directory '" + path.string() + "': " + filesystem_error.message(), error,
		           error_length);
		return false;
	}
	return true;
}

void mbmesh_path_join(const char *directory, const char *filename, char *path, size_t path_length) {
	const std::string joined = (std::filesystem::path(directory) / filename).string();
	std::strncpy(path, joined.c_str(), path_length - 1);
	path[path_length - 1] = '\0';
}

bool mbmesh_write_ecef_xyz_pointcloud(const mbmesh_collected_t *decimated, const mbmesh_preprocessed_t *preprocessed,
                                      const char *path, char *error, size_t error_length) {
	std::string message;
	if (!write_ecef_xyz_pointcloud(points_of(decimated->value), preprocessed->value.read_result.frame, path, &message)) {
		copy_error(message, error, error_length);
		return false;
	}
	return true;
}

bool mbmesh_write_local_xyz_pointcloud(const mbmesh_collected_t *decimated, const char *path, char *error,
                                       size_t error_length) {
	std::string message;
	if (!write_local_xyz_pointcloud(points_of(decimated->value), path, &message)) {
		copy_error(message, error, error_length);
		return false;
	}
	return true;
}

bool mbmesh_write_ply_oriented_pointcloud(const mbmesh_oriented_t *oriented, const mbmesh_preprocessed_t *preprocessed,
                                          const char *path, char *error, size_t error_length) {
	std::string message;
	if (!write_ply_oriented_pointcloud(oriented->value, preprocessed->value.read_result.frame, path, &message)) {
		copy_error(message, error, error_length);
		return false;
	}
	return true;
}

bool mbmesh_write_pointcloud_glb_file(const char *path, const mbmesh_collected_t *decimated, char *error,
                                      size_t error_length) {
	std::string message;
	if (!write_pointcloud_glb_file(std::filesystem::path(path), points_of(decimated->value), &message)) {
		copy_error(message, error, error_length);
		return false;
	}
	return true;
}

bool mbmesh_write_normal_lines_glb_file(const char *path, const mbmesh_oriented_t *oriented, double scale, char *error,
                                        size_t error_length) {
	std::string message;
	if (!write_normal_lines_glb_file(std::filesystem::path(path), oriented->value, scale, &message)) {
		copy_error(message, error, error_length);
		return false;
	}
	return true;
}

bool mbmesh_write_origin_ray_lines_glb_file(const char *path, const mbmesh_collected_t *collected, double scale,
                                            char *error, size_t error_length) {
	std::string message;
	if (!write_origin_ray_lines_glb_file(std::filesystem::path(path), collected->value, scale, &message)) {
		copy_error(message, error, error_length);
		return false;
	}
	return true;
}

bool mbmesh_write_mesh_glb_file(const char *path, const mbmesh_mesh_t *mesh, char *error, size_t error_length) {
	std::string message;
	if (!write_mesh_glb_file(std::filesystem::path(path), mesh->value, &message)) {
		copy_error(message, error, error_length);
		return false;
	}
	return true;
}

bool mbmesh_write_glb_x3dom_file(const char *path, const char *glb_filename, char *error, size_t error_length) {
	std::string message;
	if (!write_glb_x3dom_file(std::filesystem::path(path), glb_filename, {}, &message)) {
		copy_error(message, error, error_length);
		return false;
	}
	return true;
}

} /* extern "C" */
