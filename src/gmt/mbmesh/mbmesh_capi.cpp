/*--------------------------------------------------------------------
 *    The MB-system:  mbmesh_capi.cpp
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Implementation of the plain C interface declared in mbmesh_capi.h.
 */

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <cstdio>
#include <algorithm>

#include <getopt.h>

#include "mbmesh_program.h"
#include "mbmesh_capi.h"
#include "glb_reader.h"
#include "io/coordinate_frame.h"
#include "io/glb_writer.h"
#include "io/provenance.h"
#include "mb_define.h"
#include "mbmesh_fast_poisson.h"
#include "mesh_capture.h"
#include "util/parallel.h"

namespace {

/* getopt keeps its position between calls, and a GMT module runs in a session
 * that may call the program many times: start it over. */
void reset_getopt() {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
	optreset = 1;
	optind = 1;
#else
	optind = 0; /* glibc, getopt-win32 and MinGW-w64: 0 starts over and re-initialises */
#endif
}

/* The value of a long option given as "--name=value" or "--name value", or nullptr. */
const char *long_option_value(int argc, char **argv, const char *name) {
	const std::size_t length = std::strlen(name);
	const char *value = nullptr;
	for (int i = 1; i < argc; i++) {
		if (argv[i] == nullptr || std::strncmp(argv[i], name, length) != 0) {
			continue;
		}
		if (argv[i][length] == '=') {
			value = argv[i] + length + 1;
		}
		else if (argv[i][length] == '\0' && i + 1 < argc) {
			value = argv[i + 1];
		}
	}
	return value;
}

/* The output directory of a command line, -O<dir>, -O <dir>, --output=<dir> or --output <dir>; "" when none. */
std::string output_directory(int argc, char **argv) {
	std::string dir;
	for (int i = 1; i < argc; i++) {
		if (argv[i] == nullptr) {
			continue;
		}
		if (std::strncmp(argv[i], "-O", 2) == 0) {
			dir = argv[i][2] != '\0' ? argv[i] + 2 : (i + 1 < argc && argv[i + 1] != nullptr ? argv[i + 1] : "");
		}
	}
	if (const char *value = long_option_value(argc, argv, "--output")) {
		dir = value;
	}
	return dir;
}

} /* namespace */

extern "C" int mbmesh_run_program(int argc, char **argv, char **captured_output) {
	/* The fork's octree multigrid solves the Poisson equation (see mbmesh_fast_poisson.h) unless
	 * --poisson-solver names another: it is given first, so that a later --poisson-solver wins. It
	 * solves the finest grid as far from the soundings as void filling keeps mesh, half --void-size.
	 * Without arguments the program prints its usage, and is left so.
	 * One more default of the module, only when not given: --threads 8 (fewer on a smaller machine),
	 * the chunks reconstructed at once, in place of the 4 of the program. The seams stay the program's
	 * (--seam-method solve): blend, though faster, moved the EM122 test survey's mesh by tens of meters. */
	std::vector<char *> arguments(argv, argv + argc);
	static char solver_option[] = "--poisson-solver";
	static char solver_name[] = "multigrid";
	static char threads_option[] = "--threads";
	static char threads_count[16];
	if (argc > 1) {
		std::vector<char *> defaults;
		if (long_option_value(argc, argv, "--poisson-solver") == nullptr) {
			defaults.insert(defaults.end(), {solver_option, solver_name});
		}
		if (long_option_value(argc, argv, "--threads") == nullptr) {
			const unsigned cores = std::thread::hardware_concurrency();
			std::snprintf(threads_count, sizeof(threads_count), "%u", cores == 0 ? 4u : std::min(cores, 8u));
			defaults.insert(defaults.end(), {threads_option, threads_count});
		}
		arguments.insert(arguments.begin() + 1, defaults.begin(), defaults.end());
	}
	const char *void_size = long_option_value(argc, argv, "--void-size");
	mbmesh_fast_poisson_set_min_reach(void_size != nullptr ? 0.5 * std::atof(void_size) : 1.0);
	arguments.push_back(nullptr);
	argc = static_cast<int>(arguments.size()) - 1;
	argv = arguments.data();

	std::ostringstream captured;
	std::streambuf *saved = nullptr;

	int status = 1;

	if (captured_output != nullptr) {
		*captured_output = nullptr;
		saved = std::cout.rdbuf(captured.rdbuf());
	}

	reset_getopt();
#ifdef _WIN32
	/* The getopt of Windows (getopt-win32, MinGW) names the program in its error
	 * messages from the __argv of the C runtime, not from argv, and in a host
	 * that does not set __argv (Julia starts with wmain) that is a NULL read. The
	 * program logs its own error for an unknown option, so getopt stays quiet. */
	const int saved_opterr = opterr;
	opterr = 0;
#endif
	try {
		status = mbmesh_program_main(argc, argv);
	}
	catch (const mbmesh_program_exit_request &request) {
		status = request.status;
	}
	catch (const std::exception &e) {
		std::cerr << "mbmesh: [fatal] " << e.what() << std::endl;
		status = 1;
	}
	catch (...) {
		std::cerr << "mbmesh: [fatal] unknown error" << std::endl;
		status = 1;
	}
	std::cout.flush();
#ifdef _WIN32
	opterr = saved_opterr;
#endif

	if (captured_output != nullptr) {
		std::cout.rdbuf(saved);
		const std::string text = captured.str();
		char *copy = static_cast<char *>(std::malloc(text.size() + 1));
		if (copy != nullptr) {
			std::memcpy(copy, text.c_str(), text.size() + 1);
		}
		*captured_output = copy;
	}
	return status;
}

