/*--------------------------------------------------------------------
 *    The MB-system:  tinygltf_impl.cpp
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * The single translation unit that instantiates the header-only tinygltf
 * library.
 *
 * Both mbgrd2gltf (model.cpp) and mbmesh (src/io/glb_writer.cpp) write glTF,
 * and both used to define TINYGLTF_IMPLEMENTATION themselves. That is fine
 * while they are separate programs, but the GMT supplement links both
 * pipelines into one library, and the duplicate definitions then collide
 * (LNK2005). Instantiating it once here, in a small library both pipelines
 * link, keeps a single copy of the symbols.
 *
 * The TINYGLTF_NO_STB_IMAGE* settings match what both callers used, so the
 * generated code is unchanged.
 */

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "tiny_gltf.h"
