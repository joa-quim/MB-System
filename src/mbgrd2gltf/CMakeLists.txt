# ------------------------------------------------------------------------------
# The MB-system:  CMakeLists.txt   27 June 2023
#
# Copyright (c) 2023-2024 by
# David W. Caress (caress@mbari.org)
# Monterey Bay Aquarium Research Institute
# Moss Landing, California, USA
# Dale N. Chayes
# Center for Coastal and Ocean Mapping
# University of New Hampshire
# Durham, New Hampshire, USA
# Christian dos Santos Ferreira
# MARUM
# University of Bremen
# Bremen Germany
#
# MB-System was created by Caress and Chayes in 1992 at the
# Lamont-Doherty Earth Observatory
# Columbia University
# Palisades, NY 10964
#
# See README.md file for copying and redistribution conditions.
# ------------------------------------------------------------------------------

message("In src/mbgrd2gltf")

find_package(NetCDF REQUIRED)

# Attempt to find an installed version of Draco
# Commented out to force use of local Draco source files for compatibility
# find_package(Draco)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the draco directory
set(DRACO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/draco")

# Initialize INCLUDE_DIRS
set(INCLUDE_DIRS "")

# Pipeline sources shared by the mbgrd2gltf program and the GMT module. The C
# bridge (mbgrd2gltf_capi.cpp) is built here, with the C++ it wraps, so that
# src/gmt/mbgrd2gltf.c can stay plain C.
set(SOURCES bathymetry.cpp geometry.cpp logger.cpp model.cpp options.cpp mbgrd2gltf_capi.cpp)

# Manually set the include directories for Draco and other components
include_directories(
	${NetCDF_INCLUDE_DIRS}
	${CMAKE_CURRENT_SOURCE_DIR}/tinygltf
	${DRACO_DIR}
)

if(NOT TARGET Draco::Draco)
	if(NOT Draco_FOUND)
		message("Using local Draco source files from the project directory")

		# Recursively find all Draco .cc files and .h files
		file(GLOB_RECURSE DRACO_SOURCES "${DRACO_DIR}/*.cc" "${DRACO_DIR}/*.h")

		# Recursively filter out all test files
		list(FILTER DRACO_SOURCES EXCLUDE REGEX ".*/.*test.*")

		# Set the include directories and sources conditionally.
		set(INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/draco)
		set(SOURCES ${SOURCES} ${DRACO_SOURCES})
		# No external libraries needed since we're compiling the source
		set(DRACO_LIBRARIES "")
	else()
		message("Using installed Draco, but Draco target not found, checking for libraries...")
		find_library(DRACO_LIBRARY NAMES draco PATHS /usr/local/lib)

		if(DRACO_LIBRARY)
			set(DRACO_LIBRARIES ${DRACO_LIBRARY})
			find_path(DRACO_INCLUDE_DIR draco/draco_features.h /usr/local/include)
			include_directories(${DRACO_INCLUDE_DIR})
		else()
			message(FATAL_ERROR "Draco library not found.")
		endif()
	endif()
else()
	message("Using installed Draco target: Draco::Draco")
	set(DRACO_LIBRARIES Draco::Draco)
endif()

# The pipeline as a library, so both the program and the GMT module can use
# it. It deliberately does not link mbsystem: the supplement links this
# library, so depending on it here would be a cycle.
# tinygltf is header-only; instantiate it exactly once, in a library that both
# this pipeline and mbmesh link, so the GMT supplement does not end up with
# two copies of its symbols.
add_library(tinygltf_impl STATIC tinygltf_impl.cpp)
set_target_properties(tinygltf_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(tinygltf_impl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tinygltf)

add_library(mbgrd2gltf_pipeline STATIC ${SOURCES})
set_target_properties(mbgrd2gltf_pipeline PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(mbgrd2gltf_pipeline
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${NetCDF_INCLUDE_DIRS}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tinygltf
	PRIVATE ${DRACO_DIR}
	PRIVATE ${INCLUDE_DIRS}
)

target_link_libraries(mbgrd2gltf_pipeline
	PUBLIC tinygltf_impl
	PUBLIC ${NetCDF_LIBRARIES}
	PUBLIC ${DRACO_LIBRARIES}
	PUBLIC ${CMAKE_DL_LIBS}
)

# Specify sources for the executable
add_executable(mbgrd2gltf main.cpp)

# Set and link the include directories
target_include_directories(mbgrd2gltf
	PRIVATE ${NetCDF_INCLUDE_DIRS}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tinygltf
	PRIVATE ${DRACO_DIR}
	PRIVATE ${INCLUDE_DIRS}
)

target_link_libraries(mbgrd2gltf
	PRIVATE mbgrd2gltf_pipeline
	PRIVATE mbsystem
	PRIVATE ${NetCDF_LIBRARIES}
	PRIVATE ${DRACO_LIBRARIES}
	PRIVATE ${CMAKE_DL_LIBS}
)

install(TARGETS mbgrd2gltf
	DESTINATION ${CMAKE_INSTALL_BINDIR}
)
