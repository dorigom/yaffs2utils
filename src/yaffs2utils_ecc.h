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

#ifndef _YAFFS2UTILS_ECC_H_
#define _YAFFS2UTILS_ECC_H_

#include <mtd/mtd-user.h>

static nand_ecclayout_t nand_oob_16 = {
        .eccbytes = 6,
        .eccpos = {0, 1, 2, 3, 6, 7},
        .oobfree = {{.offset = 8, .length = 8}},
};

static nand_ecclayout_t nand_oob_64 = {
        .eccbytes = 24,
        .eccpos = {40, 41, 42, 43, 44, 45, 46, 47,
                   48, 49, 50, 51, 52, 53, 54, 55,
                   56, 57, 58, 59, 60, 61, 62, 63},
        .oobfree = {{.offset = 2, .length = 38}},
};

static nand_ecclayout_t nand_oob_user = {0};

#endif
