#------------------------------------------------------------------------------
#    The MB-system:  src/gmt/mbgrd2gltf/CMakeLists.txt
#
#    See README.md file for copying and redistribution conditions.
#------------------------------------------------------------------------------
# The mbgrd2gltf pipeline of the GMT module (src/gmt/mbgrd2gltf.c), built here from
# the unchanged upstream sources in src/mbgrd2gltf plus the fork's plain C bridge
# (mbgrd2gltf_capi.{h,cpp}). src/mbgrd2gltf and its program are left as upstream
# has them. Upstream model.cpp instantiates tinygltf: in the GMT plugin it is the
# only instantiation (the mbmesh pipeline takes the declarations only, see
# ../mbmesh/shim).

set(MBGRD2GLTF_UPSTREAM ${CMAKE_SOURCE_DIR}/src/mbgrd2gltf)

find_package(NetCDF REQUIRED)

add_library(mbgrd2gltf_pipeline STATIC
  mbgrd2gltf_capi.cpp
  ${MBGRD2GLTF_UPSTREAM}/bathymetry.cpp
  ${MBGRD2GLTF_UPSTREAM}/geometry.cpp
  ${MBGRD2GLTF_UPSTREAM}/logger.cpp
  ${MBGRD2GLTF_UPSTREAM}/model.cpp
  ${MBGRD2GLTF_UPSTREAM}/options.cpp
  ${MBGRD2GLTF_UPSTREAM}/provenance.cpp
  ${MBGRD2GLTF_UPSTREAM}/viewpoint.cpp
  # the page for CesiumJS (--cesium-html), from the source of mbmesh, as the program has it
  ${CMAKE_SOURCE_DIR}/src/mbmesh/src/io/cesium_writer.cpp)
target_compile_features(mbgrd2gltf_pipeline PUBLIC cxx_std_17)
set_target_properties(mbgrd2gltf_pipeline PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mbgrd2gltf_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(mbgrd2gltf_pipeline PRIVATE
  ${MBGRD2GLTF_UPSTREAM}
  ${MBGRD2GLTF_UPSTREAM}/tinygltf
  ${NetCDF_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/src/mbmesh/include
  ${CMAKE_SOURCE_DIR}/src/mbio
  ${CMAKE_SOURCE_DIR}/src/mbdraco)
target_link_libraries(mbgrd2gltf_pipeline PUBLIC ${NetCDF_LIBRARIES} mbdraco ${CMAKE_DL_LIBS})
