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
/** @file sp_report.h
 * API for outputting data columns with a complex header structure.
 *
 * For example:
 * __ _____________________ _______
 *   |       System        |
 *   |           CPU       |
 *   |              Usage  |
 * BL|Mem Max Freq  %  MHz |Process
 * o. 24  3.8GHz   25% 25Hz
 * .. 24  3.8GHz    0% 100>
 *
 * Printing similar header manually becomes complicated when some of
 * the columns are optional or dynamic. That was the main reason for
 * this API.
 *
 * Columns also can be dynamically added (of course the headers should
 * be reprinted before printing data for the new header structure).
 */
#ifndef SP_REPORT_H
#define SP_REPORT_H

#include <stdio.h>

/* the data printing template function */
typedef int (*sp_report_cell_write_fn)(char* buffer, int size, void* arg);

/**
 * The column header structure.
 *
 * Column header structure must contain either printing function or
 * child headers.
 */
typedef struct sp_report_header_t {
	/* actual header size */
	int size;
	/* header title */
	char* title;
	/* column data printing function */
	sp_report_cell_write_fn print;

	/* the header 'depth' - number of child rows */
	int depth;
	/* the printing size */
	int size_print;
	/* data to print */
	void* data;

	/* column color ANSI escape sequences */
	char* color_prefix;
	char* color_postfix;

	/* parent header */
	struct sp_report_header_t* parent;

	/* next (sibling) header */
	struct sp_report_header_t* next;

	/* first child header */
	struct sp_report_header_t* child;
} sp_report_header_t;



/**
 * Adds a new child to the specified header.
 *
 * @param[in] header    the parent header.
 * @param[in] title     the new header title.
 * @param[in] size      the new header size. Length of the title (increased by 1) is
 *                      be used if size is 0.
 * @param[in] print     the data output function.
 * @param[in] data      the data to print.
 * @return              the created header or NULL in the case of failure.
 */
sp_report_header_t* sp_report_header_add_child(
		sp_report_header_t* header,
		const char* title,
		int size,
		sp_report_cell_write_fn print,
		void* data
		);


/**
 * Adds a new sibling to the specified header.
 *
 * @param[in] header    the sibling header.
 * @param[in] title     the new header title.
 * @param[in] size      the new header size. Length of the title (increased by 1) is
 *                      be used if size is 0.
 * @param[in] print     the data output function.
 * @param[in] data      the data to print.
 * @return              the created header or NULL in the case of failure.
 */
sp_report_header_t* sp_report_header_add_sibling(
		sp_report_header_t* header,
		const char* title,
		int size,
		sp_report_cell_write_fn print,
		void* data
		);


/**
 * Sets header (column) color.
 *
 * The colors must be specified as ANSI escape sequences.
 * @param[in] header          the header.
 * @param[in] color_prefix    the column color ANSI escape sequence.
 * @param[in] color_postfix   the normal color (reset) ANSI escape sequence.
 * @return
 */

int sp_report_header_set_color(
		sp_report_header_t* header,
		const char* color_prefix,
		const char* color_postfix
		);

/**
 * Sets header title text.
 *
 * @param[in] header   the header.
 * @param[in] title    the header title.
 * @param[in] size     the header size. The length of title will be used if zero.
 * @return
 */
int sp_report_header_set_title(
		sp_report_header_t* header,
		const char* title,
		int size
		);

/**
 * Removes header from the report.
 *
 * @param[in] root    the report root header.
 * @param[in] header  the header to remove.
 * @return            0 if the header was successfully removed.
 *                    1 if the header was not found.
 */
int sp_report_header_remove(
		sp_report_header_t* root,
		sp_report_header_t* header
		);

/**
 * Frees header together with its siblings and children.
 *
 * @param[in] header   the root header.
 */
void sp_report_header_free(
		sp_report_header_t* header
		);


/**
 * Prints formatted data header described by the header structure.
 *
 * @param[in] fp    the output file.
 * @param[in] root  the root of header structure.
 * @return          0 for success.
 */
int sp_report_print_header(
		FILE* fp,
		sp_report_header_t* root
		);

/**
 * Prints data described by the header structure.
 *
 * @param[in] fp    the output file.
 * @param[in] root  the root of header structure.
 * @return          0 for success.
 */

int sp_report_print_data(
		FILE* fp,
		const sp_report_header_t* root
		);



#endif
