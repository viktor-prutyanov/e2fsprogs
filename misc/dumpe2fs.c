/*
 * dumpe2fs.c		- List the control structures of a second
 *			  extended filesystem
 *
 * Copyright (C) 1992, 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                                 Laboratoire MASI, Institut Blaise Pascal
 *                                 Universite Pierre et Marie Curie (Paris VI)
 *
 * Copyright 1995, 1996, 1997 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

/*
 * History:
 * 94/01/09	- Creation
 * 94/02/27	- Ported to use the ext2fs library
 */

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ext2fs/ext2_fs.h"

#include "ext2fs/ext2fs.h"
#include "e2p/e2p.h"
#include "ext2fs/kernel-jbd.h"
#include <uuid/uuid.h>

#include "support/nls-enable.h"
#include "support/plausible.h"
#ifdef CONFIG_JSON
#include "support/json-out.h"
#endif
#include "../version.h"

#define in_use(m, x)	(ext2fs_test_bit ((x), (m)))

static const char * program_name = "dumpe2fs";
static char * device_name = NULL;
static int hex_format = 0;
static int blocks64 = 0;
#ifdef CONFIG_JSON
#define OPTIONS "bfghixjV"
#else
#define OPTIONS "bfghixV"
#endif

static void usage(void)
{
	fprintf(stderr, _("Usage: %s [-"OPTIONS"] [-o superblock=<num>] "
		 "[-o blocksize=<num>] device\n"), program_name);
	exit(1);
}

static void print_number(unsigned long long num)
{
	if (hex_format) {
		if (blocks64)
			printf("0x%08llx", num);
		else
			printf("0x%04llx", num);
	} else
		printf("%llu", num);
}

#ifdef CONFIG_JSON
static void snprint_number(char *str, size_t size, unsigned long long num)
{
	if (hex_format) {
		if (blocks64)
			snprintf(str, size, "0x%08llx", num);
		else
			snprintf(str, size, "0x%04llx", num);
	} else
		snprintf(str, size, "%llu", num);
}
#endif

static void print_range(unsigned long long a, unsigned long long b)
{
	if (hex_format) {
		if (blocks64)
			printf("0x%08llx-0x%08llx", a, b);
		else
			printf("0x%04llx-0x%04llx", a, b);
	} else
		printf("%llu-%llu", a, b);
}

#ifdef CONFIG_JSON
static struct json_obj *json_create_range_obj(unsigned long long a,
		       unsigned long long b)
{
	struct json_obj *obj = json_obj_create();
	char buf[32];
	const char *fmt = hex_format ? (blocks64 ? "0x%08llx" : "0x%04llx") : "%llu";

	json_obj_add_fmt_buf_str(obj, "start", buf, sizeof(buf), fmt, a);
	json_obj_add_fmt_buf_str(obj, "len", buf, sizeof(buf), fmt, b - a + 1);

	return obj;
}
#endif

static void print_free(unsigned long group, char * bitmap,
		       unsigned long num, unsigned long offset, int ratio)
{
	int p = 0;
	unsigned long i;
	unsigned long j;

	offset /= ratio;
	offset += group * num;
	for (i = 0; i < num; i++)
		if (!in_use (bitmap, i))
		{
			if (p)
				printf (", ");
			print_number((i + offset) * ratio);
			for (j = i; j < num && !in_use (bitmap, j); j++)
				;
			if (--j != i) {
				fputc('-', stdout);
				print_number((j + offset) * ratio);
				i = j;
			}
			p = 1;
		}
}

#ifdef CONFIG_JSON
static void fill_json_free(struct json_list *list, unsigned long group,
		     char *bitmap, unsigned long num, unsigned long offset, int ratio)
{
	unsigned long i;
	unsigned long j;
	unsigned long long a, b;

	offset /= ratio;
	offset += group * num;
	for (i = 0; i < num; i++)
		if (!in_use (bitmap, i))
		{
			for (j = i; j < num && !in_use (bitmap, j); j++)
				;
			if (--j == i)
				a = b = (i + offset) * ratio;
			else {
				a = (i + offset) * ratio;
				b = (j + offset) * ratio;
				i = j;
			}
			json_list_add_obj(list, json_create_range_obj(a, b));
		}
}
#endif

