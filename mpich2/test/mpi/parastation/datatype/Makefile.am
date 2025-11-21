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

noinst_HEADERS = mixed_datatype_test.h

noinst_PROGRAMS =                    \
	mixed_derived_to_basic_bcast       \
	mixed_derived_to_basic_gather      \
	mixed_derived_to_basic_gatherv     \
	mixed_derived_to_basic_allgather   \
	mixed_derived_to_basic_allgatherv  \
	mixed_derived_to_basic_scatter     \
	mixed_derived_to_basic_scatterv    \
	mixed_derived_to_basic_alltoall    \
	mixed_derived_to_basic_alltoallv   \
	mixed_derived_to_contig_bcast      \
	mixed_derived_to_contig_gather     \
	mixed_derived_to_contig_gatherv    \
	mixed_derived_to_contig_allgather  \
	mixed_derived_to_contig_allgatherv \
	mixed_derived_to_contig_scatter    \
	mixed_derived_to_contig_scatterv   \
	mixed_derived_to_contig_alltoall   \
	mixed_derived_to_contig_alltoallv  \
	mixed_basic_to_derived_bcast       \
	mixed_basic_to_derived_gather      \
	mixed_basic_to_derived_gatherv     \
	mixed_basic_to_derived_allgather   \
	mixed_basic_to_derived_allgatherv  \
	mixed_basic_to_derived_scatter     \
	mixed_basic_to_derived_scatterv    \
	mixed_basic_to_derived_alltoall    \
	mixed_basic_to_derived_alltoallv   \
	mixed_contig_to_derived_bcast      \
	mixed_contig_to_derived_gather     \
	mixed_contig_to_derived_gatherv    \
	mixed_contig_to_derived_allgather  \
	mixed_contig_to_derived_allgatherv \
	mixed_contig_to_derived_scatter    \
	mixed_contig_to_derived_scatterv   \
	mixed_contig_to_derived_alltoall   \
	mixed_contig_to_derived_alltoallv

# Tests generated from mixed_derived_to_basic.c
mixed_derived_to_basic_bcast_SOURCES = mixed_derived_to_basic.c
mixed_derived_to_basic_bcast_CPPFLAGS = -DBCAST $(AM_CPPFLAGS)
mixed_derived_to_basic_gather_SOURCES = mixed_derived_to_basic.c
mixed_derived_to_basic_gather_CPPFLAGS = -DGATHER $(AM_CPPFLAGS)
mixed_derived_to_basic_gatherv_SOURCES = mixed_derived_to_basic.c
mixed_derived_to_basic_gatherv_CPPFLAGS = -DGATHERV $(AM_CPPFLAGS)
mixed_derived_to_basic_allgather_SOURCES = mixed_derived_to_basic.c
mixed_derived_to_basic_allgather_CPPFLAGS = -DALLGATHER $(AM_CPPFLAGS)
mixed_derived_to_basic_allgatherv_SOURCES = mixed_derived_to_basic.c
mixed_derived_to_basic_allgatherv_CPPFLAGS = -DALLGATHERV $(AM_CPPFLAGS)
mixed_derived_to_basic_scatter_SOURCES = mixed_derived_to_basic.c
mixed_derived_to_basic_scatter_CPPFLAGS = -DSCATTER $(AM_CPPFLAGS)
mixed_derived_to_basic_scatterv_SOURCES = mixed_derived_to_basic.c
mixed_derived_to_basic_scatterv_CPPFLAGS = -DSCATTERV $(AM_CPPFLAGS)
mixed_derived_to_basic_alltoall_SOURCES = mixed_derived_to_basic.c
mixed_derived_to_basic_alltoall_CPPFLAGS = -DALLTOALL $(AM_CPPFLAGS)
mixed_derived_to_basic_alltoallv_SOURCES = mixed_derived_to_basic.c
mixed_derived_to_basic_alltoallv_CPPFLAGS = -DALLTOALLV $(AM_CPPFLAGS)

# Tests generated from mixed_derived_to_contig.c
mixed_derived_to_contig_bcast_SOURCES = mixed_derived_to_contig.c
mixed_derived_to_contig_bcast_CPPFLAGS = -DBCAST $(AM_CPPFLAGS)
mixed_derived_to_contig_gather_SOURCES = mixed_derived_to_contig.c
mixed_derived_to_contig_gather_CPPFLAGS = -DGATHER $(AM_CPPFLAGS)
mixed_derived_to_contig_gatherv_SOURCES = mixed_derived_to_contig.c
mixed_derived_to_contig_gatherv_CPPFLAGS = -DGATHERV $(AM_CPPFLAGS)
mixed_derived_to_contig_allgather_SOURCES = mixed_derived_to_contig.c
mixed_derived_to_contig_allgather_CPPFLAGS = -DALLGATHER $(AM_CPPFLAGS)
mixed_derived_to_contig_allgatherv_SOURCES = mixed_derived_to_contig.c
mixed_derived_to_contig_allgatherv_CPPFLAGS = -DALLGATHERV $(AM_CPPFLAGS)
mixed_derived_to_contig_scatter_SOURCES = mixed_derived_to_contig.c
mixed_derived_to_contig_scatter_CPPFLAGS = -DSCATTER $(AM_CPPFLAGS)
mixed_derived_to_contig_scatterv_SOURCES = mixed_derived_to_contig.c
mixed_derived_to_contig_scatterv_CPPFLAGS = -DSCATTERV $(AM_CPPFLAGS)
mixed_derived_to_contig_alltoall_SOURCES = mixed_derived_to_contig.c
mixed_derived_to_contig_alltoall_CPPFLAGS = -DALLTOALL $(AM_CPPFLAGS)
mixed_derived_to_contig_alltoallv_SOURCES = mixed_derived_to_contig.c
mixed_derived_to_contig_alltoallv_CPPFLAGS = -DALLTOALLV $(AM_CPPFLAGS)

