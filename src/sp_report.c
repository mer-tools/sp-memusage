/*
 * This file is a part of mem-cpu-usage.
 *
 * Copyright (C) 2010 by Nokia Corporation
 *
 * Contact: Eero Tamminen <eero.tamminen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02r10-1301 USA
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include "sp_report.h"

/**
 * Private API
 */

/**
 * Output function template, used to print data when iterating header structure.
 */
typedef void (*table_print_fn)(FILE*, const sp_report_header_t*, int);

#define MAX_COLUMN_SIZE		256


#define BORDER_TSPLIT    ' '
#define BORDER_HLINE     '_'
#define BORDER_VLINE     '|'

/**
 * Returns memory location where the reference to new child of the
 * specified header must be stored.
 *
 * If header does not have any children, address of its 'child' field
 * is returned. If header already has children, address of the 'next'
 * field of the last child is returned.
 * @param[in] header  the parent header for the new child.
 * @return            memory location where the reference to the new child
 *                    must be stored.
 */
static sp_report_header_t** get_new_child_address(
		sp_report_header_t* header
		)
{
	if (header->child) {
		sp_report_header_t* child = header->child;
		while (child->next) {
			child = child->next;
		}
		return &child->next;
	}
	else {
		return &header->child;
	}
}

/**
 * Returns memory location where the reference to new sibling of the
 * specified header must be stored.
 *
 * Address of the 'next' field of the last sibling is returned.
 * @param[in] header  the sibling header for the new child.
 * @return            memory location where the reference to the new sibling
 *                    must be stored.
 */
static sp_report_header_t** get_new_sibling_address(
		sp_report_header_t* header
		)
{
	while (header->next) {
		header = header->next;
	}
	return &header->next;
}

/**
 * Frees resources associated with the header.
 *
 * @param[in] header   the header to free.
 * @return
 */
static void header_free_item(
		sp_report_header_t* header
		)
{
	if (header) {
		if (header->title) free(header->title);
		if (header->color_prefix) free(header->color_prefix);
		if (header->color_postfix) free(header->color_postfix);
		free(header);
	}
}


/**
 * Creates new header item.
 *
 * This function allocates resources for a new header item and initializes it
 * with the specified values.
 * @param[out] header   the created header.
 * @param[in] parent    the header parent.
 * @param[in] title     the header title.
 * @param[in] size      the header size. Length of the title (increased by 1) is
 *                      be used if size is 0.
 * @param[in] print     the data output function.
 * @param[in] data      the data to print.
 * @return              0 for success.
 */
static int header_create_item(
		sp_report_header_t** header,
		sp_report_header_t* parent,
		const char* title,
		int size,
		sp_report_cell_write_fn print,
		void* data
		)
{
	sp_report_header_t* item = (sp_report_header_t*)malloc(sizeof(sp_report_header_t));
	*header = item;

	if (!item) return -ENOMEM;
	item->size = size ? size : (int)strlen(title) + 1;
	if (item->size > MAX_COLUMN_SIZE) {
		return -EINVAL;
	}
	if (title) {
		item->title = strdup(title);
		if (!item->title) {
			return -ENOMEM;
		}
	}
	else {
		item->title = NULL;
	}
	item->print = print;
	item->data = data;
	item->child = NULL;
	item->next = NULL;
	item->parent = parent;
	item->depth = 0;
	item->size_print = 0;
	item->color_prefix = 0;
	item->color_postfix = 0;
	return 0;
}

/**
 * Iterate through items of the top header and prints their
 * contents/data.
 *
 * This function uses func function to print the header row at the
 * target_depth depth to the file fp.
 * @param[in] header         the header at the current iteration step.
 * @param[in] current_depth  the depth of the current iteration step.
 *                           Use 0 to call this function.
 * @param[in] target_depth   the target row depth.
 * @param[in] func           the printing function.
 * @param[in] fp             the output file.
 * @return
 */
