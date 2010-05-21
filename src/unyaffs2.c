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
 * unyaffs2.c
 *
 * Extract a YAFFS2 file image made by the mkyaffs2 tool. 
 
 * Luen-Yung Lin <penguin.lin@gmail.com>
 */

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __HAVE_MMAP
#include <sys/mman.h>
#endif

#include "yaffs_packedtags1.h"
#include "yaffs_packedtags2.h"
#include "yaffs_tagsvalidity.h"

#include "yaffs2utils.h"
#include "yaffs2utils_io.h"
#include "yaffs2utils_endian.h"

/*-------------------------------------------------------------------------*/

unsigned yaffs_traceMask = 0;

/*-------------------------------------------------------------------------*/

#define DEFAULT_CHUNK_SIZE	2048
#define DEFAULT_OBJECT_NUMBERS	65536

/*-------------------------------------------------------------------------*/

typedef struct object_item {
	unsigned object;
	unsigned parent;
	char name[NAME_MAX + 1];
} object_item_t;

/*-------------------------------------------------------------------------*/

static unsigned yaffs2_chunk_size = 0;
static unsigned yaffs2_spare_size = 0;

static unsigned yaffs2_object_list_size = DEFAULT_OBJECT_NUMBERS;
static object_item_t *yaffs2_object_list = 0;

static unsigned yaffs2_total_objects = 0;

static unsigned yaffs2_convert_endian = 0;

static int (*oobfree_info)[2] = 0;

#ifndef __HAVE_MMAP
static unsigned char *yaffs2_data_buffer = NULL;
#endif

/*-------------------------------------------------------------------------*/

static int 
object_list_compare (const void *a, const void * b)
{
	object_item_t *oa, *ob;

	oa = (object_item_t *)a;
	ob = (object_item_t *)b;

	if (oa->object < ob->object)	return -1;
	if (oa->object > ob->object)	return 1;

	return 0;
}

static int 
object_list_add (object_item_t *object)
{
	object_item_t *r = NULL;

	if (yaffs2_total_objects > 0 &&
	    (r = bsearch(object, yaffs2_object_list, yaffs2_total_objects,
		         sizeof(object_item_t), object_list_compare)) != NULL)
	{
		/* update entry if it has been added */
		r->parent = object->parent;
		strncpy(r->name, object->name, NAME_MAX);
		return 0;
	}

	if (yaffs2_total_objects >= yaffs2_object_list_size) {
		size_t newsize;
		object_item_t *newlist;

		if (yaffs2_object_list_size * 2 < YAFFS_UNUSED_OBJECT_ID) {
			yaffs2_object_list_size *= 2;
		}
		else if (yaffs2_object_list_size < YAFFS_UNUSED_OBJECT_ID) {
			yaffs2_object_list_size = YAFFS_UNUSED_OBJECT_ID;
		}
		else {
			fprintf(stderr, "too much objects (max: %u)\n", 
				YAFFS_UNUSED_OBJECT_ID);
			return -1;
		}

		newsize = sizeof(object_item_t) * yaffs2_object_list_size;
		newlist = realloc(yaffs2_object_list, newsize);
		if (newlist == NULL) {
			fprintf(stderr, "cannot allocate objects list ");
			fprintf(stderr, "(%u objects array, %u bytes)\n",
				yaffs2_object_list_size, newsize);
			return -1;
		}
		yaffs2_object_list = newlist;
	}

	yaffs2_object_list[yaffs2_total_objects].object = object->object;
	yaffs2_object_list[yaffs2_total_objects].parent = object->parent;
	strncpy(yaffs2_object_list[yaffs2_total_objects].name, object->name, NAME_MAX);

	qsort(yaffs2_object_list, ++yaffs2_total_objects, 
	      sizeof(object_item_t), object_list_compare);

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
		object->parent = r->parent;
		strncpy(object->name, r->name, NAME_MAX);

		return 0;
	}

	return -1;
}

/*-------------------------------------------------------------------------*/

static void
format_filepath (char *path, size_t size, unsigned id)
{
	size_t pathlen;
	object_item_t obj = {id, 0, {0}};

	object_list_search(&obj);

	if (obj.object == YAFFS_OBJECTID_ROOT) {
		strncpy(path, strlen(obj.name) ? obj.name : ".", size - 1);
		return;
	}

	pathlen = strlen(path);
	format_filepath(path, size, obj.parent);

	if (pathlen < strlen(path)) {
		strncat(path, "/", size - strlen(path) - 1);
	}
	strncat(path, obj.name, size - strlen(path) - 1);
}

