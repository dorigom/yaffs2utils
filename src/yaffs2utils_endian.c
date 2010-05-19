/*
 * yaffs2utils: Utilities to make/extract a YAFFS2/YAFFS1 image.
 * Copyright (C) 2010 Luen-Yung Lin <penguin.lin@gmail.com>
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
 
#include <stdio.h>
#include <asm/byteorder.h>

#include "yaffs_packedtags1.h"
#include "yaffs_packedtags2.h"
#include "yaffs_tagsvalidity.h"

#include "yaffs2utils_endian.h"

/*-------------------------------------------------------------------------*/

#define ENDIAN_SWAP32(x)	((((x) & 0x000000FF) << 24) | \
				(((x) & 0x0000FF00) << 8) | \
				(((x) & 0x00FF0000) >> 8) | \
				(((x) & 0xFF000000) >> 24))
#define ENDIAN_SWAP16(x)	((((x) & 0x00FF) << 8) | \
				(((x) & 0xFF00) >> 8))

/*-------------------------------------------------------------------------*/

void 
object_header_endian_transform (yaffs_ObjectHeader *oh)
{
	oh->type = ENDIAN_SWAP32(oh->type); // GCC makes enums 32 bits.
	oh->parentObjectId = ENDIAN_SWAP32(oh->parentObjectId); // int
	// __u16 - Not used, but done for completeness.
	oh->sum__NoLongerUsed = ENDIAN_SWAP16(oh->sum__NoLongerUsed);
	oh->yst_mode = ENDIAN_SWAP32(oh->yst_mode);

#ifdef CONFIG_YAFFS_WINCE 
	/* 
	 * WinCE doesn't implement this, but we need to just in case. 
	 * In fact, WinCE would be *THE* place where this would be an issue!
	 */
	oh->notForWinCE[0] = ENDIAN_SWAP32(oh->notForWinCE[0]);
	oh->notForWinCE[1] = ENDIAN_SWAP32(oh->notForWinCE[1]);
	oh->notForWinCE[2] = ENDIAN_SWAP32(oh->notForWinCE[2]);
	oh->notForWinCE[3] = ENDIAN_SWAP32(oh->notForWinCE[3]);
	oh->notForWinCE[4] = ENDIAN_SWAP32(oh->notForWinCE[4]);
#else
	// Regular POSIX.
	oh->yst_uid = ENDIAN_SWAP32(oh->yst_uid);
	oh->yst_gid = ENDIAN_SWAP32(oh->yst_gid);
	oh->yst_atime = ENDIAN_SWAP32(oh->yst_atime);
	oh->yst_mtime = ENDIAN_SWAP32(oh->yst_mtime);
	oh->yst_ctime = ENDIAN_SWAP32(oh->yst_ctime);
#endif

	oh->fileSize = ENDIAN_SWAP32(oh->fileSize);
	oh->equivalentObjectId = ENDIAN_SWAP32(oh->equivalentObjectId);
	oh->yst_rdev = ENDIAN_SWAP32(oh->yst_rdev);

#ifdef CONFIG_YAFFS_WINCE 
	oh->win_ctime[0] = ENDIAN_SWAP32(oh->win_ctime[0]);
	oh->win_ctime[1] = ENDIAN_SWAP32(oh->win_ctime[1]);
	oh->win_atime[0] = ENDIAN_SWAP32(oh->win_atime[0]);
	oh->win_atime[1] = ENDIAN_SWAP32(oh->win_atime[1]);
	oh->win_mtime[0] = ENDIAN_SWAP32(oh->win_mtime[0]);
	oh->win_mtime[1] = ENDIAN_SWAP32(oh->win_mtime[1]);
#else
	oh->roomToGrow[0] = ENDIAN_SWAP32(oh->roomToGrow[0]);
	oh->roomToGrow[1] = ENDIAN_SWAP32(oh->roomToGrow[1]);
	oh->roomToGrow[2] = ENDIAN_SWAP32(oh->roomToGrow[2]);
	oh->roomToGrow[3] = ENDIAN_SWAP32(oh->roomToGrow[3]);
	oh->roomToGrow[4] = ENDIAN_SWAP32(oh->roomToGrow[4]);
	oh->roomToGrow[5] = ENDIAN_SWAP32(oh->roomToGrow[5]);
#endif

	oh->inbandShadowsObject = ENDIAN_SWAP32(oh->inbandShadowsObject);
	oh->inbandIsShrink = ENDIAN_SWAP32(oh->inbandIsShrink);
	oh->reservedSpace[0] = ENDIAN_SWAP32(oh->reservedSpace[0]);
	oh->reservedSpace[1] = ENDIAN_SWAP32(oh->reservedSpace[1]);
	oh->shadowsObject = ENDIAN_SWAP32(oh->shadowsObject);
	oh->isShrink = ENDIAN_SWAP32(oh->isShrink);
}

/*-------------------------------------------------------------------------*/

