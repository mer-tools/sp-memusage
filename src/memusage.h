/* ========================================================================= *
 * File: memusage.h, part of sp-memusage
 * 
 * Copyright (C) 2005,2006 by Nokia Corporation
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
 *
 * Description:
 *    Reading /proc/meminfo and return memory important values:
 *    free, used, usage, total.
 *
 * History:
 *
 * 27-Sep-2005 Leonid Moiseichuk
 * - initial version created using memlimits.c as a prototype.
 * ========================================================================= */

#ifndef MEMUSAGE_H_USED
#define MEMUSAGE_H_USED

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= *
 * Includes
 * ========================================================================= */

#include <unistd.h>

/* ========================================================================= *
 * Definitions.
 * ========================================================================= */

/* Structure that used to report about memory consumption */
typedef struct
{
    size_t  total;     /* Total amount of memory in system: RAM + swap */
    size_t  free;      /* Free memory in system, bytes                 */
    size_t  used;      /* Used memory in system, bytes                 */
    size_t  util;      /* Memory utilization in percents               */
} MEMUSAGE;

/* ========================================================================= *
 * Methods.
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * memusage -- returns memory usage for current system in MEMUSAGE structure.
 * parameters:
 *    usage - parameters to be updated.
 * returns:
 *    0 if values loaded successfuly OR negative error code.
 * ------------------------------------------------------------------------- */
int memusage(MEMUSAGE* usage);

#ifdef __cplusplus
}
#endif

#endif /* MEMUSAGE_H_USED */