static int
create_directory (const char *name, const mode_t mode)
{
	int ret = 0;
	mode_t pmask;
	struct stat statbuf;

	pmask = umask(000);

	if (access(name, F_OK) < 0) {
		if (mkdir(name, mode) < 0 ||
		    access(name, F_OK) < 0)
		{
			ret = -1;
		}
	}
	else if (stat(name, &statbuf) < 0 ||
		 !S_ISDIR(statbuf.st_mode) ||
		 chmod(name, mode) < 0)
	{
		ret = -1;
	}

	umask(pmask);

	return ret;
}

/*-------------------------------------------------------------------------*/

static ssize_t
spare2tags (unsigned char *tags, unsigned char *spare, size_t bytes)
{
	unsigned i;
	size_t copied = 0;

	for (i = 0; i < 8 && copied < bytes; i++) {
		size_t size = bytes - copied;
		unsigned char *s = spare + oobfree_info[i][0];

		if (size > oobfree_info[i][1]) {
			size = oobfree_info[i][1];
		}

		memcpy(tags, s, size);
		if (memcmp(tags, s, size)) {
			return -1;
		}

		copied += size;
		tags += size;
	}

	return copied;
}

/*-------------------------------------------------------------------------*/

#ifndef __HAVE_MMAP

static int
extract_file (const int fd, const char *fpath, yaffs_ObjectHeader *oh)
{
	int outfd, remain = oh->fileSize;
	size_t bufsize = yaffs2_chunk_size + yaffs2_spare_size;
	ssize_t reads, written;

	yaffs_ExtendedTags t;
	yaffs_PackedTags1 pt1;
	yaffs_PackedTags2 pt2;

	outfd = open(fpath, O_CREAT | O_TRUNC | O_WRONLY, oh->yst_mode);
	if (outfd < 0) {
		fprintf(stderr, "cannot create file %s\n", fpath);
		return -1;
	}

	while (remain > 0 &&
	       (reads = safe_read(fd, yaffs2_data_buffer, bufsize)) !=0)
	{
		if (reads < 0 || reads != bufsize) {
			fprintf(stderr, "error while reading file %s\n", fpath);
			return -1;
		}

		if (yaffs2_chunk_size > 512) {
			memset(&pt2, 0xFF, sizeof(yaffs_PackedTags2));
			spare2tags((unsigned char *)&pt2,
				   yaffs2_data_buffer + yaffs2_chunk_size,
				   sizeof(yaffs_PackedTags2));

			if (yaffs2_convert_endian) {
				packedtags2_tagspart_endian_transform(&pt2);
			}

			yaffs_UnpackTags2TagsPart(&t, &pt2.t);
		}
		else {
			memset(&pt1, 0xFF, sizeof(yaffs_PackedTags1));
			spare2tags((unsigned char *)&pt1,
				   yaffs2_data_buffer + yaffs2_chunk_size,
				   sizeof(yaffs_PackedTags1));

			if (yaffs2_convert_endian) {
				packedtags1_endian_transform(&pt1, 1);
			}

			yaffs_UnpackTags1(&t, &pt1);
		}

		written = safe_write(outfd, yaffs2_data_buffer, t.byteCount);
		if (written != t.byteCount) {
			fprintf(stderr, "error while writing file %s", fpath);
			return -1;
		}

		remain -= written;
	}

	close(outfd);
	return !(remain == 0);
}

#else

static int
extract_file_mmap (unsigned char **addr,
		   size_t *size,
		   const char *fpath,
		   yaffs_ObjectHeader *oh)
{
	int outfd, remain = oh->fileSize;
	size_t bufsize = yaffs2_chunk_size + yaffs2_spare_size;
	ssize_t written;
	
	yaffs_ExtendedTags t;
	yaffs_PackedTags1 pt1;
	yaffs_PackedTags2 pt2;

	outfd = open(fpath, O_CREAT | O_TRUNC | O_WRONLY, oh->yst_mode);
	if (outfd < 0) {
		fprintf(stderr, "cannot create file %s\n", fpath);
		return -1;
	}

	while (remain > 0 && *size >= bufsize) {
		if (yaffs2_chunk_size > 512) {
			memset(&pt2, 0xFF, sizeof(yaffs_PackedTags2));
			spare2tags((unsigned char *)&pt2,
				   *addr + yaffs2_chunk_size,
				   sizeof(yaffs_PackedTags2));

			if (yaffs2_convert_endian) {
				packedtags2_tagspart_endian_transform(&pt2);
			}

			yaffs_UnpackTags2TagsPart(&t, &pt2.t);
		}
		else {
			memset(&pt1, 0xFF, sizeof(yaffs_PackedTags1));
			spare2tags((unsigned char *)&pt1,
				   *addr + yaffs2_chunk_size,
				   sizeof(yaffs_PackedTags1));

			if (yaffs2_convert_endian) {
				packedtags1_endian_transform(&pt1, 1);
			}

			yaffs_UnpackTags1(&t, &pt1);
		}

		written = safe_write(outfd, *addr, t.byteCount);
		if (written != t.byteCount) {
			fprintf(stderr, "error while writing file %s", fpath);
			close(outfd);
			return -1;
		}

		remain -= written;
		*addr += bufsize;
		*size -= bufsize;
	}

	close(outfd);
	return !(remain == 0);
}

