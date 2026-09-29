# Use this file to override variables
#
# O nome da DLL é controlado no src/lib_proj.cmake
# set_target_properties(${PROJ_CORE_TARGET} PROPERTIES RUNTIME_OUTPUT_NAME ${PROJ4_DLL_RENAME})
# Para o descobrir o melhor é grepar por set_target_properties ou DLL_RENAME
#

# Set build type can be: empty, Debug, Release, RelWithDebInfo or MinSizeRel
set (CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)

if (WIN32)
	# No Motif/X11 on Windows — disable GUI build (mbview, mbedit, mbnavadjust, mbnavedit,
	# mbeditviz, mbgrdviz, mbvelocitytool all require Motif via find_package(Motif REQUIRED)).
	set (buildGUIs OFF CACHE BOOL "" FORCE)

	set (BITAGE 64)
	# Detect if we are building a 32 or 64 bits version
	if (CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(BITAGE 32)
	endif ()
	set (CMAKE_INSTALL_PREFIX C:/progs_cygw/MB-System_take2/compileds/VC14_${BITAGE}  CACHE STRING "" FORCE) 

	# Uncomment to have the dll named other than the default 'gdal.dll'
	set (GDAL_LIB_OUTPUT_NAME gdal_w${BITAGE} CACHE STRING "" FORCE)

	# GMT install ships no GMTConfig.cmake — only FindGMT.cmake fallback works.
	# FindGMT requires GMT_LIBRARY, PSL_LIBRARY, GMT_INCLUDE_DIR.
	set(GMT_DIR         C:/progs_cygw/GMTdev/gmt5/compileds/gmt6/VC14_${BITAGE} CACHE PATH     "" FORCE)
	set(GMT_ROOT        C:/progs_cygw/GMTdev/gmt5/compileds/gmt6/VC14_${BITAGE} CACHE PATH     "" FORCE)
	set(GMT_INCLUDE_DIR C:/progs_cygw/GMTdev/gmt5/compileds/gmt6/VC14_${BITAGE}/include/gmt   CACHE PATH     "" FORCE)
	set(GMT_LIBRARY     C:/progs_cygw/GMTdev/gmt5/compileds/gmt6/VC14_${BITAGE}/lib/gmt.lib   CACHE FILEPATH "" FORCE)
	set(PSL_LIBRARY     C:/progs_cygw/GMTdev/gmt5/compileds/gmt6/VC14_${BITAGE}/lib/postscriptlight.lib CACHE FILEPATH "" FORCE)

	# set location of gdal (can be root directory, path to header file or path to gdal-config):
	# GDAL_DIR must point at the directory containing GDALConfig.cmake (CONFIG mode lookup)
	set(GDAL_DIR         C:/programs/compa_libs/gdal_GIT/compileds/VC14_${BITAGE}/lib/cmake/gdal CACHE PATH   "" FORCE)
	set(GDAL_ROOT        C:/programs/compa_libs/gdal_GIT/compileds/VC14_${BITAGE} CACHE PATH   "" FORCE)
	set(GDAL_INCLUDE_DIR C:/programs/compa_libs/gdal_GIT/compileds/VC14_${BITAGE}/include CACHE PATH   "" FORCE)
	set(GDAL_LIBRARY     C:/programs/compa_libs/gdal_GIT/compileds/VC14_${BITAGE}/lib/gdal_w${BITAGE}.lib CACHE FILEPATH "" FORCE)
	set(GDAL_VERSION     3.13.0 CACHE STRING "" FORCE)


	# build-utils/FindFFTW.cmake expects FFTW_ROOT (root dir, not /lib) + FFTW_INCLUDE_DIRS (plural).
	# When FFTW_ROOT set, it auto-finds FFTW_FLOAT_LIB / FFTW_DOUBLE_LIB by glob in <root>/lib.
	set (FFTW_ROOT          C:/programs/compa_libs/fftw-3.3.8/compileds/VC14_${BITAGE} CACHE PATH     "" FORCE)
	set (FFTW_INCLUDE_DIRS  C:/programs/compa_libs/fftw-3.3.8/compileds/VC14_${BITAGE}/include CACHE PATH     "" FORCE)
	set (FFTW_FLOAT_LIB     C:/programs/compa_libs/fftw-3.3.8/compileds/VC14_${BITAGE}/lib/fftw3f.lib CACHE FILEPATH "" FORCE)
	set (FFTW_DOUBLE_LIB    C:/programs/compa_libs/fftw-3.3.8/compileds/VC14_${BITAGE}/lib/fftw3.lib  CACHE FILEPATH "" FORCE)

	set (PTHREADINC C:/programs/compa_libs/pthreads/compileds/VC14_${BITAGE}/include CACHE PATH "" FORCE)
	set (PTHREADLIB C:/programs/compa_libs/pthreads/compileds/VC14_${BITAGE}/lib/pthreadVC2.lib CACHE FILEPATH "" FORCE)

	# Prebuilt getopt for MSVC — utilities (mbclean, mbcopy, etc.) use POSIX getopt.
	set (GETOPT_INCLUDE_DIR C:/programs/compa_libs/getopt/src CACHE PATH "" FORCE)
	set (GETOPT_LIBRARY     C:/programs/compa_libs/getopt/compileds/VC14_${BITAGE}/lib/getopt.lib CACHE FILEPATH "" FORCE)

	# GMT was built with HAVE_GLIB_GTHREAD — its public headers #include <glib.h>.
	# Provide glib include dirs so consumers (mbaux/mb_readwritegrd.c) compile.
	set (GLIB_INCLUDE_DIR  C:/programs/compa_libs/glib-2.38.2/compileds/VC14_${BITAGE}/include/glib-2.0     CACHE PATH "" FORCE)
	set (GLIB_CONFIG_DIR   C:/programs/compa_libs/glib-2.38.2/compileds/VC14_${BITAGE}/lib/glib-2.0/include CACHE PATH "" FORCE)
	set (GLIB_LIBRARY      C:/programs/compa_libs/glib-2.38.2/compileds/VC14_${BITAGE}/lib/glib-2.0.lib     CACHE FILEPATH "" FORCE)

	set(buildOpenCV OFF CACHE BOOL "" FORCE)
	# TRN (Terrain-Relative Navigation). The sources are POSIX-oriented and need
	# shims for libgen/dirent/netdb/arpa/netinet/sys/*; those are supplied by the
	# port so the subsystem builds natively.
	set(buildTRN ON CACHE BOOL "" FORCE)

	set(NETCDF_INCLUDE_DIR "C:/programs/compa_libs/netcdf_vcpkg/compileds/VC14_${BITAGE}/include" CACHE STRING "" FORCE)
	set(NETCDF_LIBRARY "C:/programs/compa_libs/netcdf_vcpkg/compileds/VC14_${BITAGE}/lib/netcdf.lib" CACHE STRING "" FORCE)
	# build-utils/FindNetCDF.cmake expects CamelCase NetCDF_* (not NETCDF_*)
	set(NetCDF_INCLUDE_DIR "C:/programs/compa_libs/netcdf_vcpkg/compileds/VC14_${BITAGE}/include" CACHE PATH     "" FORCE)
	set(NetCDF_LIBRARY     "C:/programs/compa_libs/netcdf_vcpkg/compileds/VC14_${BITAGE}/lib/netcdf.lib" CACHE FILEPATH "" FORCE)

	set(PROJ_INCLUDE_DIR "C:/programs/compa_libs/proj5_GIT/compileds/VC14_${BITAGE}/include" CACHE STRING "" FORCE)
	set(PROJ_LIBRARY "C:/programs/compa_libs/proj5_GIT/compileds/VC14_${BITAGE}/lib/proj.lib" CACHE STRING "" FORCE)
	# MB-System uses build-utils/FindLibPROJ.cmake which expects LibPROJ_* names, not PROJ_*
	set(LibPROJ_INCLUDE_DIR     "C:/programs/compa_libs/proj5_GIT/compileds/VC14_${BITAGE}/include"   CACHE PATH     "" FORCE)
	set(LibPROJ_LIBRARY         "C:/programs/compa_libs/proj5_GIT/compileds/VC14_${BITAGE}/lib/proj.lib" CACHE FILEPATH "" FORCE)
	set(LibPROJ_LIBRARY_RELEASE "C:/programs/compa_libs/proj5_GIT/compileds/VC14_${BITAGE}/lib/proj.lib" CACHE FILEPATH "" FORCE)

endif (WIN32)
