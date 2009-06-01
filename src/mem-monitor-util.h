/* ========================================================================= *
 * This file is part of sp-memusage.
 *
 * Copyright (C) 2005-2009 by Nokia Corporation
 *
 * Author: Leonid Moiseichuk <leonid.moiseichuk@nokia.com>
 * Contact: Eero Tamminen <eero.tamminen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ========================================================================= */

#ifndef MEM_MONITOR_UTIL_H
#define MEM_MONITOR_UTIL_H

typedef struct {
	const char* key;    /* /proc/meminfo parameter with ":" */
	unsigned    value;  /* loaded value                     */
} MEMINFO;

/* Parses /proc/meminfo, looking for values for the keys defined in @wanted.
 *
 *    @wanted       What keys to look for, eg. "MemTotal:", "Cached:".
 *    @wanted_cnt   How many items @wanted contains.
 *
 * Returns the number of keys that were found.
 */
unsigned parse_proc_meminfo(MEMINFO* wanted, unsigned wanted_cnt);

/* Opens specified flag file, and return true if it set on.
 * parameters:
 *    path - path to file to handle.
 * returns:
 *    0 error opening file or flag not set.
 *    1 flag is available and set on.
 */
int check_flag(const char* path);

#endif
