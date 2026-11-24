#pragma once

#include <cstddef>

#include "data_types/geometry.h"

// The mesh of a run handed to the host in memory instead of read back from mesh.glb (mbmesh_run_mesh() in
// mbmesh_capi.h). mbmesh_program.cpp routes mbmesh.cpp's GLB writes here: while a capture is on, the mesh it
// would write as mesh.glb is kept, and the files are written only when the host asked for them.
bool mbmesh_capture_active();
bool mbmesh_capture_save_files();
// `mesh`: the vertices as mbmesh writes them, Earth-centred minus `offset` (the GeoOrigin's, or zero).
void mbmesh_capture_mesh(const Mesh &mesh, const Vec3 &offset);
// The soundings the host handed over (longitude, latitude, height for each), or nullptr: the input is the file -I names.
const double *mbmesh_capture_points(std::size_t *count);
