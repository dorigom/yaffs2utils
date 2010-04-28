/*
 * yaffs2utils: Utilities to make/extract a YAFFS2/YAFFS1 image
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
/*
 * mkyaffs2.c 
 *
 * Makes a YAFFS2 file system image that can be used to load up a file system.
 * Uses default Linux MTD layout - change if you need something different.
 *
 * This source is originally ported from the yaffs2 online cvs website
 * <http://www.aleph1.co.uk/cgi-bin/viewvc.cgi/yaffs2/>, and modified by
 * Luen-Yung Lin for yaffs2utils.
 *
 * Luen-Yung Lin <penguin.lin@gmail.com>
 */
/*
 * TODO:
 * 1. Verify the code used for yaffs1
 */
 
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "yaffs_packedtags1.h"
#include "yaffs_packedtags2.h"
#include "yaffs_tagsvalidity.h"

#include "yaffs2utils.h"
#include "yaffs2utils_io.h"

/*-------------------------------------------------------------------------*/

unsigned yaffs_traceMask = 0;

/*-------------------------------------------------------------------------*/

#define DEFAULT_CHUNK_SIZE	2048
#define DEFAULT_OBJECT_NUMBERS	65536
#define ENDIAN_SWAP32(x)	((((x) & 0x000000FF) << 24) | \
				(((x) & 0x0000FF00) << 8) | \
				(((x) & 0x00FF0000) >> 8) | \
				(((x) & 0xFF000000) >> 24))
#define ENDIAN_SWAP16(x)	((((x) & 0x00FF) << 8) | \
				(((x) & 0xFF00) >> 8))

/*-------------------------------------------------------------------------*/

typedef struct object_item {
	dev_t dev;
	ino_t ino;
	unsigned obj;
} object_item_t;

/*-------------------------------------------------------------------------*/

static unsigned yaffs2_chunk_size = 0;
static unsigned yaffs2_spare_size = 0;

static unsigned yaffs2_object_list_size = DEFAULT_OBJECT_NUMBERS;
static object_item_t *yaffs2_object_list = 0;

static unsigned yaffs2_current_objects = YAFFS_NOBJECT_BUCKETS;
static unsigned yaffs2_total_objects = 0;
static unsigned yaffs2_total_directories = 0;
static unsigned  yaffs2_total_pages = 0;

static int yaffs2_outfd = -1;
static int yaffs2_convert_endian = 0;

static int (*oobfree_info)[2] = 0;
static int (*write_chunk)(unsigned char *, unsigned, unsigned, unsigned) = NULL;

/*-------------------------------------------------------------------------*/

static int 
object_list_compare (const void *a, const void * b)
{
	object_item_t *oa, *ob;
  
	oa = (object_item_t *)a;
	ob = (object_item_t *)b;
  
	if (oa->dev < ob->dev) 	return -1;
	if (oa->dev > ob->dev) 	return 1;
	if (oa->ino < ob->ino) 	return -1;
	if (oa->ino > ob->ino) 	return 1;

	return 0;
}

static int 
object_list_add (object_item_t *object)
{
	/* realloc much memory to store the object list. */
	if (yaffs2_total_objects >= yaffs2_object_list_size) {
		size_t newsize;

		if (yaffs2_object_list_size * 2 < YAFFS_UNUSED_OBJECT_ID) {
			yaffs2_object_list_size *= 2;
		}
		else if (yaffs2_object_list_size <  YAFFS_UNUSED_OBJECT_ID) {
			yaffs2_object_list_size = YAFFS_UNUSED_OBJECT_ID;
		}
		else {
	                fprintf(stderr, "too much objects (max: %d)\n",
				YAFFS_UNUSED_OBJECT_ID);
			return -1;
		}

		newsize = sizeof(object_item_t) * yaffs2_object_list_size;
		yaffs2_object_list = realloc(yaffs2_object_list, newsize);
		if (yaffs2_object_list == NULL) {
			fprintf(stderr, "cannot allocate memory for objects ");
			fprintf(stderr, "(%d objects array, %d bytes)\n",
				yaffs2_object_list_size, newsize);
			free(yaffs2_object_list);
			return -1;
		}
	}

	/* add the object */
	yaffs2_object_list[yaffs2_total_objects].dev = object->dev;
	yaffs2_object_list[yaffs2_total_objects].ino = object->ino;
	yaffs2_object_list[yaffs2_total_objects].obj = object->obj;
	qsort(yaffs2_object_list, ++yaffs2_total_objects, sizeof(object_item_t),
	      object_list_compare);

	return 0;
}

