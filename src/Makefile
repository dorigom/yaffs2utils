#
# Makefile for yaffs2utils.
#
# yaffs2utils: Utilities to make/extract a YAFFS2/YAFFS1 image
# Copyright (C) 2010-2011 Luen-Yung Lin <penguin.lin@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

OSTYPE		= $(shell uname -s|awk '{print tolower($$0)}')

CROSS		=
CC		= $(CROSS)gcc

CFLAGS		=  -I. -I./yaffs2 -O2 -Wall
CFLAGS		+= -Wshadow -Wpointer-arith -Wwrite-strings -Wstrict-prototypes \
		   -Wmissing-declarations -Wmissing-prototypes -Wredundant-decls \
		   -Wnested-externs -Winline

CFLAGS		+= -D_HAVE_MMAP

LDFLAGS		+= -lm

YAFFS2SRCS	= yaffs2/yaffs_hweight.c yaffs2/yaffs_ecc.c \
		  yaffs2/yaffs_packedtags1.c yaffs2/yaffs_packedtags2.c
YAFFS2OBJS	= $(YAFFS2SRCS:.c=.o)

LIBSRCS		= yaffs2utils_io.c yaffs2utils_endian.c yaffs2utils_progress.c
LIBOBJS		= $(LIBSRCS:.c=.o)

MKYAFFS2SRCS	= mkyaffs2.c
MKYAFFS2OBJS	= $(MKYAFFS2SRCS:.c=.o)

UNYAFFS2SRCS	= unyaffs2.c
UNYAFFS2OBJS	= $(UNYAFFS2SRCS:.c=.o)

UNSPARE2SRCS	= unspare2.c
UNSPARE2OBJS	= $(UNSPARE2SRCS:.c=.o)


all: mkyaffs2 unyaffs2 unspare2

mkyaffs2: $(YAFFS2OBJS) $(LIBOBJS) $(MKYAFFS2OBJS)
	$(CC) $(LDFLAGS) -o $@ $(YAFFS2OBJS) $(LIBOBJS) $(MKYAFFS2OBJS)

unyaffs2: $(YAFFS2OBJS) $(LIBOBJS) $(UNYAFFS2OBJS)
	$(CC) $(LDFLAGS) -o $@ $(YAFFS2OBJS) $(LIBOBJS) $(UNYAFFS2OBJS)

unspare2: $(YAFFS2OBJS) $(LIBOBJS) $(UNSPARE2OBJS)
	$(CC) $(LDFLAGS) -o $@ $(YAFFS2OBJS) $(LIBOBJS) $(UNSPARE2OBJS)

clean:
	rm -rf $(YAFFS2OBJS) $(LIBOBJS) \
	       $(MKYAFFS2OBJS) $(UNYAFFS2OBJS) $(UNSPARE2OBJS)

distclean: clean
	rm -rf mkyaffs2 unyaffs2 unspare2

.PHONY: all clean distclean mkyaffs2 unyaffs2 unspare2
