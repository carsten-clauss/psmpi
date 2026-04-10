#
# ParaStation
#
# Copyright (C) 2026 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.

include $(top_srcdir)/Makefile_single.mtest

EXTRA_DIST = testlist

noinst_libdir = $(top_srcdir)/parastation/pscoll/
noinst_lib_LTLIBRARIES = libpscoll_ucc.la libpscoll_gloo.la libpscoll_mpi.la

libpscoll_ucc_la_CPPFLAGS = -DMAKE_COLLOPS_PLUGIN
libpscoll_ucc_la_SOURCES = pscoll_ucc.c
libpscoll_ucc_la_LDFLAGS = -module -avoid-version -shared
libpscoll_ucc_la_LIBADD = -lucc

pscoll_ucc_plg_CPPFLAGS = $(AM_CPPFLAGS) -DUSE_COLLOPS_PLUGIN
pscoll_ucc_plg_SOURCES = pscoll_ucc.c
pscoll_ucc_plg_LDADD = libpscoll_ucc.la -lucc

pscoll_ucc_SOURCES = pscoll_ucc.c
pscoll_ucc_LDADD = $(LDADD) -lucc

libpscoll_gloo_la_CPPFLAGS = -DMAKE_COLLOPS_PLUGIN
libpscoll_gloo_la_SOURCES = pscoll_gloo.c
libpscoll_gloo_la_LDFLAGS = -module -avoid-version -shared
libpscoll_gloo_la_LIBADD = -lgloo

pscoll_gloo_plg_CPPFLAGS = $(AM_CPPFLAGS) -DUSE_COLLOPS_PLUGIN
pscoll_gloo_plg_SOURCES = pscoll_gloo.c gloo_wrapper.cc gloo_wrapper.h
pscoll_gloo_plg_LDADD = libpscoll_gloo.la -lgloo -lpthread

pscoll_gloo_SOURCES = pscoll_gloo.c gloo_wrapper.cc gloo_wrapper.h
pscoll_gloo_LDADD = $(LDADD) -lgloo -lpthread

libpscoll_mpi_la_CPPFLAGS = -DMAKE_COLLOPS_PLUGIN
libpscoll_mpi_la_SOURCES = pscoll_mpi.c
libpscoll_mpi_la_LDFLAGS = -module -avoid-version -shared

pscoll_mpi_plg_CPPFLAGS = $(AM_CPPFLAGS) -DUSE_COLLOPS_PLUGIN
pscoll_mpi_plg_SOURCES = pscoll_mpi.c
pscoll_mpi_plg_LDADD = libpscoll_mpi.la

pscoll_mpi_SOURCES = pscoll_mpi.c

noinst_PROGRAMS =   \
    pscoll_ucc      \
    pscoll_ucc_plg  \
    pscoll_gloo     \
    pscoll_gloo_plg \
    pscoll_mpi      \
    pscoll_mpi_plg
