################################################################################
#
# Makefile for yaffs2utils.
#
# yaffs2utils: Utilities to make/extract a YAFFS2/YAFFS1 image
# Copyright (C) 2010 Luen-Yung Lin <penguin.lin@gmail.com>
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
################################################################################

CFLAGS		=  -I. -O2 -Wall -DCONFIG_YAFFS_UTIL
CFLAGS		+= -Wshadow -Wpointer-arith -Wwrite-strings -Wstrict-prototypes -Wmissing-declarations
CFLAGS		+= -Wmissing-prototypes -Wredundant-decls -Wnested-externs -Winline

ifneq ($(YAFFS2DIR), )
	CFLAGS	+= -I$(YAFFS2DIR)
endif

CFLAGS		+= -D__HAVE_MMAP

LIBSRCS		= yaffs2utils_io.c yaffs2utils_endian.c
LIBOBJS		= $(LIBSRCS:.c=.o)

COMMONLINKS	= yaffs_ecc.c yaffs_packedtags1.c yaffs_packedtags2.c yaffs_tagsvalidity.c
COMMONOBJS	= $(COMMONLINKS:.c=.o)

MKYAFFS2SRCS	= mkyaffs2.c
MKYAFFS2OBJS	= $(MKYAFFS2SRCS:.c=.o)

UNYAFFS2SRCS	= unyaffs2.c
UNYAFFS2OBJS	= $(UNYAFFS2SRCS:.c=.o)


all: mkyaffs2 unyaffs2

$(COMMONLINKS):
	ln -sf $(YAFFS2DIR)/$@ $@

$(COMMONOBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

$(LIBOBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

mkyaffs2: $(COMMONOBJS) $(LIBOBJS) $(MKYAFFS2OBJS)
	$(CC) -o $@ $(COMMONOBJS) $(LIBOBJS) $(MKYAFFS2OBJS)

unyaffs2: $(COMMONOBJS) $(LIBOBJS) $(UNYAFFS2OBJS)
	$(CC) -o $@ $(COMMONOBJS) $(LIBOBJS) $(UNYAFFS2OBJS)

clean:
	rm -rf $(COMMONLINKS) $(COMMONOBJS) $(LIBOBJS) \
	       $(MKYAFFS2OBJS) $(UNYAFFS2OBJS)

distclean: clean
	rm -rf mkyaffs2 unyaffs2

.PHONY: all clean distclean mkyaffs2 unyaffs2
