##
## Copyright (C) by Argonne National Laboratory
##     See COPYRIGHT in top-level directory
##

include $(top_srcdir)/Makefile_single.mtest

EXTRA_DIST = testlist

## for all programs that are just built from the single corresponding source
## file, we don't need per-target _SOURCES rules, automake will infer them
## correctly
noinst_PROGRAMS = \
    session       \
    session_mult_init \
    session_mult_init_with_world \
    session_re_init \
    session_mod_group \
    session_psets \
    session_self \
    session_self_with_world \
    session_bsend

session_mult_init_SOURCES = session.c
session_mult_init_CPPFLAGS = -DMULT_INIT $(AM_CPPFLAGS)
session_mult_init_with_world_SOURCES = session.c
session_mult_init_with_world_CPPFLAGS = -DMULT_INIT -DWITH_WORLD $(AM_CPPFLAGS)
session_re_init_SOURCES = session.c
session_re_init_CPPFLAGS = -DRE_INIT $(AM_CPPFLAGS)
session_self_with_world_SOURCES = session_self.c
session_self_with_world_CPPFLAGS = -DWITH_WORLD $(AM_CPPFLAGS)
session_mod_group_SOURCES = session.c
session_mod_group_CPPFLAGS = -DMOD_GROUP $(AM_CPPFLAGS)
