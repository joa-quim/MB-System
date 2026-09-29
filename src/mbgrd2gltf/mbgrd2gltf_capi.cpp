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

#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <cstring>

#include "options.h"
#include "logger.h"
#include "bathymetry.h"
#include "geometry.h"
#include "model.h"

#include "mbgrd2gltf_capi.h"

using namespace mbgrd2gltf;

struct mbgrd2gltf_bathymetry_s {
	Bathymetry value;
	explicit mbgrd2gltf_bathymetry_s(const Options &options) : value(options) {}
};

struct mbgrd2gltf_geometry_s {
	Geometry value;
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
	_is_draco_compressed = options.is_draco_compressed;
	_is_geoorigin_set = options.is_geoorigin_set;
	_is_geoorigin_auto = options.is_geoorigin_auto;
	_is_html_output = options.is_html_output;
	for (int i = 0; i < 4; i++) {
		_draco_quantization[i] = options.draco_quantization[i];
	}
}

} /* namespace mbgrd2gltf */

extern "C" {

void mbgrd2gltf_options_defaults(struct mbgrd2gltf_options *options) {
	std::memset(options, 0, sizeof(*options));
	options->exaggeration = 1.0;
	/* [POSITION, NORMAL, TEXCOORD, COLOR], as in the C++ class */
	options->draco_quantization[0] = 16;
	options->draco_quantization[1] = 7;
	options->draco_quantization[2] = 10;
	options->draco_quantization[3] = 8;
}

void mbgrd2gltf_default_output_root(const char *input_filepath, char *output_root, size_t length) {
	const PathInfo info = get_path_info(input_filepath);
	const std::string root = info.folder + info.file_basename;

	std::strncpy(output_root, root.c_str(), length - 1);
	output_root[length - 1] = '\0';
}

bool mbgrd2gltf_draco_quantization_valid(const struct mbgrd2gltf_options *options) {
	for (int i = 0; i < 4; i++) {
		if (options->draco_quantization[i] < 2 || options->draco_quantization[i] > 30) return false;
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

bool mbgrd2gltf_write_gltf(const mbgrd2gltf_geometry_t *geometry, const struct mbgrd2gltf_options *options,
                           char *error, size_t error_length) {
	return guarded([&]() {
		const Options cxx_options(*options);
		model::write_gltf(geometry->value, cxx_options);
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
		                  Logger::get_captured_logs());
	}, error, error_length);
}

} /* extern "C" */
