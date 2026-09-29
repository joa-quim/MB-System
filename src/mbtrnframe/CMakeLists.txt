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

#------------------------------------------------------------------------------
#
# Configurable error and debugging output library generally called mframe but
# here named mbtrnframe (mbsystem/src/mbtrnframe) and used by code in
# mbsystem/src/mbtrn mbsystem/src/mbtrnav mbsystem/src/mbtrnutils
#
#------------------------------------------------------------------------------
message("In src/mbtrnframe")
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
#------------------------------------------------------------------------------
#
add_compile_options(
  -DHAVE_CONFIG_H  
  -D_GNU_SOURCE 
  -DTRNC_VER=1.0 
  -DFRAMES7K_VER=1.0 
  -DSTREAM7K_VER=1.0 
  -DTBINX_VER=1.0 
  -DEMU7K_VER=1.0 
  -DWITH_TESTS 
  -DWITH_R7KR_TEST 
  -DWITH_MB1R_TEST 
  -DWITH_MST_MSTATS 
  -DMST_STATS_EN 
  -DMXDEBUG_C_API)
add_library(
  mbtrnframe
  mbbuf.c
  mcbuf.c
  mconfig.c
  merror.c
  mfile.c
  mkvconf.c
  mlist.c
  mlog.c
  mmdebug.c
  msocket.c
  mstats.c
  mswap.c
  mthread.c
  mtime.c
  mutils.c
  mxdebug.cpp)
target_link_libraries(mbtrnframe PRIVATE ${MB_SYSLIBS})
#
#------------------------------------------------------------------------------
#
# install it all
install(TARGETS mbtrnframe DESTINATION ${CMAKE_INSTALL_LIBDIR})
#
#------------------------------------------------------------------------------