static void print_bg_opt(int bg_flags, int mask,
			  const char *str, int *first)
{
	if (bg_flags & mask) {
		if (*first) {
			fputs(" [", stdout);
			*first = 0;
		} else
			fputs(", ", stdout);
		fputs(str, stdout);
	}
}
static void print_bg_opts(ext2_filsys fs, dgrp_t i)
{
	int first = 1, bg_flags = 0;

	if (ext2fs_has_group_desc_csum(fs))
		bg_flags = ext2fs_bg_flags(fs, i);

	print_bg_opt(bg_flags, EXT2_BG_INODE_UNINIT, "INODE_UNINIT",
 		     &first);
	print_bg_opt(bg_flags, EXT2_BG_BLOCK_UNINIT, "BLOCK_UNINIT",
 		     &first);
	print_bg_opt(bg_flags, EXT2_BG_INODE_ZEROED, "ITABLE_ZEROED",
 		     &first);
	if (!first)
		fputc(']', stdout);
	fputc('\n', stdout);
}

#ifdef CONFIG_JSON
static void fill_json_bg_opts(struct json_obj *obj, ext2_filsys fs, dgrp_t i)
{
	int bg_flags = 0;
	struct json_list *bg_opts_list = json_list_create_in_obj(obj, "bg-opts",
					JSON_VAL_STRING);

	if (ext2fs_has_group_desc_csum(fs))
		bg_flags = ext2fs_bg_flags(fs, i);
	else
		return;

	if (bg_flags & EXT2_BG_INODE_UNINIT)
		json_list_add_str(bg_opts_list, "INODE_UNINIT");
	if (bg_flags & EXT2_BG_BLOCK_UNINIT)
		json_list_add_str(bg_opts_list, "BLOCK_UNINIT");
	if (bg_flags & EXT2_BG_INODE_ZEROED)
		json_list_add_str(bg_opts_list, "ITABLE_ZEROED");
}
#endif

static void print_bg_rel_offset(ext2_filsys fs, blk64_t block, int itable,
				blk64_t first_block, blk64_t last_block)
{
	if ((block >= first_block) && (block <= last_block)) {
		if (itable && block == first_block)
			return;
		printf(" (+%u)", (unsigned)(block - first_block));
	} else if (ext2fs_has_feature_flex_bg(fs->super)) {
		dgrp_t flex_grp = ext2fs_group_of_blk2(fs, block);
		printf(" (bg #%u + %u)", flex_grp,
		       (unsigned)(block-ext2fs_group_first_block2(fs,flex_grp)));
	}
}

#ifdef CONFIG_JSON
static struct json_obj* json_create_bg_rel_offset_obj(ext2_filsys fs,
			blk64_t block, int itable, blk64_t first_block, blk64_t last_block)
{
	struct json_obj *obj = json_obj_create();
	char buf[32];

	if ((block >= first_block) && (block <= last_block)) {
		if (itable && block == first_block)
			return obj;
		snprintf(buf, sizeof(buf), "%u", (unsigned)(block - first_block));
		json_obj_add_str(obj, "offset", buf);
	} else if (ext2fs_has_feature_flex_bg(fs->super)) {
		dgrp_t flex_grp = ext2fs_group_of_blk2(fs, block);
		snprintf(buf, sizeof(buf), "%u", flex_grp);
		json_obj_add_str(obj, "bg", buf);
		snprintf(buf, sizeof(buf), "%u",
		         (unsigned)(block-ext2fs_group_first_block2(fs,flex_grp)));
		json_obj_add_str(obj, "offset", buf);
	}

	return obj;
}
#endif