void
packedtags1_endian_transform (yaffs_PackedTags1 *pt, unsigned reverse)
{
	yaffs_TagsUnion *tags = (yaffs_TagsUnion *)pt; // Work in bytes.
	yaffs_TagsUnion temp;
	unsigned char *pb = tags->asBytes;
	unsigned char *tb = temp.asBytes;

	memset(&temp, 0, sizeof(yaffs_TagsUnion));

	/* I really hate these */
	if (!reverse) {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		tb[0] = ((pb[2] & 0x0F) << 4) |
			((pb[1] & 0xF0) >> 4);
		tb[1] = ((pb[1] & 0x0F) << 4) |
			((pb[0] & 0xF0) >> 4);
		tb[2] = ((pb[0] & 0x0F) << 4) |
			((pb[2] & 0x30) >> 2) |
			((pb[3] & 0xC0) >> 6);
		tb[3] = ((pb[3] & 0x3F) << 2) |
			((pb[2] & 0xC0) >> 6);
		tb[4] = ((pb[6] & 0x03) << 6) |
			((pb[5] & 0xFC) >> 2);
		tb[5] = ((pb[5] & 0x03) << 6) |
			((pb[4] & 0xFC) >> 2);
		tb[6] = ((pb[4] & 0x03) << 6) |
			(pb[7] & 0x3F);
		tb[7] = (pb[6] & 0xFC) |
			((pb[7] & 0x40) >> 5) |
			((pb[7] & 0x80) >> 7);
#elif defined(__BIG_ENDIAN_BITFIELD)
		tb[0] = ((pb[2] & 0xF0) >> 4) |
			((pb[1] & 0x0F) << 4);
		tb[1] = ((pb[1] & 0xF0) >> 4) |
			((pb[0] & 0x0F) << 4);
		tb[2] = ((pb[0] & 0xF0) >> 4) |
			((pb[2] & 0x0C) << 2) |
			((pb[3] & 0x03) << 6);
		tb[3] = ((pb[3] & 0xFC) >> 2) |
			((pb[2] & 0x03) << 6);
		tb[4] = ((pb[6] & 0xC0) >> 6) |
			((pb[5] & 0x3F) << 2);
		tb[5] = ((pb[5] & 0xC0) >> 6) |
			((pb[4] & 0x3F) << 2);
		tb[6] = ((pb[4] & 0xC0) >> 6) |
			(pb[7] & 0xFC);
		tb[7] = (pb[6] & 0x3F) |
			((pb[7] & 0x02) << 5) |
			((pb[7] & 0x01) << 7);
#endif
	}
	else {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		tb[0] = ((pb[2] & 0xF0) >> 4) |
			((pb[1] & 0x0F) << 4);
		tb[1] = ((pb[1] & 0xF0) >> 4) |
			((pb[0] & 0x0F) << 4);
		tb[2] = ((pb[0] & 0xF0) >> 4) |
			((pb[2] & 0x0C) << 2) |
			((pb[3] & 0x03) << 6);
		tb[3] = ((pb[3] & 0xFC) >> 2) |
			((pb[2] & 0x03) << 6);
		tb[4] = ((pb[6] & 0xC0) >> 6) |
			((pb[5] & 0x3F) << 2);
		tb[5] = ((pb[5] & 0xC0) >> 6) |
			((pb[4] & 0x3F) << 2);
		tb[6] = ((pb[4] & 0xC0) >> 6) |
			(pb[7] & 0xFC);
		tb[7] = (pb[6] & 0x3F) |
			((pb[7] & 0x02) << 5) |
			((pb[7] & 0x01) << 7);
#elif defined(__BIG_ENDIAN_BITFIELD)
		tb[0] = ((pb[2] & 0x0F) << 4) |
			((pb[1] & 0xF0) >> 4);
		tb[1] = ((pb[1] & 0x0F) << 4) |
			((pb[0] & 0xF0) >> 4);
		tb[2] = ((pb[0] & 0x0F) << 4) |
			((pb[2] & 0x30) >> 2) |
			((pb[3] & 0xC0) >> 6);
		tb[3] = ((pb[3] & 0x3F) << 2) |
			((pb[2] & 0xC0) >> 6);
		tb[4] = ((pb[6] & 0x03) << 6) |
			((pb[5] & 0xFC) >> 2);
		tb[5] = ((pb[5] & 0x03) << 6) |
			((pb[4] & 0xFC) >> 2);
		tb[6] = ((pb[4] & 0x03) << 6) |
			(pb[7] & 0x3F);
		tb[7] = (pb[6] & 0xFC) |
			((pb[7] & 0x40) >> 5) |
			((pb[7] & 0x80) >> 7);
#endif
	}

	// Now copy it back.
	pb[0] = tb[0];
	pb[1] = tb[1];
	pb[2] = tb[2];
	pb[3] = tb[3];
	pb[4] = tb[4];
	pb[5] = tb[5];
	pb[6] = tb[6];
	pb[7] = tb[7];
}

/*-------------------------------------------------------------------------*/

void
packedtags2_tagspart_endian_transform (yaffs_PackedTags2 *t)
{
	yaffs_PackedTags2TagsPart *tp = &t->t;

	tp->sequenceNumber = ENDIAN_SWAP32(tp->sequenceNumber);
	tp->objectId = ENDIAN_SWAP32(tp->objectId);
	tp->chunkId = ENDIAN_SWAP32(tp->chunkId);
	tp->byteCount = ENDIAN_SWAP32(tp->byteCount);	
}

void 
packedtags2_eccother_endian_transform (yaffs_PackedTags2 *t)
{
	yaffs_ECCOther *e = &t->ecc;

	e->lineParity = ENDIAN_SWAP32(e->lineParity);
	e->lineParityPrime = ENDIAN_SWAP32(e->lineParityPrime);
}
