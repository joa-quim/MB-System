/*--------------------------------------------------------------------
 *    The MB-system:  mbgrd2gltf_capi.cpp
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Implementation of the plain C interface declared in mbgrd2gltf_capi.h.
 *
 * Deliberately thin: it owns no pipeline logic. It translates between the POD
 * structure the GMT module uses and the C++ types of the mbgrd2gltf library,
 * hands back opaque handles, and converts the exceptions the original main()
 * caught into error strings. Sequencing, reporting and error handling stay in
 * the driver (src/gmt/mbgrd2gltf.c).
 */

#include <ctime>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>
#include <cstring>
#include <system_error>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "options.h"
#include "logger.h"
#include "bathymetry.h"
#include "geometry.h"
#include "model.h"
#include "provenance.h"
#include "viewpoint.h"
#include "draco_mesh.h"

#include "mb_define.h"

#include "mbgrd2gltf_capi.h"

using namespace mbgrd2gltf;

struct mbgrd2gltf_bathymetry_s {
	Bathymetry value;
	explicit mbgrd2gltf_bathymetry_s(const Options &options) : value(options) {}
};

struct mbgrd2gltf_geometry_s {
	Geometry value;
	/* filled in by mbgrd2gltf_write_gltf(), for the pages written after it */
	GridRunInfo run;
	mbgrd2gltf_geometry_s(const Bathymetry &bathymetry, const Options &options) : value(bathymetry, options) {}
};

namespace {

void copy_error(const std::string &message, char *error, size_t error_length) {
	if (error == nullptr || error_length == 0) {
		return;
	}
	std::strncpy(error, message.c_str(), error_length - 1);
	error[error_length - 1] = '\0';
}

/* The same set of handlers the original main() wrapped the whole program in,
 * applied per call so the driver can report and unwind through Return(). */
template <typename Fn>
bool guarded(Fn fn, char *error, size_t error_length) {
	try {
		fn();
		return true;
	}
	catch (const std::invalid_argument &e) {
		copy_error(std::string("Invalid argument error: ") + e.what(), error, error_length);
	}
	catch (const std::out_of_range &e) {
		copy_error(std::string("Out of range error: ") + e.what(), error, error_length);
	}
	catch (const std::bad_alloc &e) {
		copy_error(std::string("Memory allocation error: ") + e.what(), error, error_length);
	}
	catch (const std::runtime_error &e) {
		copy_error(std::string("Runtime error: ") + e.what(), error, error_length);
	}
	catch (const std::exception &e) {
		copy_error(std::string("General error: ") + e.what(), error, error_length);
	}
	catch (...) {
		copy_error("Unknown error occurred.", error, error_length);
	}
	return false;
}

/* Where the model is and how it was made, as the program's main() gathers it. */
GridRunInfo make_run_info(const Bathymetry &bathymetry, const Geometry &geometry, const Options &options,
                          const std::string &command_line) {
	GridRunInfo run;
	run.program = "mbgrd2gltf";
	run.version = MB_VERSION;
	run.command_line = command_line;
	run.started_at = iso_utc_time(static_cast<double>(std::time(nullptr)));
	{
		std::error_code ec;
		const std::filesystem::path cwd = std::filesystem::current_path(ec);
		if (!ec) run.working_directory = cwd.string();
	}
#ifdef _WIN32
	if (const char *user = std::getenv("USERNAME")) run.user = user;
	if (const char *host = std::getenv("COMPUTERNAME")) run.host = host;
#else
	if (const char *user = std::getenv("USER"))
		run.user = user;
	else if (const char *logname = std::getenv("LOGNAME"))
		run.user = logname;
	{
		char host[256] = "";
		if (gethostname(host, sizeof(host) - 1) == 0) run.host = host;
	}
#endif
	{
		std::string grid_file = options.input_filepath();
		std::error_code ec;
		const std::filesystem::path absolute = std::filesystem::absolute(grid_file, ec);
		if (!ec) grid_file = absolute.lexically_normal().string();
		run.grid_file = grid_file;
		const size_t slash = grid_file.find_last_of("/\\");
		run.title = slash == std::string::npos ? grid_file : grid_file.substr(slash + 1);
	}
	run.grid_columns = bathymetry.size_x();
	run.grid_rows = bathymetry.size_y();
	run.longitude_spacing = bathymetry.longitude_spacing();
	run.latitude_spacing = bathymetry.latitude_spacing();
	run.pixel_registered = bathymetry.pixel_registered();
	run.extent_west = bathymetry.longitude_min();
	run.extent_east = bathymetry.longitude_max();
	run.extent_south = bathymetry.latitude_min();
	run.extent_north = bathymetry.latitude_max();
	run.mean_altitude = geometry.reference_altitude();
	if (options.is_geoorigin_auto() || options.is_geoorigin_set()) {
		run.geoorigin_mode = options.is_geoorigin_set() ? "specified" : "automatic";
		run.has_center = true;
		run.center_x = geometry.geoorigin_x();
		run.center_y = geometry.geoorigin_y();
		run.center_z = geometry.geoorigin_z();
		if (options.is_geoorigin_set()) {
			run.center_longitude = options.geoorigin_lon();
			run.center_latitude = options.geoorigin_lat();
			run.center_height = options.geoorigin_elev();
		}
		else {
			run.center_longitude = (bathymetry.longitude_min() + bathymetry.longitude_max()) / 2.0;
			run.center_latitude = (bathymetry.latitude_min() + bathymetry.latitude_max()) / 2.0;
			run.center_height = geometry.reference_altitude();
		}
	}
	run.exaggeration = options.exaggeration();
	run.binary = options.is_binary_output();
	return run;
}

} /* namespace */

