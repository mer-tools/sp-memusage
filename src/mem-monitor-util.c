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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mem-monitor-util.h"

static char line[256];

unsigned
parse_proc_meminfo(MEMINFO* wanted, unsigned size)
{
	unsigned counter = 0;
	FILE* meminfo = fopen("/proc/meminfo", "r");
	if (!meminfo) return 0;
	while (fgets(line, sizeof(line), meminfo)) {
		unsigned idx;
		for (idx=0; idx < size; ++idx) {
			const char* key = wanted[idx].key;
			if (line == strstr(line, key)) {
				/* Parameter has a format SomeName:\tValue, we
				 * expect that MEMINFO::name contains ":" */
				wanted[idx].value = (unsigned)
					strtoul(line+strlen(key)+1, NULL, 0);
				if (++counter == size) goto done;
				continue;
			}
		}
	}
done:
	fclose(meminfo);
	return counter;
}

int check_flag(const char* path)
{
	FILE* fp = fopen(path, "r");
	if (!fp) return 0;
	const int value = fgetc(fp);
	fclose(fp);
	return (value == '1');
}