#endif

/*-------------------------------------------------------------------------*/

#ifndef __HAVE_MMAP

static int 
extract_image (const int fd)
{
	size_t bufsize = yaffs2_chunk_size + yaffs2_spare_size;
	ssize_t reads;

	while ((reads = safe_read(fd, yaffs2_data_buffer, bufsize)) != 0) {
		yaffs_ExtendedTags t;
		yaffs_PackedTags1 pt1;
		yaffs_PackedTags2 pt2;

		if (reads < 0 || reads != bufsize) {
			return -1;
		}

		if (yaffs2_chunk_size > 512) {
			memset(&pt2, 0xFF, sizeof(yaffs_PackedTags2));
			spare2tags((unsigned char *)&pt2,
				   yaffs2_data_buffer + yaffs2_chunk_size,
				   sizeof(yaffs_PackedTags2));

			if (yaffs2_convert_endian) {
				packedtags2_tagspart_endian_transform(&pt2);
			}

			yaffs_UnpackTags2TagsPart(&t, &pt2.t);
		}
		else {
			memset(&pt1, 0xFF, sizeof(yaffs_PackedTags1));
			spare2tags((unsigned char *)&pt1,
				   yaffs2_data_buffer + yaffs2_chunk_size,
				   sizeof(yaffs_PackedTags1));

			if (yaffs2_convert_endian) {
				packedtags1_endian_transform(&pt1, 1);
			}

			yaffs_UnpackTags1(&t, &pt1);
		}

		/* new object */
		if (t.chunkId == 0) {
			int retval = -1;
			char filepath[PATH_MAX] = {0}, linkpath[PATH_MAX] ={0};
			yaffs_ObjectHeader oh;
			object_item_t obj;

			memcpy(&oh, yaffs2_data_buffer, sizeof(yaffs_ObjectHeader));
			if (yaffs2_convert_endian) {
				object_header_endian_transform(&oh);
			}

			/* add object into object list */
			obj.object = t.objectId;
			obj.parent = oh.parentObjectId;
			strncpy(obj.name, oh.name, NAME_MAX);

			if (strlen(obj.name) == 0 && 
			    obj.object != YAFFS_OBJECTID_ROOT) 
			{
				fprintf(stderr, "skipping object %u ",
					obj.object);
				fprintf(stderr, "(empty filename)\n");
				continue;
			}

			retval = object_list_add(&obj);
			if (retval) {
				fprintf(stderr, "error while adding object ");
				fprintf(stderr, "%u into the objects list\n",
                                        obj.object);
				return -1;
			}
			format_filepath(filepath, PATH_MAX, obj.object);

			switch (oh.type) {
			case YAFFS_OBJECT_TYPE_FILE:
				printf("create file: %s\n", filepath);
				retval = extract_file(fd, filepath, &oh);
				break;
			case YAFFS_OBJECT_TYPE_DIRECTORY:
				printf("create directory %s\n", filepath);
				retval = create_directory(filepath,
							  oh.yst_mode);
				break;
			case YAFFS_OBJECT_TYPE_SYMLINK:
				printf("create symlink: %s\n", filepath);
				retval = symlink(oh.alias, filepath);
				break;
			case YAFFS_OBJECT_TYPE_HARDLINK:
				printf("create hardlink: %s\n", filepath);
				format_filepath(linkpath, PATH_MAX,
						oh.equivalentObjectId);
				retval = link(linkpath, filepath);
				break;
			case YAFFS_OBJECT_TYPE_SPECIAL:
				if (S_ISBLK(oh.yst_mode) ||
				    S_ISCHR(oh.yst_mode) ||
				    S_ISFIFO(oh.yst_mode) ||
				    S_ISSOCK(oh.yst_mode))
				{
					printf("create dev node: %s\n",
					       filepath);
					retval = mknod(filepath, oh.yst_mode,
						       oh.yst_rdev);
				}
				break;
			default:
				retval = -1;
				fprintf(stderr, "unsupported object type %u\n", oh.type);
				break;
			}

			if (retval) {
				fprintf(stderr, "error while extracting %s\n",
					filepath);
			}
		}
	}

	return 0;
}

