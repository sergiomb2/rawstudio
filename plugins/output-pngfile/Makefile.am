AM_CFLAGS =\
	-Wall\
	-O4\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""@localedir@"\" \
	@PACKAGE_CFLAGS@ \
	-I$(top_srcdir)/librawstudio/ \
	-I$(top_srcdir)/

AM_CXXFLAGS = $(AM_CFLAGS)

lib_LTLIBRARIES = output_pngfile.la

libdir = @RAWSTUDIO_PLUGINS_LIBS_DIR@

output_pngfile_la_LIBADD = @PACKAGE_LIBS@
output_pngfile_la_LDFLAGS = -module -avoid-version
output_pngfile_la_SOURCES = output-pngfile.c
