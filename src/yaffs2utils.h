/* 
 * yaffs2utils: Utilities to make/extract a YAFFS2/YAFFS1 image.
 * Copyright (C) 2010-2011 Luen-Yung Lin <penguin.lin@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _YAFFS2UTILS_H
#define _YAFFS2UTILS_H

#define YAFFS2UTILS_VERSION	"0.2.3"

#define DEFAULT_CHUNKSIZE	2048
#define DEFAULT_MAX_OBJID	0x3ffff

#if defined(YAFFS_UNUSED_OBJECT_ID)
#define YAFFS_MAX_OBJID		YAFFS_UNUSED_OBJECT_ID
#elif defined(YAFFS_MAX_OBJECT_ID)
#define YAFFS_MAX_OBJID		YAFFS_MAX_OBJECT_ID
#else
#define YAFFS_MAX_OBJID		DEFAULT_MAX_OBJID
#endif

#endif