static void header_iterate_level(
		const sp_report_header_t* header,
		int current_depth,
		int target_depth,
		table_print_fn func,
		FILE* fp
		)
{
	if (header->color_prefix) fputs(header->color_prefix, fp);
	int depth = current_depth + header->parent->depth - header->depth;
	if (depth > target_depth) {
		func(fp, header, 1);
	}
	if (depth == target_depth) {
		func(fp, header, 0);
	}
	if (depth < target_depth) {
		if (header->child) {
			header_iterate_level(header->child, depth, target_depth, func, fp);
		}
		else {
			func(fp, header, -1);
		}
	}
	if (header->color_postfix) fputs(header->color_postfix, fp);
	if (header->parent->parent && header->next) {
		header_iterate_level(header->next, current_depth, target_depth, func, fp);
	}
}

/**
 * Update header's format (printing size and depth).
 *
 * This function calculates the total size and max depth
 * of the headers children and updates size_print and depth
 * fields.
 * @param[in] header    the header to update.
 * @param[out] size     the headers printing size.
 * @param depth[out[    the headers depth.
 * @return
 */
static void header_update_format(
		sp_report_header_t* header,
		int* size,
		int* depth
		)
{
	sp_report_header_t* child;
	*size = 0;
	*depth = 0;
	for (child = header->child; child; child = child->next) {
		int child_depth = 0;
		int child_size = 0;
		header_update_format(child, &child_size, &child_depth);
		*size += child_size;
		if (child_depth + 1 > *depth) *depth = child_depth + 1;
	}
	if (header->size > *size) {
		/* Header should not be larger than its children. Increase
		 * the printing size of all its children to fix this */
		int size_diff = header->size - *size;
		for (child = header->child; child; child = child->child) {
			child->size_print += size_diff;
		}
		*size = header->size;
	}
	header->size_print = *size;
	header->depth = *depth;
}

/**
 * Prints the top line of the header.
 *
 * @param[in] fp              the output file.
 * @param[in] header          the header to print.
 * @param[in] relative_depth  -1 header is above the printing target depth(row).
 *                             0 header is at the printing target depth(row).
 *                             1 header is below the printing target depth(row).
 * @return
 */
static void header_print_top(
		FILE* fp,
		const sp_report_header_t* header,
		int relative_depth __attribute__ ((unused))
		)
{
	char buffer[MAX_COLUMN_SIZE];
	memset(buffer, BORDER_HLINE, header->size_print);
	buffer[header->size_print] = '\0';
	fputs(buffer, fp);
}

/**
 * Prints the header itself.
 *
 * @param[in] fp              the output file.
 * @param[in] header          the header to print.
 * @param[in] relative_depth  -1 header is above the printing target depth(row).
 *                             0 header is at the printing target depth(row).
 *                             1 header is below the printing target depth(row).
 * @return
 */
static void header_print_item(
		FILE* fp,
		const sp_report_header_t* header,
		int relative_depth
		)
{
	char buffer[MAX_COLUMN_SIZE];
	memset(buffer, ' ', header->size_print);
	buffer[header->size_print] = '\0';

	/* only print header if it's located at the target depth.
	 * Otherwise print just empty field of the header's size */
	if (relative_depth == 0 && header->title) {
		int pos = 0;
		int size = strlen(header->title);
		if (size < header->size_print) {
			pos = (header->size_print - size) / 2;
		}
		memcpy(buffer + pos, header->title, size);
		if (size > header->size_print) {
			buffer[header->size_print - 1] = '>';
			buffer[header->size_print] = '\0';
		}
	}
	fputs(buffer, fp);
}

/**
 * Prints the data described by header.
 *
 * @param[in] fp              the output file.
 * @param[in] header          the header to print.
 * @param[in] relative_depth  -1 header is above the printing target depth(row).
 *                             0 header is at the printing target depth(row).
 *                             1 header is below the printing target depth(row).
 * @return
 */

static void header_print_data(
		FILE* fp,
		const sp_report_header_t* header,
		int relative_depth __attribute__ ((unused))
		)
{
	if (header->print) {
		char buffer[MAX_COLUMN_SIZE];
		int size = header->print(buffer, header->size, header->data);
		int i;

		if (size > header->size_print) {
			/* if the returned data does fit into the column, cut it and
			 * append '>' as a sign that the column data has been truncated */
			buffer[header->size_print - 1] = '>';
		}
		fputs(buffer, fp);
		for (i = 0; i < header->size_print - size; i++) {
			fputc(' ', fp);
		}
	}
	else {
		header_print_item(fp, header, 1);
	}
}


