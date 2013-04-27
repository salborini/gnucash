SUBDIRS = .

pkglib_LTLIBRARIES=libgncmod-price-csv.la

libgncmod_price_csv_la_SOURCES = \
  gncmod-price-csv.c \
  gnc-plugin-price-csv.c \
  gnc-price-import.c \
  gnc-price-export.c \
  gnc-price-model.c

noinst_HEADERS = \
  gnc-plugin-price-csv.h \
  gnc-price-import.h \
  gnc-price-export.h \
  gnc-price-model.h

libgncmod_price_csv_la_LDFLAGS = -avoid-version

libgncmod_price_csv_la_LIBADD = \
  ${top_builddir}/src/gnome-utils/libgncmod-gnome-utils.la \
  ${top_builddir}/src/app-utils/libgncmod-app-utils.la \
  ${top_builddir}/src/engine/libgncmod-engine.la \
  ${top_builddir}/src/core-utils/libgnc-core-utils.la \
  ${top_builddir}/src/gnc-module/libgnc-module.la \
  ${top_builddir}/lib/stf/libgnc-stf.la \
  ${top_builddir}/lib/libc/libc-missing.la \
  ${GOFFICE_LIBS} \
  ${GLIB_LIBS}

AM_CPPFLAGS = \
  -I${top_srcdir}/src/core-utils \
  -I${top_srcdir}/src/engine \
  -I${top_srcdir}/src/gnc-module \
  -I${top_srcdir}/src/app-utils \
  -I${top_srcdir}/src/gnome-utils \
  -I${top_srcdir}/src/libqof/qof \
  -I${top_srcdir}/lib/libc \
  -I${top_srcdir}/lib \
  -I${top_srcdir}/lib/libc \
  ${GCONF_CFLAGS} \
  $(GOFFICE_CFLAGS)

uidir = $(GNC_UI_DIR)
ui_DATA = \
	gnc-plugin-price-csv-ui.xml

gtkbuilderdir = ${GNC_GTKBUILDER_DIR}
gtkbuilder_DATA = \
	gnc-price-import.glade

EXTRA_DIST = $(ui_DATA) $(gtkbuilder_DATA)

INCLUDES = -DG_LOG_DOMAIN=\"gnc.import.price-csv\"