#else

static int
extract_image_mmap (unsigned char *addr, size_t size)
{
	size_t bufsize = yaffs2_chunk_size + yaffs2_spare_size;

	while (size >= bufsize) {
		yaffs_ExtendedTags t;
		yaffs_PackedTags1 pt1;
		yaffs_PackedTags2 pt2;

		if (yaffs2_chunk_size > 512) {
			memset(&pt2, 0xFF, sizeof(yaffs_PackedTags2));
			spare2tags((unsigned char *)&pt2,
				   addr + yaffs2_chunk_size,
				   sizeof(yaffs_PackedTags2));

			if (yaffs2_convert_endian) {
				packedtags2_tagspart_endian_transform(&pt2);
			}

			yaffs_UnpackTags2TagsPart(&t, &pt2.t);
		}
		else {
			memset(&pt1, 0xFF, sizeof(yaffs_PackedTags1));
			spare2tags((unsigned char *)&pt1,
				   addr + yaffs2_chunk_size,
				   sizeof(yaffs_PackedTags1));

			if (yaffs2_convert_endian) {
				packedtags1_endian_transform(&pt1, 1);
			}

			yaffs_UnpackTags1(&t, &pt1);
		}

		/* new object */
		if (t.chunkId == 0) {
			int retval = -1;
			char filepath[PATH_MAX] = {0}, linkpath[PATH_MAX] = {0};
			yaffs_ObjectHeader oh;
			object_item_t obj;

			memcpy(&oh, addr, sizeof(yaffs_ObjectHeader));
			if (yaffs2_convert_endian) {
				object_header_endian_transform(&oh);
			}

			/* add object into object list */
			obj.object = t.objectId;
			obj.parent = oh.parentObjectId;
			strncpy(obj.name, oh.name, NAME_MAX);

			if (strlen(obj.name) == 0 &&
			    obj.object != YAFFS_OBJECTID_ROOT) 
			{
				fprintf(stderr, "skipping object %u ",
					obj.object);
				fprintf(stderr, "(empty filename)\n");
				goto next;
			}

			retval = object_list_add(&obj);
			if (retval) {
				fprintf(stderr, "error while adding object ");
				fprintf(stderr, "%u into the objects list\n",
                                        obj.object);
				return -1;
			}

			format_filepath(filepath, PATH_MAX, obj.object);

			switch (oh.type) {
			case YAFFS_OBJECT_TYPE_FILE:
				printf("create file: %s\n", filepath);
				addr += bufsize;
				size -= bufsize;
				retval = extract_file_mmap(&addr, &size,
							   filepath, &oh);
				break;
			case YAFFS_OBJECT_TYPE_DIRECTORY:
				printf("create directory %s\n", filepath);
				retval = create_directory(filepath,
							  oh.yst_mode);
				break;
			case YAFFS_OBJECT_TYPE_SYMLINK:
				printf("create symlink: %s\n", filepath);
				retval = symlink(oh.alias, filepath);
				break;
			case YAFFS_OBJECT_TYPE_HARDLINK:
				printf("create hardlink: %s\n", filepath);
				format_filepath(linkpath, PATH_MAX,
						oh.equivalentObjectId);
				retval = link(linkpath, filepath);
				break;
			case YAFFS_OBJECT_TYPE_SPECIAL:
				if (S_ISBLK(oh.yst_mode) ||
				    S_ISCHR(oh.yst_mode) ||
				    S_ISFIFO(oh.yst_mode) ||
				    S_ISSOCK(oh.yst_mode))
				{
					printf("create dev node: %s\n",
					       filepath);
					retval = mknod(filepath, oh.yst_mode,
						       oh.yst_rdev);
				}
				break;
			default:
				retval = -1;
				fprintf(stderr, "unsupported object type %u\n",
					oh.type);
				break;
			}

			if (retval) {
				fprintf(stderr, "error while extracting %s\n",
					filepath);
			}

			if (oh.type == YAFFS_OBJECT_TYPE_FILE) {
				continue;
			}
		}

next:
		addr += bufsize;
		size -= bufsize;
	}

	return 0;
}

#endif

/*-------------------------------------------------------------------------*/