static void list_desc(ext2_filsys fs, int grp_only)
{
	unsigned long i;
	blk64_t	first_block, last_block;
	blk64_t	super_blk, old_desc_blk, new_desc_blk;
	char *block_bitmap=NULL, *inode_bitmap=NULL;
	const char *units = _("blocks");
	int inode_blocks_per_group, old_desc_blocks, reserved_gdt;
	int		block_nbytes, inode_nbytes;
	int has_super;
	blk64_t		blk_itr = EXT2FS_B2C(fs, fs->super->s_first_data_block);
	ext2_ino_t	ino_itr = 1;
	errcode_t	retval;

	if (ext2fs_has_feature_bigalloc(fs->super))
		units = _("clusters");

	block_nbytes = EXT2_CLUSTERS_PER_GROUP(fs->super) / 8;
	inode_nbytes = EXT2_INODES_PER_GROUP(fs->super) / 8;

	if (fs->block_map)
		block_bitmap = malloc(block_nbytes);
	if (fs->inode_map)
		inode_bitmap = malloc(inode_nbytes);

	inode_blocks_per_group = ((fs->super->s_inodes_per_group *
				   EXT2_INODE_SIZE(fs->super)) +
				  EXT2_BLOCK_SIZE(fs->super) - 1) /
				 EXT2_BLOCK_SIZE(fs->super);
	reserved_gdt = fs->super->s_reserved_gdt_blocks;
	fputc('\n', stdout);
	first_block = fs->super->s_first_data_block;
	if (ext2fs_has_feature_meta_bg(fs->super))
		old_desc_blocks = fs->super->s_first_meta_bg;
	else
		old_desc_blocks = fs->desc_blocks;
	if (grp_only)
		printf("group:block:super:gdt:bbitmap:ibitmap:itable\n");
	for (i = 0; i < fs->group_desc_count; i++) {
		first_block = ext2fs_group_first_block2(fs, i);
		last_block = ext2fs_group_last_block2(fs, i);

		ext2fs_super_and_bgd_loc2(fs, i, &super_blk,
					  &old_desc_blk, &new_desc_blk, 0);

		if (grp_only) {
			printf("%lu:%llu:", i, first_block);
			if (i == 0 || super_blk)
				printf("%llu:", super_blk);
			else
				printf("-1:");
			if (old_desc_blk) {
				print_range(old_desc_blk,
					    old_desc_blk + old_desc_blocks - 1);
				printf(":");
			} else if (new_desc_blk)
				printf("%llu:", new_desc_blk);
			else
				printf("-1:");
			printf("%llu:%llu:%llu\n",
			       ext2fs_block_bitmap_loc(fs, i),
			       ext2fs_inode_bitmap_loc(fs, i),
			       ext2fs_inode_table_loc(fs, i));
			continue;
		}

		printf(_("Group %lu: (Blocks "), i);
		print_range(first_block, last_block);
		fputs(")", stdout);
		if (ext2fs_has_group_desc_csum(fs)) {
			unsigned csum = ext2fs_bg_checksum(fs, i);
			unsigned exp_csum = ext2fs_group_desc_csum(fs, i);

			printf(_(" csum 0x%04x"), csum);
			if (csum != exp_csum)
				printf(_(" (EXPECTED 0x%04x)"), exp_csum);
		}
		print_bg_opts(fs, i);
		has_super = ((i==0) || super_blk);
		if (has_super) {
			printf (_("  %s superblock at "),
				i == 0 ? _("Primary") : _("Backup"));
			print_number(super_blk);
		}
		if (old_desc_blk) {
			printf("%s", _(", Group descriptors at "));
			print_range(old_desc_blk,
				    old_desc_blk + old_desc_blocks - 1);
			if (reserved_gdt) {
				printf("%s", _("\n  Reserved GDT blocks at "));
				print_range(old_desc_blk + old_desc_blocks,
					    old_desc_blk + old_desc_blocks +
					    reserved_gdt - 1);
			}
		} else if (new_desc_blk) {
			fputc(has_super ? ',' : ' ', stdout);
			printf("%s", _(" Group descriptor at "));
			print_number(new_desc_blk);
			has_super++;
		}
		if (has_super)
			fputc('\n', stdout);
		fputs(_("  Block bitmap at "), stdout);
		print_number(ext2fs_block_bitmap_loc(fs, i));
		print_bg_rel_offset(fs, ext2fs_block_bitmap_loc(fs, i), 0,
				    first_block, last_block);
		if (ext2fs_has_feature_metadata_csum(fs->super))
			printf(_(", csum 0x%08x"),
			       ext2fs_block_bitmap_checksum(fs, i));
		if (getenv("DUMPE2FS_IGNORE_80COL"))
			fputs(_(","), stdout);
		else
			fputs(_("\n "), stdout);
		fputs(_(" Inode bitmap at "), stdout);
		print_number(ext2fs_inode_bitmap_loc(fs, i));
		print_bg_rel_offset(fs, ext2fs_inode_bitmap_loc(fs, i), 0,
				    first_block, last_block);
		if (ext2fs_has_feature_metadata_csum(fs->super))
			printf(_(", csum 0x%08x"),
			       ext2fs_inode_bitmap_checksum(fs, i));
		fputs(_("\n  Inode table at "), stdout);
		print_range(ext2fs_inode_table_loc(fs, i),
			    ext2fs_inode_table_loc(fs, i) +
			    inode_blocks_per_group - 1);
		print_bg_rel_offset(fs, ext2fs_inode_table_loc(fs, i), 1,
				    first_block, last_block);
		printf (_("\n  %u free %s, %u free inodes, "
			  "%u directories%s"),
			ext2fs_bg_free_blocks_count(fs, i), units,
			ext2fs_bg_free_inodes_count(fs, i),
			ext2fs_bg_used_dirs_count(fs, i),
			ext2fs_bg_itable_unused(fs, i) ? "" : "\n");
		if (ext2fs_bg_itable_unused(fs, i))
			printf (_(", %u unused inodes\n"),
				ext2fs_bg_itable_unused(fs, i));
		if (block_bitmap) {
			fputs(_("  Free blocks: "), stdout);
			retval = ext2fs_get_block_bitmap_range2(fs->block_map,
				 blk_itr, block_nbytes << 3, block_bitmap);
			if (retval)
				com_err("list_desc", retval,
					"while reading block bitmap");
			else
				print_free(i, block_bitmap,
					   fs->super->s_clusters_per_group,
					   fs->super->s_first_data_block,
					   EXT2FS_CLUSTER_RATIO(fs));
			fputc('\n', stdout);
			blk_itr += fs->super->s_clusters_per_group;
		}
		if (inode_bitmap) {
			fputs(_("  Free inodes: "), stdout);
			retval = ext2fs_get_inode_bitmap_range2(fs->inode_map,
				 ino_itr, inode_nbytes << 3, inode_bitmap);
			if (retval)
				com_err("list_desc", retval,
					"while reading inode bitmap");
			else
				print_free(i, inode_bitmap,
					   fs->super->s_inodes_per_group,
					   1, 1);
			fputc('\n', stdout);
			ino_itr += fs->super->s_inodes_per_group;
		}
	}
	if (block_bitmap)
		free(block_bitmap);
	if (inode_bitmap)
		free(inode_bitmap);
}