/**
 * Public API
 *
 * See header for specifications.
 */

void sp_report_header_free(
		sp_report_header_t* header
		)
{
	if (header) {
		sp_report_header_free(header->child);
		sp_report_header_free(header->next);
		header_free_item(header);
	}
}


sp_report_header_t* sp_report_header_add_child(
		sp_report_header_t* header,
		const char* title,
		int size,
		sp_report_cell_write_fn print,
		void* data
		)
{
	sp_report_header_t* child;
	int rc;

	if ( (rc = header_create_item(&child, header, title, size, print, data)) != 0) {
		header_free_item(child);
		return NULL;
	}
	*get_new_child_address(header) = child;
	return child;
}


sp_report_header_t* sp_report_header_add_sibling(
		sp_report_header_t* header,
		const char* title,
		int size,
		sp_report_cell_write_fn print,
		void* data
		)
{
	sp_report_header_t* sibling;
	int rc;

	if ( (rc = header_create_item(&sibling, header->parent, title, size, print, data)) != 0) {
		header_free_item(sibling);
		return NULL;
	}
	*get_new_sibling_address(header) = sibling;
	return sibling;
}


int sp_report_header_remove(
		sp_report_header_t* root,
		sp_report_header_t* header
		)
{
	sp_report_header_t* child;
	if (root->child == header) {
		root->child = header->next;
		header->next = NULL;
		return 0;
	}
	for (child = root->child; child; child = child->next) {
		if (child->next == header) {
			child->next = header->next;
			header->next = NULL;
			return 0;
		}
		if (child->child && sp_report_header_remove(child->child, header) == 0) {
			return 0;
		}
	}
	return 1;
}

int sp_report_print_header(
		FILE* fp,
		sp_report_header_t* root
		)
{
	int iDepth;
	sp_report_header_t* header;

	/* updates header format by calculating printing sizes and depth */
	int size = 0;
	int depth = 0;
	header_update_format(root, &size, &depth);

	/* first print the top line, something like ____ ____ ___ */
	for (header = root->child; header; header = header->next) {
		header_iterate_level(header, 0, 0, header_print_top, fp);
		if (header->next) {
			fputc(BORDER_TSPLIT, fp);
		}
	}
	fputc('\n', fp);

	/* print the formatted header structure */
	for (iDepth = 1; iDepth <= root->depth; iDepth++) {
		for (header = root->child; header; header = header->next) {
			header_iterate_level(header, 0, iDepth, header_print_item, fp);
			if (header->next) {
				fputc(BORDER_VLINE, fp);
			}
		}
		fputc('\n', fp);
	}
	return 0;
}


int sp_report_print_data(
		FILE* fp,
		const sp_report_header_t* root
		)
{
	sp_report_header_t* header;

	for (header = root->child; header; header = header->next) {
		/* large target depth (0xFFFF) fill result in the lowest headers being called
		 * for header_print_data, which is exactly what we need */
		header_iterate_level(header, 0, 0xFFFF, header_print_data, fp);
		if (header->next) {
			fputc(' ', fp);
		}
	}
	fputc('\n', fp);
	return 0;
}


int sp_report_header_set_color(
		sp_report_header_t* header,
		const char* color_prefix,
		const char* color_postfix
		)
{
	if (header->color_prefix) {
		free(header->color_prefix);
	}
	if (header->color_postfix) {
		free(header->color_postfix);
	}
	header->color_prefix = color_prefix ? strdup(color_prefix) : NULL;
	header->color_postfix = color_postfix ? strdup(color_postfix) : NULL;
	return 0;
}


int sp_report_header_set_title(
		sp_report_header_t* header,
		const char* title,
		int size
		)
{
	if (header->title) free(header->title);
	header->title = strdup(title);
	if (header->title == NULL) {
		return -ENOMEM;
	}
	header->size = size ? size : (int)strlen(title) + 1;

	return 0;
}
