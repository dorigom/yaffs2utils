/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */


#ifndef __YPORTENV_H__
#define __YPORTENV_H__

/* Definition of types */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned u32;

#include <stdio.h>

#include "yaffs_hweight.h"

#define hweight8(x)	yaffs_hweight8(x)
#define hweight32(x)	yaffs_hweight32(x)

#define yaffs_trace(msk, fmt, ...) do { \
        if (yaffs_trace_mask & ((msk) | YAFFS_TRACE_ALWAYS)) \
                printf("yaffs: " fmt "\n", ##__VA_ARGS__); \
} while(0)

#define YCHAR char

#endif
