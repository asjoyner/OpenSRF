# Copyright (C) 2008 Equinox Software, Inc.
# Kevin Beswick <kevinbeswick00@gmail.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.


LDADD = -lreadline -lxml2 -lncurses $(DEF_LDLIBS) 
AM_CFLAGS = $(DEF_CFLAGS) -DEXEC_DEFAULT -L@top_builddir@/src/libopensrf
AM_LDFLAGS = $(DEF_LDFLAGS)

DISTCLEANFILES = Makefile.in Makefile

bin_PROGRAMS = srfsh
srfsh_SOURCES = srfsh.c
