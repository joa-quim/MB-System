/*--------------------------------------------------------------------
 *    The MB-system:  mbmesh_program.cpp
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * The mbmesh program, built into the mbmesh_pipeline library so that the GMT
 * module (src/gmt/mbmesh.c, through mbmesh_capi.cpp) runs exactly the code of
 * the program, whatever options upstream adds to it, without a copy of its
 * driver to keep in step.
 *
 * mbmesh.cpp is compiled here unchanged, with two names changed:
 *   main       becomes mbmesh_program_main(), which mbmesh_run_program() calls;
 *   std::exit  becomes std::mbmesh_program_exit(), which throws instead of
 *              ending the process: parse_options() calls std::exit(1) on a bad
 *              option, which in a GMT module would end the whole host session
 *              (Julia, Python, MATLAB). mbmesh_run_program() catches it and
 *              returns the status.
 *
 * Every header that mbmesh.cpp includes is included first, so that the two
 * macros reach only mbmesh.cpp itself and never a library header.
 */

#include <algorithm>
#include <cstdio>
#include <ctime>
#ifdef _MSC_VER
#include "unistd_w.h"
#else
#include <unistd.h>
#endif
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cmath>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "settings.h"

#include "util/logger.h"
#include "util/stopwatch.h"

#include "io/coordinate_frame.h"
#include "io/xyz_writer.h"
#include "draco_mesh.h"
#include "io/glb_writer.h"
#include "io/tileset_writer.h"
#include "io/point_spill.h"
#include "util/memory.h"
#include "io/track.h"
#include "io/cesium_convert.h"
#include "io/cesium_writer.h"
#include "io/x3dom_writer.h"

#include "mbmesh_program.h"

namespace std {
// Declared in std only so that the std::exit() calls of mbmesh.cpp reach it through the macro below.
[[noreturn]] void mbmesh_program_exit(int status) {
    throw mbmesh_program_exit_request{status};
}
}  // namespace std

#define main mbmesh_program_main
#define exit(status) mbmesh_program_exit(status)
#include "mbmesh.cpp"
#undef exit
#undef main
