# ------------------------------------------------------------------------------
# autotools

SHELL = bash
ACLOCAL_AMFLAGS = -I build/m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

CLEANFILES = $(BUILT_SOURCES)
bin_PROGRAMS =
noinst_LIBRARIES =

INSTALL_EXEC_HOOKS =
UNINSTALL_EXEC_HOOKS =
.PHONY: $(INSTALL_EXEC_HOOKS) $(UNINSTALL_EXEC_HOOKS)
install-exec-hook: $(INSTALL_EXEC_HOOKS)
uninstall-hook: $(UNINSTALL_DATA_HOOKS) $(UNINSTALL_EXEC_HOOKS)

AM_CPPFLAGS = \
	-include $(top_builddir)/build/config.h \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	dbus-http

dbus_http_SOURCES = \
	src/dbus-http.c \
	src/http-server.h \
	src/http-server.c \
	src/json.h \
	src/json.c \
	src/dbus.h \
	src/dbus.c\
	src/log.c \
	src/log.h

dbus_http_CFLAGS = \
	$(AM_CFLAGS) \
	$(EXPAT_CFLAGS) \
	$(MICROHTTPD_CFLAGS) \
	$(SYSTEMD_CFLAGS)

dbus_http_LDADD = \
	$(EXPAT_LIBS) \
	$(MICROHTTPD_LIBS) \
	$(SYSTEMD_LIBS)

install-tree: all
	rm -rf $(abs_builddir)/install-tree
	$(MAKE) install DESTDIR=$(abs_builddir)/install-tree
	tree $(abs_builddir)/install-tree
.PHONY: install-tree