static int 
object_list_search (object_item_t *object)
{
	object_item_t *r = NULL;
	
	if (yaffs2_total_objects > 0 &&
	    (r = bsearch(object, yaffs2_object_list, yaffs2_total_objects,
			 sizeof(object_item_t), object_list_compare)) != NULL)
	{
		object->obj = r->obj;
		return 0;
	}

	return -1;
}

/*-------------------------------------------------------------------------*/

static void
packedtags1_endian_transform (yaffs_PackedTags1 *pt)
{
	yaffs_TagsUnion *tags = (yaffs_TagsUnion *)pt; // Work in bytes.
	yaffs_TagsUnion temp;

	memset(&temp, 0, sizeof(temp));
	// Ick, I hate magic numbers.
	temp.asBytes[0] = ((tags->asBytes[2] & 0x0F) << 4) |
			  ((tags->asBytes[1] & 0xF0) >> 4);
	temp.asBytes[1] = ((tags->asBytes[1] & 0x0F) << 4) |
			  ((tags->asBytes[0] & 0xF0) >> 4);
	temp.asBytes[2] = ((tags->asBytes[0] & 0x0F) << 4) |
			  ((tags->asBytes[2] & 0x30) >> 2) |
			  ((tags->asBytes[3] & 0xC0) >> 6);
	temp.asBytes[3] = ((tags->asBytes[3] & 0x3F) << 2) |
			  ((tags->asBytes[2] & 0xC0) >> 6);
	temp.asBytes[4] = ((tags->asBytes[6] & 0x03) << 6) |
			  ((tags->asBytes[5] & 0xFC) >> 2);
	temp.asBytes[5] = ((tags->asBytes[5] & 0x03) << 6) |
			  ((tags->asBytes[4] & 0xFC) >> 2);
	temp.asBytes[6] = ((tags->asBytes[4] & 0x03) << 6) |
			  (tags->asBytes[7] & 0x3F);
	temp.asBytes[7] = (tags->asBytes[6] & 0xFC) |
			  ((tags->asBytes[7] & 0xC0) >> 6);

	// Now copy it back.
	tags->asBytes[0] = temp.asBytes[0];
	tags->asBytes[1] = temp.asBytes[1];
	tags->asBytes[2] = temp.asBytes[2];
	tags->asBytes[3] = temp.asBytes[3];
	tags->asBytes[4] = temp.asBytes[4];
	tags->asBytes[5] = temp.asBytes[5];
	tags->asBytes[6] = temp.asBytes[6];
	tags->asBytes[7] = temp.asBytes[7];
}

static void 
packedtags1_ecc_calculate (yaffs_PackedTags1 *pt)
{
	/* Calculate an ecc */

	unsigned char *b = ((yaffs_TagsUnion *) pt)->asBytes;
	unsigned i, j;
	unsigned ecc = 0;
	unsigned bit = 0;

	/* clear the ecc field */
	if (yaffs2_convert_endian) {
		b[6] &= 0xC0;
		b[7] &= 0x03;
	}
	else {
		pt->ecc = 0;
	}

	for (i = 0; i < 8; i++) {
		for (j = 1; j & 0xff; j <<= 1) {
			bit++;
			if (b[i] & j)
				ecc ^= bit;
		}
	}

	if (yaffs2_convert_endian) {
		b[6] |= ((ecc >> 6) & 0x3F);
		b[7] |= ((ecc & 0x3F) << 2);
	}
	else {
		pt->ecc = ecc;
	}
}

/*-------------------------------------------------------------------------*/

static void
packedtags2_tagspart_endian_transform (yaffs_PackedTags2 *t)
{
	yaffs_PackedTags2TagsPart *tp = &t->t;

	tp->sequenceNumber = ENDIAN_SWAP32(tp->sequenceNumber);
	tp->objectId = ENDIAN_SWAP32(tp->objectId);
	tp->chunkId = ENDIAN_SWAP32(tp->chunkId);
	tp->byteCount = ENDIAN_SWAP32(tp->byteCount);	
}