static void
show_usage (void)
{
	fprintf(stderr, "Usage: unyaffs2 [-h] [-p pagesize] imgfile dirname\n");
	fprintf(stderr, "unyaffs2: A utility to extract the yaffs2 image\n");
	fprintf(stderr, "version: %s\n", YAFFS2PROGS_VERSION);
	fprintf(stderr, "options:\n");
	fprintf(stderr, "	-h		display this help message and exit\n");
	fprintf(stderr, "	-e		convert the endian differed from the local machine\n");
	fprintf(stderr, "	-p pagesize	page size (512|2048, default: %u)\n", DEFAULT_CHUNK_SIZE);
	fprintf(stderr, "			512 bytes page size will use the yaffs1 format\n");
}

/*-------------------------------------------------------------------------*/

int
main (int argc, char* argv[])
{
	int retval, objsize, fd;
	char *input_path, *output_path;
	struct stat statbuf;
#ifdef __HAVE_MMAP
	void *addr;
#endif

	int option, option_index;
	static const char *short_options = "hep:";
	static const struct option long_options[] = {
		{"pagesize",	required_argument, 	0, 'p'},
		{"endian",	no_argument, 		0, 'e'},
		{"help",	no_argument, 		0, 'h'},
	};

	printf("unyaffs2-%s: image extracting tool for YAFFS2\n",
		YAFFS2PROGS_VERSION);

	yaffs2_chunk_size = DEFAULT_CHUNK_SIZE;

	while ((option = getopt_long(argc, argv, short_options,
				     long_options, &option_index)) != EOF) 
	{
		switch (option) {
		case 'p':
			yaffs2_chunk_size = strtol(optarg, NULL, 10);
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
		oobfree_info = (int (*)[2])nand_oobfree_16;
		break;
	case 2048:
		oobfree_info = (int (*)[2])nand_oobfree_64;
		break;
	default:
		fprintf(stderr, "%u bytes page size is not supported\n",
			yaffs2_chunk_size);
		return -1;
	}

	/* spare size */
	yaffs2_spare_size = yaffs2_chunk_size / 32;

	/* verify whether the input image is valid */
	if (stat(input_path, &statbuf) < 0 &&
	    !S_ISREG(statbuf.st_mode))
	{
		fprintf(stderr, "%s is not a regular file\n", input_path);
		return -1;
	}

	if ((statbuf.st_size % (yaffs2_chunk_size + yaffs2_spare_size)) != 0) {
		fprintf(stderr, "image size is NOT a mutiple of %u + %u\n",
			yaffs2_chunk_size, yaffs2_spare_size);
		return -1;
	}

	/* verify whether the output image is valid */
	if (create_directory(output_path, 0755) < 0) {
		fprintf(stderr, "cannot create the directory %s (permission?)",
			output_path);
		return -1;
	}

	objsize = sizeof(object_item_t) * yaffs2_object_list_size;
	yaffs2_object_list = (object_item_t *)malloc(objsize);
	if (yaffs2_object_list == NULL) {
		fprintf(stderr, "cannot allocate objects list ");
		fprintf(stderr, "(default: %u objects, %u bytes)\n",
			yaffs2_object_list_size,
			yaffs2_object_list_size * sizeof(object_item_t));
		return -1;
	}

	fd = open(input_path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "cannot open the image file %s\n", input_path);
		free(yaffs2_object_list);
		return -1;
	}

	chdir(output_path);
	printf("extracting image to \"%s\"\n", output_path);
#ifndef __HAVE_MMAP
	yaffs2_data_buffer = (unsigned char *)malloc(yaffs2_chunk_size +
						     yaffs2_spare_size);
	if (yaffs2_data_buffer == NULL) {
		fprintf(stderr, "cannot allocate working buffer ");
		fprintf(stderr, "(default: %u bytes)\n",
			yaffs2_chunk_size + yaffs2_spare_size);
		free(yaffs2_object_list);
		return -1;
	}

	retval = extract_image(fd);

	free(yaffs2_data_buffer);
#else
	addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == NULL) {
		fprintf(stderr, "error while mapping the image\n");
		free(yaffs2_object_list);
		return -1;
	}

	retval = extract_image_mmap((unsigned char *)addr, statbuf.st_size);

	munmap(addr, statbuf.st_size);
#endif
	if (retval) {
		fprintf(stderr, "operation incomplete!\n");
	}
	else {
		printf("operation complete.\ntotal %u objects\n",
			yaffs2_total_objects);
	}

	free(yaffs2_object_list);
	close(fd);

	return retval;
}