# Tests generated from mixed_basic_to_derived.c
mixed_basic_to_derived_bcast_SOURCES = mixed_basic_to_derived.c
mixed_basic_to_derived_bcast_CPPFLAGS = -DBCAST $(AM_CPPFLAGS)
mixed_basic_to_derived_gather_SOURCES = mixed_basic_to_derived.c
mixed_basic_to_derived_gather_CPPFLAGS = -DGATHER $(AM_CPPFLAGS)
mixed_basic_to_derived_gatherv_SOURCES = mixed_basic_to_derived.c
mixed_basic_to_derived_gatherv_CPPFLAGS = -DGATHERV $(AM_CPPFLAGS)
mixed_basic_to_derived_allgather_SOURCES = mixed_basic_to_derived.c
mixed_basic_to_derived_allgather_CPPFLAGS = -DALLGATHER $(AM_CPPFLAGS)
mixed_basic_to_derived_allgatherv_SOURCES = mixed_basic_to_derived.c
mixed_basic_to_derived_allgatherv_CPPFLAGS = -DALLGATHERV $(AM_CPPFLAGS)
mixed_basic_to_derived_scatter_SOURCES = mixed_basic_to_derived.c
mixed_basic_to_derived_scatter_CPPFLAGS = -DSCATTER $(AM_CPPFLAGS)
mixed_basic_to_derived_scatterv_SOURCES = mixed_basic_to_derived.c
mixed_basic_to_derived_scatterv_CPPFLAGS = -DSCATTERV $(AM_CPPFLAGS)
mixed_basic_to_derived_alltoall_SOURCES = mixed_basic_to_derived.c
mixed_basic_to_derived_alltoall_CPPFLAGS = -DALLTOALL $(AM_CPPFLAGS)
mixed_basic_to_derived_alltoallv_SOURCES = mixed_basic_to_derived.c
mixed_basic_to_derived_alltoallv_CPPFLAGS = -DALLTOALLV $(AM_CPPFLAGS)

# Tests generated from mixed_contig_to_derived.c
mixed_contig_to_derived_bcast_SOURCES = mixed_contig_to_derived.c
mixed_contig_to_derived_bcast_CPPFLAGS = -DBCAST $(AM_CPPFLAGS)
mixed_contig_to_derived_gather_SOURCES = mixed_contig_to_derived.c
mixed_contig_to_derived_gather_CPPFLAGS = -DGATHER $(AM_CPPFLAGS)
mixed_contig_to_derived_gatherv_SOURCES = mixed_contig_to_derived.c
mixed_contig_to_derived_gatherv_CPPFLAGS = -DGATHERV $(AM_CPPFLAGS)
mixed_contig_to_derived_allgather_SOURCES = mixed_contig_to_derived.c
mixed_contig_to_derived_allgather_CPPFLAGS = -DALLGATHER $(AM_CPPFLAGS)
mixed_contig_to_derived_allgatherv_SOURCES = mixed_contig_to_derived.c
mixed_contig_to_derived_allgatherv_CPPFLAGS = -DALLGATHERV $(AM_CPPFLAGS)
mixed_contig_to_derived_scatter_SOURCES = mixed_contig_to_derived.c
mixed_contig_to_derived_scatter_CPPFLAGS = -DSCATTER $(AM_CPPFLAGS)
mixed_contig_to_derived_scatterv_SOURCES = mixed_contig_to_derived.c
mixed_contig_to_derived_scatterv_CPPFLAGS = -DSCATTERV $(AM_CPPFLAGS)
mixed_contig_to_derived_alltoall_SOURCES = mixed_contig_to_derived.c
mixed_contig_to_derived_alltoall_CPPFLAGS = -DALLTOALL $(AM_CPPFLAGS)
mixed_contig_to_derived_alltoallv_SOURCES = mixed_contig_to_derived.c
mixed_contig_to_derived_alltoallv_CPPFLAGS = -DALLTOALLV $(AM_CPPFLAGS)
