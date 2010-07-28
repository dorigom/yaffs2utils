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

# change or override with your yaffs2 source
YAFFS2DIR	= /archive/projects/yaffs2

## cross-compiler?
CROSS 		= 
CC		= $(CROSS)gcc

SRCROOT		= src


all: src

src:
	CC="$(CC)" \
	YAFFS2DIR="$(shell readlink -f "${YAFFS2DIR}")" \
	$(MAKE) -C $(SRCROOT)

clean distclean:
	$(MAKE) -C $(SRCROOT) $@


.PHONY: all src clean distclean
