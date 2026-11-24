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
#include "mbmesh_fast_poisson.h"

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

extern "C" void mbmesh_free(void *pointer) { std::free(pointer); }
