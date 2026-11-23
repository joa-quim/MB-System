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
#include <iostream>
#include <sstream>
#include <string>

#include <getopt.h>

#include "mbmesh_program.h"
#include "mbmesh_capi.h"

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

} /* namespace */

extern "C" int mbmesh_run_program(int argc, char **argv, char **captured_output) {
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