extern "C" int mbmesh_read_glb_geodetic(const char *path, int threads, double **vertices, size_t *nvertices,
                                        int **triangles, size_t *ntriangles, char *error, size_t error_length) {
	*vertices = nullptr;
	*triangles = nullptr;
	*nvertices = 0;
	*ntriangles = 0;
	auto fail = [error, error_length](const std::string &message) {
		if (error != nullptr && error_length > 0) {
			std::strncpy(error, message.c_str(), error_length - 1);
			error[error_length - 1] = '\0';
		}
		return 1;
	};
	try {
		GlbMesh glb;
		std::string message;
		if (!read_mesh_glb(std::filesystem::path(path), &glb, &message, threads > 0 ? threads : 1))
			return fail(message);
		const Mesh &mesh = glb.mesh;
		if (mesh.vertices.empty() || mesh.indices.size() < 3)
			return fail(std::string("no triangles in ") + path);
		const size_t nv = mesh.vertices.size();
		const size_t nt = mesh.indices.size() / 3;
		double *v = static_cast<double *>(std::malloc(3 * nv * sizeof(double)));
		int *t = static_cast<int *>(std::malloc(3 * nt * sizeof(int)));
		if (v == nullptr || t == nullptr) {
			std::free(v);
			std::free(t);
			return fail("out of memory for the mesh");
		}
		for (size_t i = 0; i < nv; i++) {
			// a vertex is ECEF, or relative to the GeoOrigin's ECEF position (the file's georeference)
			const Vec3 ecef = glb.has_center ? mesh.vertices[i] + glb.center : mesh.vertices[i];
			const GeodeticPosition g = ecef_to_geodetic(ecef);
			v[3 * i] = g.longitude;
			v[3 * i + 1] = g.latitude;
			v[3 * i + 2] = g.elevation;
		}
		for (size_t i = 0; i < 3 * nt; i++) t[i] = static_cast<int>(mesh.indices[i]);
		*vertices = v;
		*triangles = t;
		*nvertices = nv;
		*ntriangles = nt;
		return 0;
	}
	catch (const std::exception &e) {
		return fail(e.what());
	}
}

/* The capture of mbmesh_run_mesh(): on for the length of that call (one run at a time, as getopt's state is global). */
namespace {
struct Capture {
	bool active = false;
	bool save_files = false;
	bool use_offset = false;   // the vertices are written relative to the GeoOrigin (-G, --geoorigin)
	const double *points = nullptr;   // the soundings handed over, or none: the input is the file -I names
	std::size_t npoints = 0;
	std::vector<double> vertices;
	std::vector<int> triangles;
};
Capture capture;
}  // namespace

bool mbmesh_capture_active() { return capture.active; }
bool mbmesh_capture_save_files() { return capture.save_files; }

const double *mbmesh_capture_points(std::size_t *count) {
	*count = capture.active ? capture.npoints : 0;
	return capture.active ? capture.points : nullptr;
}

void mbmesh_capture_mesh(const Mesh &mesh, const Vec3 &offset) {
	const Vec3 shift = capture.use_offset ? offset : Vec3(0.0, 0.0, 0.0);
	const size_t nv = mesh.vertices.size();
	capture.vertices.resize(3 * nv);
	const long count = static_cast<long>(nv);
	const int threads = static_cast<int>(std::max(1u, std::min(16u, std::thread::hardware_concurrency())));
	mbmesh::parallel_for_ranges(0, count, nv > 100000 ? threads : 1, [&](long begin, long end) {
		for (long i = begin; i < end; i++) {
			const GeodeticPosition g = ecef_to_geodetic(mesh.vertices[static_cast<size_t>(i)] + shift);
			capture.vertices[3 * i] = g.longitude;
			capture.vertices[3 * i + 1] = g.latitude;
			capture.vertices[3 * i + 2] = g.elevation;
		}
	});
	capture.triangles.assign(mesh.indices.begin(), mesh.indices.end());
}