#ifdef CONFIG_JSON
static void fill_json_desc(struct json_obj *obj, ext2_filsys fs)
{
	unsigned long i;
	blk64_t	first_block, last_block;
	blk64_t	super_blk, old_desc_blk, new_desc_blk;
	char *block_bitmap=NULL, *inode_bitmap=NULL;
	const char *units = "blocks";
	int inode_blocks_per_group, old_desc_blocks, reserved_gdt;
	int		block_nbytes, inode_nbytes;
	int has_super;
	blk64_t		blk_itr = EXT2FS_B2C(fs, fs->super->s_first_data_block);
	ext2_ino_t	ino_itr = 1;
	errcode_t	retval;
	struct json_list *desc_list = json_list_create_in_obj(obj, "desc", JSON_VAL_OBJECT);
	char buf[64];

	if (ext2fs_has_feature_bigalloc(fs->super))
		units = "clusters";

	block_nbytes = EXT2_CLUSTERS_PER_GROUP(fs->super) / 8;
	inode_nbytes = EXT2_INODES_PER_GROUP(fs->super) / 8;

	if (fs->block_map)
		block_bitmap = malloc(block_nbytes);
	if (fs->inode_map)
		inode_bitmap = malloc(inode_nbytes);
	inode_blocks_per_group = ((fs->super->s_inodes_per_group *
				   EXT2_INODE_SIZE(fs->super)) +
				  EXT2_BLOCK_SIZE(fs->super) - 1) /
				 EXT2_BLOCK_SIZE(fs->super);
	reserved_gdt = fs->super->s_reserved_gdt_blocks;
	first_block = fs->super->s_first_data_block;
	if (ext2fs_has_feature_meta_bg(fs->super))
		old_desc_blocks = fs->super->s_first_meta_bg;
	else
		old_desc_blocks = fs->desc_blocks;

	for (i = 0; i < fs->group_desc_count; i++) {
		struct json_obj *group_obj = json_obj_create();

		json_list_add_obj(desc_list, group_obj);

		first_block = ext2fs_group_first_block2(fs, i);
		last_block = ext2fs_group_last_block2(fs, i);

		ext2fs_super_and_bgd_loc2(fs, i, &super_blk,
					  &old_desc_blk, &new_desc_blk, 0);

		json_obj_add_fmt_buf_str(group_obj, "num", buf, sizeof(buf), "%lu", i);
		json_obj_add_obj(group_obj, "blocks",
						json_create_range_obj(first_block, last_block));
		if (ext2fs_has_group_desc_csum(fs)) {
			unsigned csum = ext2fs_bg_checksum(fs, i);
			unsigned exp_csum = ext2fs_group_desc_csum(fs, i);

			json_obj_add_fmt_buf_str(group_obj, "group-desc-csum",
							buf, sizeof(buf), "0x%04x", csum);
			if (csum != exp_csum)
				json_obj_add_fmt_buf_str(group_obj, "group-desc-csum-exp",
								buf, sizeof(buf), "0x%04x", exp_csum);
		}

		fill_json_bg_opts(group_obj, fs, i);
		has_super = ((i==0) || super_blk);
		if (has_super) {
			json_obj_add_str(group_obj, "superblock-type",
							i == 0 ? "Primary" : "Backup");
			snprint_number(buf, sizeof(buf), super_blk);
			json_obj_add_str(group_obj, "superblock-at", buf);
		}
		if (old_desc_blk) {
			json_obj_add_obj(group_obj, "group-descriptors-at",
				    json_create_range_obj(old_desc_blk,
					    old_desc_blk + old_desc_blocks - 1));
			if (reserved_gdt) {
				json_obj_add_obj(group_obj, "reserved-gdt-blocks-at",
				    json_create_range_obj(old_desc_blk + old_desc_blocks,
					    old_desc_blk + old_desc_blocks + reserved_gdt - 1));
			}
		} else if (new_desc_blk) {
			snprint_number(buf, sizeof(buf), new_desc_blk);
			json_obj_add_str(group_obj, "group-desc-at", buf);
			has_super++;
		}

		snprint_number(buf, sizeof(buf), ext2fs_block_bitmap_loc(fs, i));
		json_obj_add_str(group_obj, "block-bitmap-at", buf);
		json_obj_add_obj(group_obj, "block-bitmap-rel-offset",
				    json_create_bg_rel_offset_obj(fs,
					    ext2fs_block_bitmap_loc(fs, i), 0,
					    first_block, last_block));
		if (ext2fs_has_feature_metadata_csum(fs->super))
			json_obj_add_fmt_buf_str(group_obj, "block-bitmap-csum", buf,
				sizeof(buf), "0x%08x", ext2fs_block_bitmap_checksum(fs, i));

		snprint_number(buf, sizeof(buf), ext2fs_inode_bitmap_loc(fs, i));
		json_obj_add_str(group_obj, "inode-bitmap-at", buf);
		json_obj_add_obj(group_obj, "inode-bitmap-rel-offset",
				    json_create_bg_rel_offset_obj(fs,
					    ext2fs_inode_bitmap_loc(fs, i), 0,
					    first_block, last_block));
		if (ext2fs_has_feature_metadata_csum(fs->super))
			json_obj_add_fmt_buf_str(group_obj, "inode-bitmap-csum", buf,
				sizeof(buf), "0x%08x", ext2fs_inode_bitmap_checksum(fs, i));

		json_obj_add_obj(group_obj, "inode-table-at",
			    json_create_range_obj(ext2fs_inode_table_loc(fs, i),
					    ext2fs_inode_table_loc(fs, i) +
					    inode_blocks_per_group - 1));

		json_obj_add_obj(group_obj, "inode-table-rel-offset",
			   json_create_bg_rel_offset_obj(fs,
				    ext2fs_inode_table_loc(fs, i), 1,
				    first_block, last_block));

		json_obj_add_fmt_buf_str(group_obj, "free-blocks-count", buf,
			sizeof(buf), "%u %s", ext2fs_bg_free_blocks_count(fs, i), units);
		json_obj_add_fmt_buf_str(group_obj, "free-inodes-count", buf,
			sizeof(buf), "%u", ext2fs_bg_free_inodes_count(fs, i));
		json_obj_add_fmt_buf_str(group_obj, "used-dirs-count", buf,
			sizeof(buf), "%u", ext2fs_bg_used_dirs_count(fs, i));
		json_obj_add_fmt_buf_str(group_obj, "unused-inodes", buf,
			sizeof(buf), "%u", ext2fs_bg_itable_unused(fs, i));
		if (block_bitmap) {
			struct json_list *free_blocks_list;

			free_blocks_list = json_list_create_in_obj(group_obj,
							"free-blocks", JSON_VAL_OBJECT);
			retval = ext2fs_get_block_bitmap_range2(fs->block_map,
				 blk_itr, block_nbytes << 3, block_bitmap);
			if (!retval)
				fill_json_free(free_blocks_list, i,
					   block_bitmap,
					   fs->super->s_clusters_per_group,
					   fs->super->s_first_data_block,
					   EXT2FS_CLUSTER_RATIO(fs));
			blk_itr += fs->super->s_clusters_per_group;
		}
		if (inode_bitmap) {
			struct json_list *free_inodes_list;

			free_inodes_list = json_list_create_in_obj(group_obj,
							"free-inodes", JSON_VAL_OBJECT);
			retval = ext2fs_get_inode_bitmap_range2(fs->inode_map,
				 ino_itr, inode_nbytes << 3, inode_bitmap);
			if (!retval)
				fill_json_free(free_inodes_list, i,
					   inode_bitmap,
					   fs->super->s_inodes_per_group,
					   1, 1);
			ino_itr += fs->super->s_inodes_per_group;
		}
	}
	if (block_bitmap)
		free(block_bitmap);
	if (inode_bitmap)
		free(inode_bitmap);
}
#endif

