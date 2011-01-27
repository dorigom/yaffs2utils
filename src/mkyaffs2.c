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
/*
 * mkyaffs2.c 
 *
 * Makes a YAFFS2 file system image that can be used to load up a file system.
 * Uses default Linux MTD layout - change if you need something different.
 *
 * This source is originally ported from the yaffs2 official cvs 
 * <http://yaffs.net/node/346>, and modified by Luen-Yung Lin for yaffs2utils.
 *
 * Luen-Yung Lin <penguin.lin@gmail.com>
 */
 
#include <stdio.h>
#include <string.h>
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
#include "yaffs2utils_endian.h"

/*-------------------------------------------------------------------------*/

unsigned yaffs_trace_mask = 0;

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
static unsigned yaffs2_total_pages = 0;

static int yaffs2_outfd = -1;
static int yaffs2_convert_endian = 0;

static int (*oobfree_info)[2] = 0;
static int (*write_chunk)(unsigned, unsigned, unsigned) = NULL;

static unsigned char *yaffs2_data_buffer = NULL;

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
	/* realloc much memory to store the objects list. */
	if (yaffs2_total_objects >= yaffs2_object_list_size) {
		size_t newsize;
		object_item_t *newlist;

		if (yaffs2_object_list_size * 2 < MAX_OBJECT_NUMBERS) {
			yaffs2_object_list_size *= 2;
		}
		else if (yaffs2_object_list_size <  MAX_OBJECT_NUMBERS) {
			yaffs2_object_list_size = MAX_OBJECT_NUMBERS;
		}
		else {
			fprintf(stderr, "too much objects (max: %u)\n",
				MAX_OBJECT_NUMBERS);
			return -1;
		}

		newsize = sizeof(object_item_t) * yaffs2_object_list_size;
		newlist = realloc(yaffs2_object_list, newsize);
		if (newlist == NULL) {
			fprintf(stderr, "cannot allocate objects list ");
			fprintf(stderr, "(%u objects, %u bytes)\n",
				yaffs2_object_list_size, newsize);
			return -1;
		}
		yaffs2_object_list = newlist;
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
packedtags1_ecc_calculate (struct yaffs_packed_tags1 *pt)
{
	unsigned char *b = ((union yaffs_tags_union *)pt)->as_bytes;
	unsigned i, j;
	unsigned ecc = 0;
	unsigned bit = 0;

	/* clear the ecc field */
	if (yaffs2_convert_endian) {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		b[6] &= 0xC0;
		b[7] &= 0x03;
#elif defined(__BIG_ENDIAN_BITFIELD)
		b[6] &= 0x03;
		b[7] &= 0xC0;
#endif
	}
	else {
		pt->ecc = 0;
	}

	/* calculate ecc */
	for (i = 0; i < 8; i++) {
		for (j = 1; j & 0xFF; j <<= 1) {
			bit++;
			if (b[i] & j)
				ecc ^= bit;
		}
	}

	/* write ecc back to tags */
	if (yaffs2_convert_endian) {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		b[6] |= ((ecc >> 6) & 0x3F);
		b[7] |= ((ecc & 0x3F) << 2);
#elif defined(__BIG_ENDIAN_BITFIELD)
		b[6] |= ((ecc & 0x3F) << 2);
		b[7] |= ((ecc >> 6) & 0x3F);
#endif
	}
	else {
		pt->ecc = ecc;
	}
}

/*-------------------------------------------------------------------------*/

static ssize_t
tags2spare (unsigned char *spare, unsigned char *tags, size_t bytes)
{
	unsigned i;
	size_t copied = 0;

	for (i = 0; i < 8 && copied < bytes; i++) {
		size_t size = bytes - copied;
		unsigned char *s = spare + oobfree_info[i][0];

		if (size > oobfree_info[i][1]) {
			size = oobfree_info[i][1];
		}

		memcpy(s, tags, size);
		if (memcmp(s, tags, size)) {
			return -1;
		}

		copied += size;
		tags += size;
	}

	return copied;
}

/*-------------------------------------------------------------------------*/

static int 
yaffs1_write_chunk (unsigned bytes,
		    unsigned object_id,
		    unsigned chunk_id)
{
	ssize_t written;
	struct yaffs_ext_tags et;
	struct yaffs_packed_tags1 pt;
	size_t bufsize = yaffs2_chunk_size + yaffs2_spare_size;
	unsigned char *spare = yaffs2_data_buffer + yaffs2_chunk_size;

	/* prepare the spare (oob) first */
	yaffs_init_tags(&et);
	
	et.chunk_id = chunk_id;
//	et.serial_number = 0;	// double check
	et.serial_number = 1;	// double check
	et.n_bytes = bytes;
	et.obj_id = object_id;
	et.is_deleted = 0;	// double check

	memset(&pt, 0xFF, sizeof(struct yaffs_packed_tags1));
	yaffs_pack_tags1(&pt, &et);

	if (yaffs2_convert_endian) {
		packedtags1_endian_transform(&pt, 0);
	}

#ifndef YAFFS_IGNORE_TAGS_ECC
	packedtags1_ecc_calculate(&pt);
#endif

	/* write the spare (oob) into the buffer */
	memset(spare, 0xFF, yaffs2_spare_size);
	written = tags2spare(spare, (unsigned char *)&pt,
			     sizeof(struct yaffs_packed_tags1) -
			     sizeof(pt.should_be_ff));
	if (written != (sizeof(struct yaffs_packed_tags1) -
			sizeof(pt.should_be_ff))) {
		return -1;
	}

	/* write a whole "chunk + spare" back to the image */
	written = safe_write(yaffs2_outfd, yaffs2_data_buffer, bufsize);
	if (written != bufsize) {
		return -1;
	}

	yaffs2_total_pages++;

	return 0;
}

/*-------------------------------------------------------------------------*/

static int
yaffs2_write_chunk (unsigned bytes,
		    unsigned object_id,
		    unsigned chunk_id)
{
	ssize_t written;
	struct yaffs_ext_tags et;
	struct yaffs_packed_tags2 pt;
	size_t bufsize = yaffs2_chunk_size + yaffs2_spare_size;
	unsigned char *spare = yaffs2_data_buffer + yaffs2_chunk_size;

	/* prepare the spare (oob) first */
	yaffs_init_tags(&et);
	
	et.chunk_id = chunk_id;
//	et.serial_number = 0;	// double check
	et.serial_number = 1;	// double check
	et.n_bytes = bytes;
	et.obj_id = object_id;
	et.chunk_used = 1;
	et.seq_number = YAFFS_LOWEST_SEQUENCE_NUMBER;

	memset(&pt, 0xFF, sizeof(struct yaffs_packed_tags2));
	yaffs_pack_tags2_tags_only(&pt.t, &et);

	if (yaffs2_convert_endian) {
		packedtags2_tagspart_endian_transform(&pt);
	}

#ifndef YAFFS_IGNORE_TAGS_ECC
	yaffs_ecc_calc_other((unsigned char *)&pt.t,
				sizeof(struct yaffs_packed_tags2_tags_only),
				&pt.ecc);
	if (yaffs2_convert_endian) {
		packedtags2_eccother_endian_transform(&pt);
	}
#endif

	/* write the spare (oob) into the buffer */
	memset(spare, 0xFF, yaffs2_spare_size);
	written = tags2spare(spare, (unsigned char *)&pt,
			     sizeof(struct yaffs_packed_tags2));
	if (written != sizeof(struct yaffs_packed_tags2)) {
		return -1;
	}

	/* write a whole "chunk + spare" back to the image */
	written = safe_write(yaffs2_outfd, yaffs2_data_buffer, bufsize);
	if (written != bufsize) {
		return -1;
	}

	yaffs2_total_pages++;

	return 0;
}

/*-------------------------------------------------------------------------*/

static int 
write_object_header (const char *name,
		     struct stat *s,
		     enum yaffs_obj_type type,
		     const char *alias,
		     unsigned object_id,
		     unsigned parent_id,
		     unsigned equivalent_id)
{
	int retval;
	struct yaffs_obj_hdr oh;

	memset(&oh, 0xFF, sizeof(struct yaffs_obj_hdr));

	oh.type = type;
	oh.parent_obj_id = parent_id;
	strncpy(oh.name, name, YAFFS_MAX_NAME_LENGTH);
	
	if(type != YAFFS_OBJECT_TYPE_HARDLINK) {
		oh.yst_mode = s->st_mode;
		oh.yst_uid = s->st_uid;
		oh.yst_gid = s->st_gid;
		oh.yst_atime = s->st_atime;
		oh.yst_mtime = s->st_mtime;
		oh.yst_ctime = s->st_ctime;
		oh.yst_rdev  = s->st_rdev;
	}

	switch (type) {
	case YAFFS_OBJECT_TYPE_FILE:
		oh.file_size = s->st_size;
		break;
	case YAFFS_OBJECT_TYPE_HARDLINK:
		oh.equiv_id = equivalent_id;
		break;
	case YAFFS_OBJECT_TYPE_SYMLINK:
		strncpy(oh.alias, alias, YAFFS_MAX_ALIAS_LENGTH);
		break;
	default:
		break;
	}

	if (yaffs2_convert_endian) {
 	   	objheader_endian_transform(&oh);
	}

	/* copy header into the buffer */
	memset(yaffs2_data_buffer, 0xFF, yaffs2_chunk_size);
	memcpy(yaffs2_data_buffer, &oh, sizeof(struct yaffs_obj_hdr));

	/* write buffer */
	retval = write_chunk(0xFFFF, object_id, 0);

	return retval;
}

/*-------------------------------------------------------------------------*/

static int 
parse_regular_file (const char *fpath,
		    struct dirent *dentry,
		    unsigned object_id,
		    unsigned parent_id)
{
	int fd, retval = 0;
	ssize_t bytes;
	unsigned chunk = 0;
	unsigned char *datbuf = yaffs2_data_buffer;

	fd = open(fpath, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "cannot open the file: %s\n", fpath);
		return -1;
	}

	memset(datbuf, 0xFF, yaffs2_chunk_size);
	while((bytes = safe_read(fd, datbuf, yaffs2_chunk_size)) != 0) {
		if (bytes < 0) {
			fprintf(stderr, "error while reading file %s\n", fpath);
			retval = bytes;
			break;
		}

		/* write buffer */
		retval = write_chunk(bytes, object_id, ++chunk);
		if (retval) {
			fprintf(stderr, "error while writing file %s\n", fpath);
			break;
		}

		memset(datbuf, 0xFF, yaffs2_chunk_size);
	}

	close(fd);

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

		int retval = 0;
 		char fpath[PATH_MAX];
		struct stat s;
		
		sprintf(fpath, "%s/%s", path, dent->d_name);
		if (lstat(fpath, &s) < 0) {
			fprintf(stderr, "warning: cannot stat the file: %s\n",
				fpath);
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
			unsigned id;
			struct object_item obj;
			enum yaffs_obj_type ftype;

			id = ++yaffs2_current_objects;
			
			printf("object %u, %s is a ", id, fpath);
			
			/* we're going to create an object for it */
			obj.dev = s.st_dev;
			obj.ino = s.st_ino;
			obj.obj = id;

			/* special case for hard link */
			if (!object_list_search(&obj)) {
			 	/* we need to make a hard link */
				ftype = YAFFS_OBJECT_TYPE_HARDLINK;
			 	printf("hard link to object %u\n", obj.obj);
				retval = write_object_header(dent->d_name, &s,
							     ftype, NULL, id,
							     parent, obj.obj);
				if (retval) {
					goto error;
				}
				continue;
			}

			retval = object_list_add(&obj);
			if (retval) {
				goto error;
			}

			if (S_ISLNK(s.st_mode)) {
				char sympath[PATH_MAX];

				ftype = YAFFS_OBJECT_TYPE_SYMLINK;
				memset(sympath, 0, sizeof(sympath));
				readlink(fpath, sympath, sizeof(sympath) -1);
				printf("symbolic link to %s\n", sympath);
				retval = write_object_header(dent->d_name, &s,
							     ftype, sympath,
							     id, parent, -1);
			}
			else if (S_ISREG(s.st_mode)) {
				printf("file\n");
				ftype = YAFFS_OBJECT_TYPE_FILE;
				retval = write_object_header(dent->d_name, &s,
							     ftype, NULL, id,
							     parent, -1);
				if (!retval) {
					retval = parse_regular_file(fpath, dent,
								    id, parent);
				}
			}
			else if (S_ISSOCK(s.st_mode)) {
				printf("socket\n");
				ftype = YAFFS_OBJECT_TYPE_SPECIAL;
				retval = write_object_header(dent->d_name, &s,
						    	     ftype, NULL, id,
							     parent, -1);
			}
			else if (S_ISFIFO(s.st_mode)) {
				printf("fifo\n");
				ftype = YAFFS_OBJECT_TYPE_SPECIAL;
				retval = write_object_header(dent->d_name, &s,
						    	     ftype, NULL, id,
							     parent, -1);
			}
			else if (S_ISCHR(s.st_mode)) {
				printf("character device\n");
				ftype = YAFFS_OBJECT_TYPE_SPECIAL;
				retval = write_object_header(dent->d_name, &s,
						    	     ftype, NULL, id,
							     parent, -1);
			}
			else if (S_ISBLK(s.st_mode)) {
				printf("block device\n");
				ftype = YAFFS_OBJECT_TYPE_SPECIAL;
				retval = write_object_header(dent->d_name, &s,
						    	     ftype, NULL, id,
							     parent, -1);
			}
			else if (S_ISDIR(s.st_mode)) {
				printf("directory\n");
				ftype = YAFFS_OBJECT_TYPE_DIRECTORY; 
				retval = write_object_header(dent->d_name, &s,
						    	     ftype, NULL, id,
							     parent, -1);
				if (!retval) {
					retval = parse_directory(id, fpath);
				}
			}
		}
		else {
			fprintf(stderr, "warning: unsupport type for %s\n",
				fpath);
		}

error:
		if (retval) {
			fprintf(stderr, "error while parsing %s\n", fpath);
			closedir(dir);
			return -1;
		}
	}

	closedir(dir);

	return 0;
}

