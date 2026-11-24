#pragma once

#include <filesystem>
#include <string>

#include "io/datalist_reader.h"

// What upstream settings.cpp calls in place of read_datalist() (settings_hooked.cpp routes it): the soundings a host
// handed over in memory (mbmesh_run_mesh_points), or the one swath file named by -I without a datalist, or else the
// datalist, read by upstream read_datalist(). While a host takes the mesh in memory nothing spills to disk.
bool mbmesh_program_read_datalist(const std::filesystem::path &path, const DatalistReaderOptions &options,
                                  DatalistReadResult *result, std::string *error);
