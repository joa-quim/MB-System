lib_LTLIBRARIES = mbsystem.la

if BUILD_MBTRN
  MBTRNINCDIR = -I${top_srcdir}/src/mbtrn
  MBTRNLIB = ${top_builddir}/src/mbtrn/libmbtrn.la
endif

include_HEADERS =

AM_CPPFLAGS = -I${top_srcdir}/src/mbio \
        -I${top_srcdir}/src/mbaux \
        $(MBTRNINCDIR) \
        ${libgmt_CPPFLAGS} \
        ${libgdal_CPPFLAGS} \
        ${libnetcdf_CPPFLAGS}
AM_CFLAGS = ${libgmt_CFLAGS} ${libgdal_CFLAGS} ${libnetcdf_CFLAGS}

mbsystem_la_LDFLAGS = -module -avoid-version -shared
mbsystem_la_SOURCES = mbswath.c mbcontour.c mbgrdtiff.c
mbsystem_la_LIBADD = ${top_builddir}/src/mbio/libmbio.la \
        ${top_builddir}/src/mbaux/libmbaux.la \
        ${libgmt_LIBS} ${libgdal_LIBS} ${libnetcdf_LIBS} \
        $(MBTRNLIB)
