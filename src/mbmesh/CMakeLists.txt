#------------------------------------------------------------------------------
#    The MB-system:  CMakeLists.txt   3/6/2026
#
#    Copyright (c) 2026 by
#    [Your Team/Institution]
#
#    See README.md file for copying and redistribution conditions.
#------------------------------------------------------------------------------

message("In src/mbmesh")

# Pipeline sources shared by the mbmesh program and the GMT module. The C
# bridge (mbmesh_capi.cpp) is built here, with the C++ it wraps, so that
# src/gmt/mbmesh.c can stay plain C.
set(MBMESH_PIPELINE_SOURCES
  settings.cpp
  mbmesh_capi.cpp
  src/io/datalist_reader.cpp
  src/io/xyz_writer.cpp
  src/io/glb_writer.cpp
  src/io/x3dom_writer.cpp
  src/algorithms/point_decimation.cpp
  src/algorithms/normal_estimation.cpp
  src/algorithms/screened_poisson.cpp
  src/algorithms/marching_cubes.cpp
  src/algorithms/support_trimming.cpp)

add_library(mbmesh_pipeline STATIC ${MBMESH_PIPELINE_SOURCES})
target_compile_features(mbmesh_pipeline PUBLIC cxx_std_17)
set_target_properties(mbmesh_pipeline PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(mbmesh_pipeline PUBLIC mbio tinygltf_impl)
target_include_directories(mbmesh_pipeline PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/mbio
  ${CMAKE_SOURCE_DIR}/src/mbgrd2gltf)

add_executable(mbmesh mbmesh.cpp)
target_compile_features(mbmesh PRIVATE cxx_std_17)

# Link to MB-System mbio library
target_link_libraries(mbmesh PRIVATE mbmesh_pipeline mbio)

target_include_directories(mbmesh PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/mbio
  ${CMAKE_SOURCE_DIR}/src/mbgrd2gltf)

install(TARGETS mbmesh DESTINATION ${CMAKE_INSTALL_BINDIR})

if(buildTests)
  add_executable(mbmesh_test_support_trimming
    tests/test_support_trimming.cpp
    src/algorithms/support_trimming.cpp
    src/algorithms/screened_poisson.cpp
    src/algorithms/marching_cubes.cpp
    src/io/glb_writer.cpp)
  target_compile_features(mbmesh_test_support_trimming PRIVATE cxx_std_17)
  # compiles glb_writer.cpp directly, so it needs the single tinygltf
  # instantiation that the pipeline libraries share
  target_link_libraries(mbmesh_test_support_trimming PRIVATE tinygltf_impl)
  target_include_directories(mbmesh_test_support_trimming PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/mbgrd2gltf)
  add_test(NAME mbmesh_support_trimming COMMAND mbmesh_test_support_trimming)

  add_executable(mbmesh_test_settings
    tests/test_settings.cpp
    settings.cpp
    src/io/datalist_reader.cpp
    src/algorithms/point_decimation.cpp
    src/algorithms/normal_estimation.cpp
    src/algorithms/screened_poisson.cpp
    src/algorithms/marching_cubes.cpp
    src/algorithms/support_trimming.cpp)
  target_compile_features(mbmesh_test_settings PRIVATE cxx_std_17)
  target_include_directories(mbmesh_test_settings PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/mbio
    ${CMAKE_SOURCE_DIR}/src/mbgrd2gltf)
  target_link_libraries(mbmesh_test_settings PRIVATE mbio)
  add_test(NAME mbmesh_settings COMMAND mbmesh_test_settings)

  add_executable(mbmesh_test_marching_cubes
    tests/test_marching_cubes.cpp
    src/algorithms/marching_cubes.cpp
    src/io/glb_writer.cpp)
  target_compile_features(mbmesh_test_marching_cubes PRIVATE cxx_std_17)
  # compiles glb_writer.cpp directly, so it needs the single tinygltf
  # instantiation that the pipeline libraries share
  target_link_libraries(mbmesh_test_marching_cubes PRIVATE tinygltf_impl)
  target_include_directories(mbmesh_test_marching_cubes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/mbgrd2gltf)
  add_test(NAME mbmesh_marching_cubes COMMAND mbmesh_test_marching_cubes)

  add_executable(mbmesh_test_normal_estimation
    tests/test_normal_estimation.cpp
    src/algorithms/normal_estimation.cpp
    src/io/glb_writer.cpp)
  target_compile_features(mbmesh_test_normal_estimation PRIVATE cxx_std_17)
  # compiles glb_writer.cpp directly, so it needs the single tinygltf
  # instantiation that the pipeline libraries share
  target_link_libraries(mbmesh_test_normal_estimation PRIVATE tinygltf_impl)
  target_include_directories(mbmesh_test_normal_estimation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/mbgrd2gltf)
  add_test(NAME mbmesh_normal_estimation COMMAND mbmesh_test_normal_estimation)

  add_test(NAME mbmesh_test COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/mbmesh_test.py")
endif()
