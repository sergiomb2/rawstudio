AM_CFLAGS =\
	-Wall\
	-O4\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""@localedir@"\" \
	@PACKAGE_CFLAGS@ \
	-I$(top_srcdir)/librawstudio/ \
	-I$(top_srcdir)/

lib_LTLIBRARIES = meta_raf.la

libdir = @RAWSTUDIO_PLUGINS_LIBS_DIR@

meta_raf_la_LIBADD = @PACKAGE_LIBS@
meta_raf_la_LDFLAGS = -module -avoid-version
meta_raf_la_SOURCES =  raf-meta.c