/* Builds the C++ Options from the POD the GMT module fills in. Declared in
 * options.h so it sits with the rest of the class. */
namespace mbgrd2gltf {

Options::Options(const struct mbgrd2gltf_options &options) {
	_input_filepath = options.input_filepath;
	_output_filepath = options.output_filepath;
	_exaggeration = options.exaggeration;
	_geoorigin_lon = options.geoorigin_lon;
	_geoorigin_lat = options.geoorigin_lat;
	_geoorigin_elev = options.geoorigin_elev;
	_is_binary_output = options.is_binary_output;
	_is_help = options.is_help;
	_is_verbose = options.is_verbose;
	_is_exaggeration_set = options.is_exaggeration_set;
	_is_output_folder_set = options.is_output_folder_set;
	_is_geoorigin_set = options.is_geoorigin_set;
	_is_geoorigin_auto = options.is_geoorigin_auto;
	_is_html_output = options.is_html_output;
	_is_cesium_html_output = options.is_cesium_html_output;
	_colors = options.colors;
	_cesium_url = options.cesium_url;
	_draco.enabled = options.is_draco_compressed;
	_draco.precision = options.draco_precision;
	_draco.normal_bits = options.draco_normal_bits;
	for (int i = 0; i < options.n_geoviewpoints && i < MBGRD2GLTF_GEOVIEWPOINT_MAX; i++) {
		_geoviewpoints.push_back(options.geoviewpoints[i]);
	}
}

} /* namespace mbgrd2gltf */

