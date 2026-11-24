#pragma once

#include <cstddef>

#include "data_types/geometry.h"

// The fork's copy of upstream weld_coincident_vertices() (src/mbmesh/src/algorithms/seam_blending.cpp), which
// mbmesh.cpp calls on the whole mesh of a chunked reconstruction to join the chunks at their seams. The same result,
// vertex for vertex (each vertex merged into the first earlier one within the tolerance, found in the same order; the
// kept vertices in their order; the triangles and normals made the same way), with the cells of the tolerance held in
// flat arrays instead of a hash map of vectors: about ten times faster on a mesh of millions of vertices.
// mbmesh_program.cpp routes mbmesh.cpp's calls to it.
std::size_t mbmesh_fast_weld_coincident_vertices(Mesh *mesh, double tolerance);