static void 
packedtags2_eccother_endian_transform (yaffs_PackedTags2 *t)
{
	yaffs_ECCOther *e = &t->ecc;

	e->lineParity = ENDIAN_SWAP32(e->lineParity);
	e->lineParityPrime = ENDIAN_SWAP32(e->lineParityPrime);
}

/*-------------------------------------------------------------------------*/

static void 
object_header_endian_transform (yaffs_ObjectHeader *oh)
{
	oh->type = ENDIAN_SWAP32(oh->type); // GCC makes enums 32 bits.
	oh->parentObjectId = ENDIAN_SWAP32(oh->parentObjectId); // int
	// __u16 - Not used, but done for completeness.
	oh->sum__NoLongerUsed = ENDIAN_SWAP16(oh->sum__NoLongerUsed);
	oh->yst_mode = ENDIAN_SWAP32(oh->yst_mode);

	// Aiee. An int... signed, at that!
	oh->fileSize = ENDIAN_SWAP32(oh->fileSize);
	oh->equivalentObjectId = ENDIAN_SWAP32(oh->equivalentObjectId);
	oh->yst_rdev = ENDIAN_SWAP32(oh->yst_rdev);

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
	
	oh->win_ctime[0] = ENDIAN_SWAP32(oh->win_ctime[0]);
	oh->win_ctime[1] = ENDIAN_SWAP32(oh->win_ctime[1]);
	oh->win_atime[0] = ENDIAN_SWAP32(oh->win_atime[0]);
	oh->win_atime[1] = ENDIAN_SWAP32(oh->win_atime[1]);
	oh->win_mtime[0] = ENDIAN_SWAP32(oh->win_mtime[0]);
	oh->win_mtime[1] = ENDIAN_SWAP32(oh->win_mtime[1]);
	oh->roomToGrow[0] = ENDIAN_SWAP32(oh->roomToGrow[0]);
	oh->roomToGrow[1] = ENDIAN_SWAP32(oh->roomToGrow[1]);
	oh->roomToGrow[2] = ENDIAN_SWAP32(oh->roomToGrow[2]);
	oh->roomToGrow[3] = ENDIAN_SWAP32(oh->roomToGrow[3]);
	oh->roomToGrow[4] = ENDIAN_SWAP32(oh->roomToGrow[4]);
	oh->roomToGrow[5] = ENDIAN_SWAP32(oh->roomToGrow[5]);
#else
	// Regular POSIX.
	oh->yst_uid = ENDIAN_SWAP32(oh->yst_uid);
	oh->yst_gid = ENDIAN_SWAP32(oh->yst_gid);
	oh->yst_atime = ENDIAN_SWAP32(oh->yst_atime);
	oh->yst_mtime = ENDIAN_SWAP32(oh->yst_mtime);
	oh->yst_ctime = ENDIAN_SWAP32(oh->yst_ctime);

	oh->roomToGrow[0] = ENDIAN_SWAP32(oh->roomToGrow[0]);
	oh->roomToGrow[1] = ENDIAN_SWAP32(oh->roomToGrow[1]);
	oh->roomToGrow[2] = ENDIAN_SWAP32(oh->roomToGrow[2]);
	oh->roomToGrow[3] = ENDIAN_SWAP32(oh->roomToGrow[3]);
	oh->roomToGrow[4] = ENDIAN_SWAP32(oh->roomToGrow[4]);
	oh->roomToGrow[5] = ENDIAN_SWAP32(oh->roomToGrow[5]);

	oh->inbandShadowsObject = ENDIAN_SWAP32(oh->inbandShadowsObject);
	oh->inbandIsShrink = ENDIAN_SWAP32(oh->inbandIsShrink);
	oh->reservedSpace[0] = ENDIAN_SWAP32(oh->reservedSpace[0]);
	oh->reservedSpace[9] = ENDIAN_SWAP32(oh->reservedSpace[1]);
	oh->shadowsObject = ENDIAN_SWAP32(oh->shadowsObject);
	oh->isShrink = ENDIAN_SWAP32(oh->isShrink);
#endif
}