extern "C" {

void mbgrd2gltf_options_defaults(struct mbgrd2gltf_options *options) {
	const mbdraco::Settings draco;
	std::memset(options, 0, sizeof(*options));
	options->exaggeration = 1.0;
	/* as in the C++ class: GLB, Draco and colors by height unless switched off */
	options->is_binary_output = true;
	options->colors = true;
	options->is_draco_compressed = draco.enabled;
	options->draco_precision = draco.precision;
	options->draco_normal_bits = draco.normal_bits;
}

void mbgrd2gltf_default_output_root(const char *input_filepath, char *output_root, size_t length) {
	const PathInfo info = get_path_info(input_filepath);
	const std::string root = info.folder + info.file_basename;

	std::strncpy(output_root, root.c_str(), length - 1);
	output_root[length - 1] = '\0';
}

bool mbgrd2gltf_parse_draco_precision(const char *text, double *precision, char *reason, size_t reason_length) {
	std::string message;
	if (!mbdraco::parse_precision(text, precision, &message)) {
		copy_error(message, reason, reason_length);
		return false;
	}
	return true;
}

bool mbgrd2gltf_parse_draco_normal_bits(const char *text, int *bits, char *reason, size_t reason_length) {
	std::string message;
	if (!mbdraco::parse_normal_bits(text, bits, &message)) {
		copy_error(message, reason, reason_length);
		return false;
	}
	return true;
}

bool mbgrd2gltf_geoviewpoint_valid(const char *text, char *reason, size_t reason_length) {
	UserViewpoint viewpoint;
	std::string message;
	if (!parse_user_viewpoint(text, &viewpoint, &message)) {
		copy_error(message, reason, reason_length);
		return false;
	}
	return true;
}

void mbgrd2gltf_logger_set_verbose(bool verbose) {
	Logger::set_level(verbose ? LogLevel::DEBUG : LogLevel::INFO);
}

void mbgrd2gltf_logger_start_capture(void) { Logger::start_capture(); }

void mbgrd2gltf_logger_info(const char *message) { LOG_INFO(message); }

bool mbgrd2gltf_bathymetry_create(const struct mbgrd2gltf_options *options,
                                  mbgrd2gltf_bathymetry_t **bathymetry, char *error, size_t error_length) {
	mbgrd2gltf_bathymetry_t *handle = nullptr;
	const bool ok = guarded([&]() {
		const Options cxx_options(*options);
		handle = new mbgrd2gltf_bathymetry_s(cxx_options);
	}, error, error_length);

	*bathymetry = ok ? handle : nullptr;
	if (!ok) delete handle;
	return ok;
}

void mbgrd2gltf_bathymetry_destroy(mbgrd2gltf_bathymetry_t *bathymetry) { delete bathymetry; }

bool mbgrd2gltf_geometry_create(const mbgrd2gltf_bathymetry_t *bathymetry,
                                const struct mbgrd2gltf_options *options,
                                mbgrd2gltf_geometry_t **geometry, char *error, size_t error_length) {
	mbgrd2gltf_geometry_t *handle = nullptr;
	const bool ok = guarded([&]() {
		const Options cxx_options(*options);
		handle = new mbgrd2gltf_geometry_s(bathymetry->value, cxx_options);
	}, error, error_length);

	*geometry = ok ? handle : nullptr;
	if (!ok) delete handle;
	return ok;
}

void mbgrd2gltf_geometry_destroy(mbgrd2gltf_geometry_t *geometry) { delete geometry; }

bool mbgrd2gltf_write_gltf(const mbgrd2gltf_bathymetry_t *bathymetry, mbgrd2gltf_geometry_t *geometry,
                           const struct mbgrd2gltf_options *options, const char *command_line,
                           char *error, size_t error_length) {
	return guarded([&]() {
		const Options cxx_options(*options);
		geometry->run = make_run_info(bathymetry->value, geometry->value, cxx_options, command_line);
		model::write_gltf(geometry->value, cxx_options, geometry->run);
	}, error, error_length);
}

bool mbgrd2gltf_write_cesium_html(const mbgrd2gltf_geometry_t *geometry, const struct mbgrd2gltf_options *options,
                                  char *error, size_t error_length) {
	return guarded([&]() {
		const Options cxx_options(*options);
		model::write_cesium_html(geometry->value, cxx_options, Logger::get_captured_logs(), geometry->run);
	}, error, error_length);
}

bool mbgrd2gltf_write_html(const mbgrd2gltf_bathymetry_t *bathymetry, const mbgrd2gltf_geometry_t *geometry,
                           const struct mbgrd2gltf_options *options, const char *command_line,
                           const char *timestamp, char *error, size_t error_length) {
	return guarded([&]() {
		const Options cxx_options(*options);
		/* The viewer embeds the captured log for provenance; the driver has been
		 * feeding it through mbgrd2gltf_logger_info() as it went. */
		model::write_html(bathymetry->value, geometry->value, cxx_options, command_line, timestamp,
		                  Logger::get_captured_logs(), geometry->run);
	}, error, error_length);
}

} /* extern "C" */