static void list_bad_blocks(ext2_filsys fs, int dump)
{
	badblocks_list		bb_list = 0;
	badblocks_iterate	bb_iter;
	blk_t			blk;
	errcode_t		retval;
	const char		*header, *fmt;

	retval = ext2fs_read_bb_inode(fs, &bb_list);
	if (retval) {
		com_err("ext2fs_read_bb_inode", retval, 0);
		return;
	}
	retval = ext2fs_badblocks_list_iterate_begin(bb_list, &bb_iter);
	if (retval) {
		com_err("ext2fs_badblocks_list_iterate_begin", retval,
			"%s", _("while printing bad block list"));
		return;
	}
	if (dump) {
		header = fmt = "%u\n";
	} else {
		header =  _("Bad blocks: %u");
		fmt = ", %u";
	}
	while (ext2fs_badblocks_list_iterate(bb_iter, &blk)) {
		printf(header ? header : fmt, blk);
		header = 0;
	}
	ext2fs_badblocks_list_iterate_end(bb_iter);
	if (!dump)
		fputc('\n', stdout);
	ext2fs_badblocks_list_free(bb_list);
}

static void print_inline_journal_information(ext2_filsys fs)
{
	journal_superblock_t	*jsb;
	struct ext2_inode	inode;
	ext2_file_t		journal_file;
	errcode_t		retval;
	ino_t			ino = fs->super->s_journal_inum;
	char			buf[1024];

	if (fs->flags & EXT2_FLAG_IMAGE_FILE)
		return;
	retval = ext2fs_read_inode(fs, ino,  &inode);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while reading journal inode"));
		exit(1);
	}
	retval = ext2fs_file_open2(fs, ino, &inode, 0, &journal_file);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while opening journal inode"));
		exit(1);
	}
	retval = ext2fs_file_read(journal_file, buf, sizeof(buf), 0);
	if (retval) {
		com_err(program_name, retval, "%s",
			_("while reading journal super block"));
		exit(1);
	}
	ext2fs_file_close(journal_file);
	jsb = (journal_superblock_t *) buf;
	if (be32_to_cpu(jsb->s_header.h_magic) != JFS_MAGIC_NUMBER) {
		fprintf(stderr, "%s",
			_("Journal superblock magic number invalid!\n"));
		exit(1);
	}
	e2p_list_journal_super(stdout, buf, fs->blocksize, 0);
}

