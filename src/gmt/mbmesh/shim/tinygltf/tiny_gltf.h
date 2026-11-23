/*
 * Seen first by the mbmesh pipeline of the GMT module (src/gmt/mbmesh/CMakeLists.txt) when
 * upstream's src/mbmesh/src/io/glb_writer.cpp includes "tinygltf/tiny_gltf.h". That file
 * instantiates tinygltf (TINYGLTF_IMPLEMENTATION), which is right for the mbmesh program but
 * would collide in the GMT plugin with the instantiation it already has (upstream
 * src/mbgrd2gltf/model.cpp, in mbgrd2gltf_pipeline). Here only the declarations are taken.
 */
#undef TINYGLTF_IMPLEMENTATION
#include "../../../../mbgrd2gltf/tinygltf/tiny_gltf.h"
