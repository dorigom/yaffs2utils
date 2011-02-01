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

#define YAFFS2UTILS_VERSION	"0.1.9"

#define DEFAULT_CHUNK_SIZE	2048
#define DEFAULT_OBJECT_NUMBERS	65536

#if defined(YAFFS_MAX_OBJECT_ID)
#define MAX_OBJECT_NUMBERS	YAFFS_MAX_OBJECT_ID
#elif defined(YAFFS_UNUSED_OBJECT_ID)
#define MAX_OBJECT_NUMBERS	YAFFS_UNUSED_OBJECT_ID
#else
#define MAX_OBJECT_NUMBERS	DEFAULT_OBJECT_NUMBERS
#endif

/* from Linux Kernel */
static u_int32_t nand_oobfree_16[8][2] = {{8, 8}};
static u_int32_t nand_oobfree_64[8][2] = {{2, 38}};

#endif