static ssize_t
tags2spare (unsigned char *spare, unsigned char *tags, size_t bytes)
{
	unsigned int i;
	size_t copied = 0;

	for (i = 0; i < 8 && copied < bytes; i++) {
		size_t size;
		size = bytes > oobfree_info[i][1] ? oobfree_info[i][1] : bytes;
		spare += oobfree_info[i][0];
		memcpy(spare, tags, size);
		if (memcmp(spare, tags, size)) {
			return -1;
		}
		copied += size;
		tags += size;
	}

	return copied;
}

/*-------------------------------------------------------------------------*/

static int 
yaffs1_write_chunk (unsigned char *data, 
		    unsigned bytes, 
		    unsigned object_id, 
		    unsigned chunk_id)
{
	ssize_t written;
	yaffs_ExtendedTags et;
	yaffs_PackedTags1 pt;
	unsigned char *spare;

	spare = (unsigned char *)malloc(yaffs2_spare_size);
	if (spare == NULL) {
		return -1;
	}

	/* 
	 * write a page
	 */
	written = safe_write(yaffs2_outfd, data, yaffs2_chunk_size);
	if(written != yaffs2_chunk_size) {
		free(spare);
		return -1;
	}

	yaffs2_total_pages++;

	/*
	 * write a oob
	 */
	yaffs_InitialiseTags(&et);
	
	et.chunkId = chunk_id;
//	et.serialNumber = 0;	// double check
	et.serialNumber = 1;	// double check
	et.byteCount = bytes;
	et.objectId = object_id;
	et.chunkDeleted = 0;

	memset(&pt, 0xff, sizeof(yaffs_PackedTags1));
	yaffs_PackTags1(&pt, &et);

	if (yaffs2_convert_endian) {
		packedtags1_endian_transform(&pt);
	}

#ifndef YAFFS_IGNORE_TAGS_ECC
	packedtags1_ecc_calculate(&pt);
#endif

	memset(spare, 0xff, yaffs2_spare_size);
	written = tags2spare(spare, (unsigned char *)&pt,
			     sizeof(yaffs_PackedTags1));
	if (written != (sizeof(yaffs_PackedTags1) - sizeof(pt.shouldBeFF))) {
		free(spare);
		return -1;
	}

	written = safe_write(yaffs2_outfd, spare, yaffs2_spare_size);

	free(spare);

	return !(written == yaffs2_spare_size);
}

/*-------------------------------------------------------------------------*/

static int 
yaffs2_write_chunk (unsigned char *data, 
		    unsigned bytes, 
		    unsigned object_id, 
		    unsigned chunk_id)
{
	ssize_t written;
	yaffs_ExtendedTags et;
	yaffs_PackedTags2 pt;
	unsigned char *spare;

	spare = (unsigned char *)malloc(yaffs2_spare_size);
	if (spare == NULL) {
		return -1;
	}

	/* 
	 * write a page
	 */
	written = safe_write(yaffs2_outfd, data, yaffs2_chunk_size);
	if(written != yaffs2_chunk_size) {
		free(spare);
		return -1;
	}

	yaffs2_total_pages++;

	/*
	 * write a oob
	 */
	yaffs_InitialiseTags(&et);
	
	et.chunkId = chunk_id;
//	et.serialNumber = 0;	// double check
	et.serialNumber = 1;	// double check
	et.byteCount = bytes;
	et.objectId = object_id;
	et.chunkUsed = 1;
	et.sequenceNumber = YAFFS_LOWEST_SEQUENCE_NUMBER;

	memset(&pt, 0xff, sizeof(yaffs_PackedTags2));
	yaffs_PackTags2TagsPart(&pt.t, &et);

	if (yaffs2_convert_endian) {
		packedtags2_tagspart_endian_transform(&pt);
	}

#ifndef YAFFS_IGNORE_TAGS_ECC
	yaffs_ECCCalculateOther((unsigned char *)&pt.t,
				sizeof(yaffs_PackedTags2TagsPart),
				&pt.ecc);
	if (yaffs2_convert_endian) {
		packedtags2_eccother_endian_transform(&pt);
	}
#endif

	memset(spare, 0xff, yaffs2_spare_size);
	written = tags2spare(spare, (unsigned char *)&pt,
			     sizeof(yaffs_PackedTags2));
	if (written != sizeof(yaffs_PackedTags2)) {
		free(spare);
		return -1;
	}

	written = safe_write(yaffs2_outfd, spare, yaffs2_spare_size);

	free(spare);

	return !(written == yaffs2_spare_size);
}