/*-------------------------------------------------------------------------*/

static void
show_usage (void)
{
	fprintf(stderr, "Usage: mkyaffs2 [-e] [-h] [-p pagesize] dirname imgfile\n");
	fprintf(stderr, "mkyaffs2: A utility to make the yaffs2 image\n");
	fprintf(stderr, "version: %s\n", YAFFS2UTILS_VERSION);
	fprintf(stderr, "options:\n");
	fprintf(stderr, "	-h		display this help message and exit\n");
	fprintf(stderr, "	-e		convert the endian differed from the local machine\n");
	fprintf(stderr, "	-p pagesize	page size (512|2048, default: %u)\n", DEFAULT_CHUNK_SIZE);
	fprintf(stderr, "			512 bytes page size will format the yaffs1 image\n");
}

/*-------------------------------------------------------------------------*/

int 
main (int argc, char *argv[])
{
	int retval, objsize;
	char *dirpath, *imgpath;
	struct stat statbuf;
	
	int option, option_index;
	static const char *short_options = "hep:";
	static const struct option long_options[] = {
		{"pagesize", 	required_argument, 	0, 'p'},
		{"endian", 	no_argument, 		0, 'e'},
		{"help", 	no_argument, 		0, 'h'},
	};

	printf("mkyaffs2-%s: image building tool for YAFFS2\n",
		YAFFS2UTILS_VERSION);

	if (getuid() != 0) {
		fprintf(stderr, "warning: non-root users\n");
	}

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
			show_usage();
			return -1;
		}
	}

	if (argc - optind < 2) {
		show_usage();
		return -1;
	}

	dirpath = argv[optind];
	imgpath = argv[optind + 1];

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
		fprintf(stderr, "%u bytes page size is not supported\n",
			yaffs2_chunk_size);
		return -1;
	}

	/* spare size */
	yaffs2_spare_size = yaffs2_chunk_size / 32;

	/* verify whether the input directory is valid */
	if (stat(dirpath, &statbuf) < 0 && 
	    !S_ISDIR(statbuf.st_mode))
	{
		fprintf(stderr, "%s is not a directory\n", dirpath);
		return -1;
	}

	/* allocate default objects array */
	objsize = sizeof(object_item_t) * yaffs2_object_list_size;
	yaffs2_object_list = (object_item_t *)malloc(objsize);
	if (yaffs2_object_list == NULL) {
		fprintf(stderr, "cannot allocate objects list ");
		fprintf(stderr, "(default: %u objects, %u bytes)\n", 
			yaffs2_object_list_size,
			yaffs2_object_list_size * sizeof(object_item_t));
		return -1;
	}

	/* allocate working buffer */
	yaffs2_data_buffer = (unsigned char *)malloc(yaffs2_chunk_size +
						     yaffs2_spare_size);
	if (yaffs2_data_buffer == NULL) {
		fprintf(stderr, "cannot allocate working buffer ");
		fprintf(stderr, "(default: %u bytes)\n",
			yaffs2_chunk_size + yaffs2_spare_size);
		free(yaffs2_object_list);
		return -1;
	}

	/* output file */
	yaffs2_outfd = open(imgpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (yaffs2_outfd < 0) {
		fprintf(stderr, "cannot open the ourput file: %s\n", 
			imgpath);
		free(yaffs2_data_buffer);
		free(yaffs2_object_list);
		return -1;
	}

	printf("Processing directory %s into image file %s\n",
		dirpath, imgpath);
	retval = write_object_header("", &statbuf, YAFFS_OBJECT_TYPE_DIRECTORY,
				     NULL, 1, 1, -1);
	if (!retval) {
		yaffs2_total_objects++;
		retval = parse_directory(YAFFS_OBJECTID_ROOT, dirpath);
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

	free(yaffs2_data_buffer);
	free(yaffs2_object_list);
	close(yaffs2_outfd);

	return retval;
}