static void print_journal_information(ext2_filsys fs)
{
	errcode_t	retval;
	char		buf[1024];
	journal_superblock_t	*jsb;

	/* Get the journal superblock */
	if ((retval = io_channel_read_blk64(fs->io,
					    fs->super->s_first_data_block + 1,
					    -1024, buf))) {
		com_err(program_name, retval, "%s",
			_("while reading journal superblock"));
		exit(1);
	}
	jsb = (journal_superblock_t *) buf;
	if ((jsb->s_header.h_magic != (unsigned) ntohl(JFS_MAGIC_NUMBER)) ||
	    (jsb->s_header.h_blocktype !=
	     (unsigned) ntohl(JFS_SUPERBLOCK_V2))) {
		com_err(program_name, 0, "%s",
			_("Couldn't find journal superblock magic numbers"));
		exit(1);
	}
	e2p_list_journal_super(stdout, buf, fs->blocksize, 0);
}

static void parse_extended_opts(const char *opts, blk64_t *superblock,
				int *blocksize)
{
	char	*buf, *token, *next, *p, *arg, *badopt = 0;
	int	len;
	int	do_usage = 0;

	len = strlen(opts);
	buf = malloc(len+1);
	if (!buf) {
		fprintf(stderr, "%s",
			_("Couldn't allocate memory to parse options!\n"));
		exit(1);
	}
	strcpy(buf, opts);
	for (token = buf; token && *token; token = next) {
		p = strchr(token, ',');
		next = 0;
		if (p) {
			*p = 0;
			next = p+1;
		}
		arg = strchr(token, '=');
		if (arg) {
			*arg = 0;
			arg++;
		}
		if (strcmp(token, "superblock") == 0 ||
		    strcmp(token, "sb") == 0) {
			if (!arg) {
				do_usage++;
				badopt = token;
				continue;
			}
			*superblock = strtoul(arg, &p, 0);
			if (*p) {
				fprintf(stderr,
					_("Invalid superblock parameter: %s\n"),
					arg);
				do_usage++;
				continue;
			}
		} else if (strcmp(token, "blocksize") == 0 ||
			   strcmp(token, "bs") == 0) {
			if (!arg) {
				do_usage++;
				badopt = token;
				continue;
			}
			*blocksize = strtoul(arg, &p, 0);
			if (*p) {
				fprintf(stderr,
					_("Invalid blocksize parameter: %s\n"),
					arg);
				do_usage++;
				continue;
			}
		} else {
			do_usage++;
			badopt = token;
		}
	}
	if (do_usage) {
		fprintf(stderr, _("\nBad extended option(s) specified: %s\n\n"
			"Extended options are separated by commas, "
			"and may take an argument which\n"
			"\tis set off by an equals ('=') sign.\n\n"
			"Valid extended options are:\n"
			"\tsuperblock=<superblock number>\n"
			"\tblocksize=<blocksize>\n"),
			badopt ? badopt : "");
		free(buf);
		exit(1);
	}
	free(buf);
}