/*-------------------------------------------------------------------------*/

static int 
write_object_header (const char *name, 
		     struct stat *s, 
		     yaffs_ObjectType type, 
		     const char *alias, 
		     unsigned object_id, 
		     unsigned parent_id, 
		     unsigned equivalent_id)
{
	int retval;
	unsigned char *buf;

	buf = (unsigned char *)malloc(yaffs2_chunk_size);
	if (buf == NULL) {
		return -1;
	}

	yaffs_ObjectHeader *oh = (yaffs_ObjectHeader *)buf;
	memset(buf, 0xff, yaffs2_chunk_size);

	oh->type = type;
	oh->parentObjectId = parent_id;
	strncpy(oh->name, name, YAFFS_MAX_NAME_LENGTH);
	
	if(type != YAFFS_OBJECT_TYPE_HARDLINK) {
		oh->yst_mode = s->st_mode;
		oh->yst_uid = s->st_uid;
		oh->yst_gid = s->st_gid;
		oh->yst_atime = s->st_atime;
		oh->yst_mtime = s->st_mtime;
		oh->yst_ctime = s->st_ctime;
		oh->yst_rdev  = s->st_rdev;
	}

	switch (type) {
	case YAFFS_OBJECT_TYPE_FILE:
		oh->fileSize = s->st_size;
		break;
	case YAFFS_OBJECT_TYPE_HARDLINK:
		oh->equivalentObjectId = equivalent_id;
		break;
	case YAFFS_OBJECT_TYPE_SYMLINK:
		strncpy(oh->alias, alias, YAFFS_MAX_ALIAS_LENGTH);
		break;
	default:
		break;
	}

	if (yaffs2_convert_endian) {
 	   	object_header_endian_transform(oh);
	}

	retval = write_chunk(buf, 0xffff, object_id, 0);

	free(buf);

	return retval;
}

/*-------------------------------------------------------------------------*/

static int 
parse_regular_file (const char *fpath, 
		    struct dirent *dentry, 
		    unsigned object_id, 
		    unsigned parent_id)
{
	int fd, retval = -1;
	ssize_t bytes;
	unsigned chunk = 0;
	unsigned char *buf;

	buf = (unsigned char *)malloc(yaffs2_chunk_size);
	if (buf == NULL) {
		fprintf(stderr, "cannot allocate memory for %s", fpath);
		goto out;
	}

	fd = open(fpath, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "cannot open the file: %s\n", fpath);
		goto free_out;
	}

	memset(buf, 0xff, yaffs2_chunk_size);
	while((bytes = safe_read(fd, buf, yaffs2_chunk_size)) != 0) {
		if (bytes < 0) {
			fprintf(stderr, "error while reading file %s\n", fpath);
			retval = bytes;
			goto close_out;
		}

		retval = write_chunk(buf, bytes, object_id, ++chunk);
		if (retval) {
			fprintf(stderr, "error while writing file %s\n", fpath);
			goto close_out;
		}

		memset(buf, 0xff, yaffs2_chunk_size);
	}

close_out:
	close(fd);
free_out:
	free(buf);
out:
	return retval;
}

/*-------------------------------------------------------------------------*/

