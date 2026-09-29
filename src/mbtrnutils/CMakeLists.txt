#------------------------------------------------------------------------------
#    The MB-system:  CMakeLists.txt   27 June 2023
#
#    Copyright (c) 2023-2024 by
#    David W. Caress (caress@mbari.org)
#      Monterey Bay Aquarium Research Institute
#      Moss Landing, California, USA
#    Dale N. Chayes 
#      Center for Coastal and Ocean Mapping
#      University of New Hampshire
#      Durham, New Hampshire, USA
#    Christian dos Santos Ferreira
#      MARUM
#      University of Bremen
#      Bremen Germany
#      
#    MB-System was created by Caress and Chayes in 1992 at the
#      Lamont-Doherty Earth Observatory
#      Columbia University
#      Palisades, NY 10964
#
#    See README.md file for copying and redistribution conditions.
#------------------------------------------------------------------------------
#
# MB-System Terrain Relative Navigation (TRN) Tools (mbsystem/src/mbtrnutils)
#
# This directory includes the MB-System TRN program mbtrnpp, which is used
# to use new swath bathymetry data to localize a platform relative to a
# preexisting topography model, in real time or offline. Also included are two
# supporting programs having to do with obtaining the preexisting topographic
# model in the octree form used by TRN. The program mbgrd2octree converts a
# GMT grid into an octree, and mbgrdtilemaker generates tiled octrees needed
# for handling very large models.
#
message("In src/mbtrnutils")
#
#------------------------------------------------------------------------------
# The TRN sources reach for POSIX headers that MSVC does not ship. On Windows
# they include the substitutes unistd_w.h / dirent_w.h from src/mbio, and the
# BSD-socket headers (sys/socket.h, arpa/inet.h, netinet/*, netdb.h) plus
# libgen.h come from the Winsock-backed shims in src/mbtrnframe/win32.
if(WIN32)
  # Build the TRN libraries static on Windows. Several of them export data
  # (mbtrnframe me_errno, newmat BaseException::what_error), and data crossing
  # a DLL boundary needs explicit dllexport/dllimport annotations these
  # sources do not carry. They are internal libraries, so linking them in
  # directly is the simpler and safer answer.
  set(BUILD_SHARED_LIBS OFF)
  # tells the headers not to mark their exports dllimport/dllexport
  add_compile_definitions(TRNW_STATIC)
  include_directories(${CMAKE_SOURCE_DIR}/src/mbio
                      ${CMAKE_SOURCE_DIR}/src/mbtrnframe/win32)
  # Force-include the compat header, as the top-level build already does for
  # mb_define.h. It supplies the POSIX names the sources use without including
  # the declaring header, and guarantees winsock2.h precedes windows.h.
  if(MSVC)
    add_compile_options(/FI${CMAKE_SOURCE_DIR}/src/mbtrnframe/win32/mbtrn_w32_compat.h)
  endif()
endif()
#
#------------------------------------------------------------------------------

find_package(NetCDF REQUIRED)
find_package(LibPROJ REQUIRED)

#------------------------------------------------------------------------------
add_compile_definitions(
WITH_MMDEBUG
WITH_MST_MSTATS
MST_STATS_EN
)

add_executable(mbtrnpp mbtrnpp.c)
target_include_directories(mbtrnpp PRIVATE ${NetCDF_INCLUDE_DIRS} ${LibPROJ_INCLUDE_DIRS}
										${CMAKE_SOURCE_DIR}/src/mbtrnframe
                                        ${CMAKE_SOURCE_DIR}/src/mbtrnav/terrain-nav
                                        ${CMAKE_SOURCE_DIR}/src/mbtrn/r7kr
										${CMAKE_SOURCE_DIR}/src/mbtrnav/trnw
										${CMAKE_SOURCE_DIR}/src/mbtrnav/utils
										${CMAKE_SOURCE_DIR}/src/mbtrn/mb1r )
target_compile_definitions(mbtrnpp PRIVATE WITH_MBTNAV)
target_link_libraries(mbtrnpp PRIVATE mbio mbaux trnw qnx newmat tnav geolib netif r7kr mb1r qnx mbtrnframe geocon NetCDF::NetCDF LibPROJ::LibPROJ )

#------------------------------------------------------------------------------

add_executable(mbgrd2octree mbgrd2octree.cc)
target_include_directories(mbgrd2octree PRIVATE ${NetCDF_INCLUDE_DIRS}
                                        ${CMAKE_SOURCE_DIR}/src/mbtrnav/terrain-nav )
target_link_libraries(mbgrd2octree PRIVATE mbio mbaux tnav NetCDF::NetCDF )

#------------------------------------------------------------------------------

add_executable(mbgrdtilemaker mbgrdtilemaker.cc)
target_link_libraries(mbgrdtilemaker PRIVATE mbio mbaux NetCDF::NetCDF )

#------------------------------------------------------------------------------
# install it all
#
install(TARGETS mbtrnpp mbgrd2octree mbgrdtilemaker DESTINATION ${CMAKE_INSTALL_BINDIR})
#
#------------------------------------------------------------------------------
