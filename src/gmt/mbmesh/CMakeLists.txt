#------------------------------------------------------------------------------
#    The MB-system:  src/gmt/mbmesh/CMakeLists.txt
#
#    See README.md file for copying and redistribution conditions.
#------------------------------------------------------------------------------
# The mbmesh pipeline of the GMT module (src/gmt/mbmesh.c), built here from the
# unchanged upstream sources in src/mbmesh plus the fork's own files:
#   mbmesh_capi.{h,cpp}       the plain C bridge mbmesh.c calls
#   mbmesh_program.{h,cpp}    upstream mbmesh.cpp compiled in, main renamed
#   screened_poisson_fast.cpp upstream's screened_poisson.cpp plus the octree
#                             multigrid (--poisson-solver multigrid, the
#                             module's default), in place of upstream's file
#   poisson_multigrid.{h,cpp} the multigrid solver
#   fast_weld.{h,cpp}         a faster copy of upstream weld_coincident_vertices(),
#                             the same result
# src/mbmesh itself, and its mbmesh program, are left as upstream has them.

set(MBMESH_UPSTREAM ${CMAKE_SOURCE_DIR}/src/mbmesh)

find_package(Threads REQUIRED)

add_library(mbmesh_pipeline STATIC
  mbmesh_capi.cpp
  mbmesh_program.cpp
  screened_poisson_fast.cpp
  poisson_multigrid.cpp
  fast_weld.cpp
  settings_hooked.cpp
  ${MBMESH_UPSTREAM}/src/io/coordinate_frame.cpp
  input_hooks.cpp
  ${MBMESH_UPSTREAM}/src/io/point_spill.cpp
  ${MBMESH_UPSTREAM}/src/io/xyz_writer.cpp
  ${MBMESH_UPSTREAM}/src/io/track.cpp
  ${MBMESH_UPSTREAM}/src/io/provenance.cpp
  ${MBMESH_UPSTREAM}/src/io/glb_writer.cpp
  ${MBMESH_UPSTREAM}/src/io/tileset_writer.cpp
  ${MBMESH_UPSTREAM}/src/io/x3dom_writer.cpp
  ${MBMESH_UPSTREAM}/src/io/cesium_writer.cpp
  ${MBMESH_UPSTREAM}/src/algorithms/point_decimation.cpp
  ${MBMESH_UPSTREAM}/src/algorithms/normal_estimation.cpp
  ${MBMESH_UPSTREAM}/src/algorithms/marching_cubes.cpp
  ${MBMESH_UPSTREAM}/src/algorithms/support_trimming.cpp
  ${MBMESH_UPSTREAM}/src/algorithms/seam_blending.cpp
  ${MBMESH_UPSTREAM}/src/algorithms/reconstruction.cpp
  # mbglb2tiles' GLB reader (Draco or plain): the bridge hands a mesh.glb back to a host as lon/lat/height
  ${CMAKE_SOURCE_DIR}/src/mbglb2tiles/glb_reader.cpp)
target_compile_features(mbmesh_pipeline PUBLIC cxx_std_17)
set_target_properties(mbmesh_pipeline PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Upstream glb_writer.cpp instantiates tinygltf, which the GMT plugin already has once
# (upstream model.cpp, in mbgrd2gltf_pipeline): shim/tinygltf/tiny_gltf.h, found first,
# takes only its declarations.
target_include_directories(mbmesh_pipeline BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim)
target_include_directories(mbmesh_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(mbmesh_pipeline PRIVATE
  ${MBMESH_UPSTREAM}
  ${MBMESH_UPSTREAM}/include
  ${CMAKE_SOURCE_DIR}/src/mbio
  ${CMAKE_SOURCE_DIR}/src/mbgrd2gltf
  ${CMAKE_SOURCE_DIR}/src/mbgrd2gltf/tinygltf
  ${CMAKE_SOURCE_DIR}/src/mbglb2tiles
  ${CMAKE_SOURCE_DIR}/src/mbdraco)
target_link_libraries(mbmesh_pipeline PUBLIC mbio mbdraco mbgrd2gltf_pipeline Threads::Threads)

# screened_poisson_fast.cpp is upstream's screened_poisson.cpp plus the multigrid: when an
# upstream sync changes that file, the change must be merged into the copy.
file(SHA256 ${MBMESH_UPSTREAM}/src/algorithms/screened_poisson.cpp MBMESH_UPSTREAM_POISSON_SHA)
if(NOT MBMESH_UPSTREAM_POISSON_SHA STREQUAL "17aece467a39225ac12300ffb42c9d508a1120da3b0ff84acb44cd420e8d24cd")
  message(WARNING "src/mbmesh/src/algorithms/screened_poisson.cpp changed upstream: merge the change into "
                  "src/gmt/mbmesh/screened_poisson_fast.cpp, then update the SHA-256 in src/gmt/mbmesh/CMakeLists.txt")
endif()

if(buildTests)
  # The multigrid solver against sor run to convergence.
  add_executable(mbmesh_test_poisson_multigrid
    test_poisson_multigrid.cpp
    screened_poisson_fast.cpp
    poisson_multigrid.cpp
    ${MBMESH_UPSTREAM}/src/algorithms/marching_cubes.cpp)
  target_compile_features(mbmesh_test_poisson_multigrid PRIVATE cxx_std_17)
  target_link_libraries(mbmesh_test_poisson_multigrid PRIVATE Threads::Threads)
  target_include_directories(mbmesh_test_poisson_multigrid PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MBMESH_UPSTREAM}/include)
  add_test(NAME mbmesh_poisson_multigrid COMMAND mbmesh_test_poisson_multigrid)
endif()