static int 
parse_directory (unsigned parent, const char *path)
{
	DIR *dir;
	struct dirent *dent;

	yaffs2_total_directories++;
	
	dir = opendir(path);
	if (dir == NULL) {
		fprintf(stderr, "cannot open the directory: %s\n", path);
		return -1;
	}
	
	while((dent = readdir(dir)) != NULL) {
		/* ignore "." and ".." */
		if (!strcmp(dent->d_name, ".") ||
		    !strcmp(dent->d_name, ".."))
		{
			continue;
		}

		int retval = -1;	
 		char fpath[PATH_MAX];
		unsigned obj;
		struct stat s;
		
		sprintf(fpath, "%s/%s", path, dent->d_name);
		if (lstat(fpath, &s) < 0 ) {
			fprintf(stderr, "cannot stat the file: %s\n", fpath);
			continue;
		}
		
		if (S_ISLNK(s.st_mode) ||
		    S_ISREG(s.st_mode) ||
		    S_ISDIR(s.st_mode) ||
		    S_ISBLK(s.st_mode) ||
		    S_ISCHR(s.st_mode) ||
		    S_ISFIFO(s.st_mode) ||
		    S_ISSOCK(s.st_mode))
		{
			struct object_item o;
			yaffs_ObjectType ftype;

			obj = ++yaffs2_current_objects;
			
			printf("Object %d, %s is a ", obj, fpath);
			
			/* We're going to create an object for it */
			o.dev = s.st_dev;
			o.ino = s.st_ino;
			o.obj = obj;

			/* hard link */
			if (!object_list_search(&o)) {
			 	/* we need to make a hard link */
				ftype = YAFFS_OBJECT_TYPE_HARDLINK;
			 	printf("hard link to object %d\n", o.obj);
				retval = write_object_header(dent->d_name, &s,
							     ftype, NULL, obj,
							     parent, o.obj);
				if (retval) {
					goto error;
				}
				continue;
			}

			retval = object_list_add(&o);
			if (retval) {
				goto error;
			}

			if (S_ISLNK(s.st_mode)) {
				char sympath[PATH_MAX];

				ftype = YAFFS_OBJECT_TYPE_SYMLINK;
				memset(sympath, 0, sizeof(sympath));
				readlink(fpath, sympath, sizeof(sympath) -1);
				printf("symbolic link to \"%s\"\n", sympath);
				retval = write_object_header(dent->d_name, &s,
							     ftype, sympath,
							     obj, parent, -1);
			}
			else if (S_ISREG(s.st_mode)) {
				printf("file\n");
				ftype = YAFFS_OBJECT_TYPE_FILE;
				retval = write_object_header(dent->d_name, &s,
							     ftype, NULL, obj,
							     parent, -1);
				if (!retval) {
					retval = parse_regular_file(fpath, dent,
								    obj, parent);
				}
			}
			else if (S_ISSOCK(s.st_mode)) {
				printf("socket\n");
				ftype = YAFFS_OBJECT_TYPE_SPECIAL;
				retval = write_object_header(dent->d_name, &s,
						    	     ftype, NULL, obj,
							     parent, -1);
			}
			else if (S_ISFIFO(s.st_mode)) {
				printf("fifo\n");
				ftype = YAFFS_OBJECT_TYPE_SPECIAL;
				retval = write_object_header(dent->d_name, &s,
						    	     ftype, NULL, obj,
							     parent, -1);
			}
			else if (S_ISCHR(s.st_mode)) {
				printf("character device\n");
				ftype = YAFFS_OBJECT_TYPE_SPECIAL;
				retval = write_object_header(dent->d_name, &s,
						    	     ftype, NULL, obj,
							     parent, -1);
			}
			else if (S_ISBLK(s.st_mode)) {
				printf("block device\n");
				ftype = YAFFS_OBJECT_TYPE_SPECIAL;
				retval = write_object_header(dent->d_name, &s,
						    	     ftype, NULL, obj,
							     parent, -1);
			}
			else if (S_ISDIR(s.st_mode)) {
				printf("directory\n");
				ftype = YAFFS_OBJECT_TYPE_DIRECTORY; 
				retval = write_object_header(dent->d_name, &s,
						    	     ftype, NULL, obj,
							     parent, -1);
				if (!retval) {
					retval = parse_directory(obj, fpath);
				}
			}
		}
		else {
			fprintf(stderr, "unsupport type for %s\n", fpath);
		}

error:
		if (retval) {
			fprintf(stderr, "error while parsing %s\n", fpath);
			return -1;
		}
	}

	return 0;
}

/*-------------------------------------------------------------------------*/