namespace {
int run_mesh(int argc, char **argv, const double *points, size_t npoints, int save_files, double **vertices,
             size_t *nvertices, int **triangles, size_t *ntriangles) {
	*vertices = nullptr;
	*triangles = nullptr;
	*nvertices = 0;
	*ntriangles = 0;
	capture = Capture();
	capture.active = true;
	capture.save_files = save_files != 0;
	capture.points = points;
	capture.npoints = points != nullptr ? npoints : 0;
	for (int i = 1; i < argc; i++) {
		if (argv[i] != nullptr && (std::strncmp(argv[i], "-G", 2) == 0 || std::strncmp(argv[i], "--geoorigin", 11) == 0)) {
			capture.use_offset = true;
		}
	}
	const int status = mbmesh_run_program(argc, argv, nullptr);
	capture.active = false;
	if (status == 0 && !capture.vertices.empty() && capture.triangles.size() >= 3) {
		double *v = static_cast<double *>(std::malloc(capture.vertices.size() * sizeof(double)));
		int *t = static_cast<int *>(std::malloc(capture.triangles.size() * sizeof(int)));
		if (v != nullptr && t != nullptr) {
			std::memcpy(v, capture.vertices.data(), capture.vertices.size() * sizeof(double));
			std::memcpy(t, capture.triangles.data(), capture.triangles.size() * sizeof(int));
			*vertices = v;
			*triangles = t;
			*nvertices = capture.vertices.size() / 3;
			*ntriangles = capture.triangles.size() / 3;
		}
		else {
			std::free(v);
			std::free(t);
		}
	}
	capture = Capture();
	return status;
}
}  // namespace

extern "C" int mbmesh_run_mesh(int argc, char **argv, int save_files, double **vertices, size_t *nvertices,
                               int **triangles, size_t *ntriangles) {
	return run_mesh(argc, argv, nullptr, 0, save_files, vertices, nvertices, triangles, ntriangles);
}

extern "C" int mbmesh_run_mesh_points(int argc, char **argv, const double *points, size_t npoints, int save_files,
                                      double **vertices, size_t *nvertices, int **triangles, size_t *ntriangles) {
	return run_mesh(argc, argv, points, npoints, save_files, vertices, nvertices, triangles, ntriangles);
}

extern "C" int mbmesh_write_glb(const char *path, const double *vertices, size_t nvertices, const int *triangles,
                                size_t ntriangles, int geographic, char *error, size_t error_length) {
	auto fail = [error, error_length](const std::string &message) {
		if (error != nullptr && error_length > 0) {
			std::strncpy(error, message.c_str(), error_length - 1);
			error[error_length - 1] = '\0';
		}
		return 1;
	};
	if (path == nullptr || vertices == nullptr || triangles == nullptr || nvertices == 0 || ntriangles == 0)
		return fail("no mesh to write");
	try {
		Mesh mesh;
		mesh.vertices.resize(nvertices);
		mesh.indices.resize(3 * ntriangles);
		for (size_t i = 0; i < 3 * ntriangles; i++) {
			if (triangles[i] < 0 || static_cast<size_t>(triangles[i]) >= nvertices)
				return fail("a triangle names a vertex the mesh does not have");
			mesh.indices[i] = static_cast<unsigned int>(triangles[i]);
		}
		RunInfo run;
		run.program = "mbmesh";
		run.version = MB_VERSION;
		if (geographic != 0) {
			double west = vertices[0], east = vertices[0], south = vertices[1], north = vertices[1], sum_height = 0.0;
			for (size_t i = 0; i < nvertices; i++) {
				west = std::min(west, vertices[3 * i]);
				east = std::max(east, vertices[3 * i]);
				south = std::min(south, vertices[3 * i + 1]);
				north = std::max(north, vertices[3 * i + 1]);
				sum_height += vertices[3 * i + 2];
			}
			// The GeoOrigin, as mbmesh's automatic one: the middle of the mesh, at its mean height.
			GeodeticPosition middle;
			middle.longitude = 0.5 * (west + east);
			middle.latitude = 0.5 * (south + north);
			middle.elevation = sum_height / static_cast<double>(nvertices);
			const Vec3 center = geodetic_to_ecef(middle);
			for (size_t i = 0; i < nvertices; i++) {
				GeodeticPosition g;
				g.longitude = vertices[3 * i];
				g.latitude = vertices[3 * i + 1];
				g.elevation = vertices[3 * i + 2];
				mesh.vertices[i] = geodetic_to_ecef(g) - center;
			}
			run.geoorigin_mode = "automatic";
			run.has_center = true;
			run.center_x = center.x;
			run.center_y = center.y;
			run.center_z = center.z;
			run.center_longitude = middle.longitude;
			run.center_latitude = middle.latitude;
			run.center_height = middle.elevation;
			run.extent_west = west;
			run.extent_east = east;
			run.extent_south = south;
			run.extent_north = north;
			run.mean_elevation = middle.elevation;
		}
		else {
			for (size_t i = 0; i < nvertices; i++)
				mesh.vertices[i] = Vec3(vertices[3 * i], vertices[3 * i + 1], vertices[3 * i + 2]);
		}
		if (geographic != 0)
			set_glb_run_metadata(run_metadata(run));
		set_glb_compression(mbdraco::Settings());
		std::string message;
		const bool written = write_mesh_glb_file(std::filesystem::u8path(path), mesh, &message,
		                                         GlbContent{"mesh", "The reconstructed seafloor surface"});
		clear_glb_run_metadata();
		return written ? 0 : fail(message.empty() ? std::string("cannot write ") + path : message);
	}
	catch (const std::exception &e) {
		clear_glb_run_metadata();
		return fail(e.what());
	}
}

extern "C" void mbmesh_free(void *pointer) { std::free(pointer); }