int main (int argc, char ** argv)
{
	errcode_t	retval;
	ext2_filsys	fs;
	int		print_badblocks = 0;
	blk64_t		use_superblock = 0;
	int		use_blocksize = 0;
	int		image_dump = 0;
	int		force = 0;
	int		flags;
	int		header_only = 0;
	int		c;
	int		grp_only = 0;
#ifdef CONFIG_JSON
	int		json = 0;
	struct	json_obj *dump_obj = NULL;
#endif

#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
	textdomain(NLS_CAT_NAME);
	set_com_err_gettext(gettext);
#endif
	add_error_table(&et_ext2_error_table);
	fprintf (stderr, "dumpe2fs %s (%s)\n", E2FSPROGS_VERSION,
		 E2FSPROGS_DATE);
	if (argc && *argv)
		program_name = *argv;

	while ((c = getopt(argc, argv, OPTIONS"o:")) != EOF) {
		switch (c) {
		case 'b':
			print_badblocks++;
			break;
		case 'f':
			force++;
			break;
		case 'g':
			grp_only++;
			break;
		case 'h':
			header_only++;
			break;
		case 'i':
			image_dump++;
			break;
		case 'o':
			parse_extended_opts(optarg, &use_superblock,
					    &use_blocksize);
			break;
		case 'V':
			/* Print version number and exit */
			fprintf(stderr, _("\tUsing %s\n"),
				error_message(EXT2_ET_BASE));
			exit(0);
		case 'x':
			hex_format++;
			break;
#ifdef CONFIG_JSON
		case 'j':
			json++;
			break;
#endif
		default:
			usage();
		}
	}
	if (optind != argc - 1) {
		usage();
		exit(1);
	}
	device_name = argv[optind++];
	flags = EXT2_FLAG_JOURNAL_DEV_OK | EXT2_FLAG_SOFTSUPP_FEATURES | EXT2_FLAG_64BITS;
	if (force)
		flags |= EXT2_FLAG_FORCE;
	if (image_dump)
		flags |= EXT2_FLAG_IMAGE_FILE;
try_open_again:
	if (use_superblock && !use_blocksize) {
		for (use_blocksize = EXT2_MIN_BLOCK_SIZE;
		     use_blocksize <= EXT2_MAX_BLOCK_SIZE;
		     use_blocksize *= 2) {
			retval = ext2fs_open (device_name, flags,
					      use_superblock,
					      use_blocksize, unix_io_manager,
					      &fs);
			if (!retval)
				break;
		}
	} else
		retval = ext2fs_open (device_name, flags, use_superblock,
				      use_blocksize, unix_io_manager, &fs);
	if (retval && !(flags & EXT2_FLAG_IGNORE_CSUM_ERRORS)) {
		flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
		goto try_open_again;
	}
	if (!retval && (fs->flags & EXT2_FLAG_IGNORE_CSUM_ERRORS))
		printf("%s", _("\n*** Checksum errors detected in filesystem!  Run e2fsck now!\n\n"));
	flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
	if (retval) {
		com_err (program_name, retval, _("while trying to open %s"),
			 device_name);
		printf("%s", _("Couldn't find valid filesystem superblock.\n"));
		if (retval == EXT2_ET_BAD_MAGIC)
			check_plausibility(device_name, CHECK_FS_EXIST, NULL);
		exit (1);
	}
#ifdef CONFIG_JSON
	if (json)
		dump_obj = json_obj_create();
#endif
	fs->default_bitmap_type = EXT2FS_BMAP64_RBTREE;
	if (ext2fs_has_feature_64bit(fs->super))
		blocks64 = 1;
	if (print_badblocks) {
#ifdef CONFIG_JSON
		if (!json)
			list_bad_blocks(fs, 1);
#else
		list_bad_blocks(fs, 1);
#endif
	} else {
		if (grp_only)
			goto just_descriptors;
#ifdef CONFIG_JSON
		if (!json)
			list_super (fs->super);
#else
		list_super (fs->super);
#endif
#ifdef CONFIG_JSON
		if (!json && ext2fs_has_feature_journal_dev(fs->super)) {
			print_journal_information(fs);
			goto out;
		}
		if (!json && ext2fs_has_feature_journal(fs->super) &&
		    (fs->super->s_journal_inum != 0))
			print_inline_journal_information(fs);
#else
		if (ext2fs_has_feature_journal_dev(fs->super)) {
			print_journal_information(fs);
			goto out;
		}
		if (ext2fs_has_feature_journal(fs->super) &&
		    (fs->super->s_journal_inum != 0))
			print_inline_journal_information(fs);
#endif
#ifdef CONFIG_JSON
		if (!json)
			list_bad_blocks(fs, 0);
#else
		list_bad_blocks(fs, 0);
#endif
		if (header_only)
			goto out;
		fs->flags &= ~EXT2_FLAG_IGNORE_CSUM_ERRORS;
try_bitmaps_again:
		retval = ext2fs_read_bitmaps (fs);
		if (retval && !(fs->flags & EXT2_FLAG_IGNORE_CSUM_ERRORS)) {
			fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
			goto try_bitmaps_again;
		}
		if (!retval && (fs->flags & EXT2_FLAG_IGNORE_CSUM_ERRORS))
			printf("%s", _("\n*** Checksum errors detected in bitmaps!  Run e2fsck now!\n\n"));
just_descriptors:
#ifdef CONFIG_JSON
		if (json)
			fill_json_desc(dump_obj, fs);
		else
			list_desc(fs, grp_only);
#else
		list_desc(fs, grp_only);
#endif
		if (retval) {
			printf(_("\n%s: %s: error reading bitmaps: %s\n"),
			       program_name, device_name,
			       error_message(retval));
		}
	}
out:
#ifdef CONFIG_JSON
	if (json) {
		json_obj_print_json(dump_obj, 0);
		putchar('\n');
		json_obj_delete(dump_obj);
	}
#endif
	ext2fs_close_free(&fs);
	remove_error_table(&et_ext2_error_table);
	exit (0);
}