static void
show_usage (void)
{
	fprintf(stderr, "Usage: mkyaffs2 [-e] [-h] [-p pagesize] dirname outfile\n");
	fprintf(stderr, "mkyaffs2: A simple utility to make the yaffs2 image\n");
	fprintf(stderr, "Version: %s\n\n", YAFFS2PROGS_VERSION);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "	-h		display this help message and exit\n");
	fprintf(stderr, "	-e		convert the endian differed from the local machine\n");
	fprintf(stderr, "	-p pagesize	page size (512|2048, default: %d)\n", DEFAULT_CHUNK_SIZE);
	fprintf(stderr, "			512 bytes page size will format the yaffs1 image\n");
}

/*-------------------------------------------------------------------------*/

int 
main(int argc, char *argv[])
{
	int retval, objsize;
	char *input_path, *output_path;
	struct stat statbuf;
	
	int option, option_index;
	static const char *short_options = "hep:";
	static const struct option long_options[] = {
		{"pagesize", 	required_argument, 	0, 'p'},
		{"endian", 	no_argument, 		0, 'e'},
		{"help", 	no_argument, 		0, 'h'},
	};

	printf("mkyaffs2-%s: image building tool for YAFFS2\n",
		YAFFS2PROGS_VERSION);

	yaffs2_chunk_size = DEFAULT_CHUNK_SIZE;

	while ((option = getopt_long(argc, argv, short_options,
				     long_options, &option_index)) != EOF)
	{
		switch (option) {
		case 'p':
			yaffs2_chunk_size = strtoul(optarg, NULL, 10);
			break;
		case 'e':
			yaffs2_convert_endian = 1;
			break;
		case 'h':
			show_usage();
			return 0;
		default:
			fprintf(stderr, "unkown option: %s\n\n", 
				argv[option_index]);
			show_usage();
			return -1;
		}
	}

	if (argc - optind < 2) {
		show_usage();
		return -1;
	}

	input_path = argv[optind];
	output_path = argv[optind + 1];

	/* valid whethe the page size is valid */
	switch (yaffs2_chunk_size) {
	case 512:
		write_chunk = &yaffs1_write_chunk;
		oobfree_info = (int (*)[2])nand_oobfree_16;
		break;
	case 2048:
		write_chunk = &yaffs2_write_chunk;
		oobfree_info = (int (*)[2])nand_oobfree_64;
		break;
	default:
		fprintf(stderr, "%d bytes page size is not supported\n",
			yaffs2_chunk_size);
		return -1;
	}

	/* spare size */
	yaffs2_spare_size = yaffs2_chunk_size / 32;

	/* verify whether the input directory is valid */
	if (stat(input_path, &statbuf) < 0 && 
	    !S_ISDIR(statbuf.st_mode))
	{
		fprintf(stderr, "%s is not a directory\n", input_path);
		return -1;
	}

	/* allocate default objects array */
	objsize = sizeof(object_item_t) * yaffs2_object_list_size;
	yaffs2_object_list = (object_item_t *)malloc(objsize);
	if (yaffs2_object_list == NULL) {
		fprintf(stderr, "cannot allocate memory for objects array ");
		fprintf(stderr, "(default: %d bytes)\n", 
			sizeof(object_item_t) * yaffs2_object_list_size);
		return -1;
	}

	/* output file */
	yaffs2_outfd = open(output_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	if (yaffs2_outfd < 0) {
		fprintf(stderr, "cannot open the ourput file: %s\n", 
			output_path);
		free(yaffs2_object_list);
		return -1;
	}

	printf("Processing directory %s into image file %s\n",
		input_path, output_path);
	retval = write_object_header("", &statbuf, YAFFS_OBJECT_TYPE_DIRECTORY,
				     NULL, 1, 1, -1);
	if (!retval) {
		retval = parse_directory(YAFFS_OBJECTID_ROOT, input_path);
	}

	if (retval) {
		fprintf(stderr, "operation incomplete!\n");
	}
	else {
		printf("operation complete.\n");
		printf("%u objects in %u directories\n%u NAND pages\n", 
			yaffs2_total_objects, yaffs2_total_directories, 
			yaffs2_total_pages);
	}

	free(yaffs2_object_list);	
	close(yaffs2_outfd);

	return retval;
}
