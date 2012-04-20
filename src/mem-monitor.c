/* ========================================================================= *
 * File: mem-momitor.c, part of sp-memusage
 * 
 * Copyright (C) 2005-2009,2012 by Nokia Corporation
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
 *    Read /proc/meminfo and return memory usage values at given interval:
 *    free, used, usage, total.
 *
 * History:
 *
 * 01-Jun-2009
 * - Moved some code to mem-monitor-util.{ch}, that is now shared with
 *   mem-monitor and mem-cpu-monitor.  Removed mem-monitor.h.
 * - Call fflush(stdout) after printing, to make sure that the lines get
 *   flushed out of C library buffers when output is redirected to file.
 *
 * 17-Sep-2008 Eero Tamminen
 * - Use term "avail" instead of "free" as although the shown amount
 *   of memory is available for re-use, it isn't tehnically free
 *   but mostly in use as buffers & caches
 * 
 * 24-Aug-2006 Eero Tamminen
 * - add -% to usage and ':' to end of each header name
 * 
 * 20-Jun-2006 Leonid Moiseichuk
 * - added flags for low/high memory notifications.
 * - added timestamps to the output, show numbers as KBs
 *
 * 28-Apr-2006 Leonid Moiseichuk
 * - removed slab information which makes users confused.
 *
 * 27-Sep-2005 Leonid Moiseichuk
 * - initial version created using memlimits.c as a prototype.
 *
 * ========================================================================= */

/* ========================================================================= *
 * Includes
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

#include "mem-monitor-util.h"

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

/* Compile-time array capacity calculation */
#define CAPACITY(a)  (sizeof(a) / sizeof(*a))

/* Correct division of 2 unsigned values */
#define DIVIDE(a,b)  (((a) + ((b) >> 1)) / (b))

/* ------------------------------------------------------------------------- *
 * memusage -- returns memory usage for current system in MEMUSAGE structure.
 * parameters:
 *    usage - parameters to be updated.
 * returns:
 *    0 if values loaded successfuly OR negative error code.
 * ------------------------------------------------------------------------- */
static int memusage(MEMUSAGE* usage)
{
   /* Check the pointer validity first */
   if ( usage )
   {
      static MEMINFO vals[] =
      {
         { "MemTotal:",  0 },
         { "SwapTotal:", 0 },
         { "MemFree:",   0 },
         { "Buffers:",   0 },
         { "Cached:",    0 },
         { "SwapCached:",0 },
         { "SwapFree:",  0 }
      }; /* vals */

      /* Load values from the meminfo file */
      if ( CAPACITY(vals) == parse_proc_meminfo(vals, CAPACITY(vals)) )
      {
         /* Discover memory information using loaded numbers */
         usage->total = vals[0].value + vals[1].value;
         usage->free  = vals[2].value + vals[3].value + vals[4].value + vals[5].value + vals[6].value;
         usage->used  = usage->total - usage->free;
         usage->util  = DIVIDE(100 * usage->used, usage->total);

         /* We are succeed */
         return 0;
      }
      else
      {
         /* Clean-up values */
         memset(usage, 0, sizeof(MEMUSAGE));
      }
   }

   /* Something wrong, shows as error */
   return -1;
} /* memusage */

int main(const int argc, const char* argv[])
{
   /* Update interval once per 3 seconds by default */
   unsigned period = 3;
   
   if (argc > 2 || (argc == 2 && !isdigit(argv[1][0]))) 
   {
      fprintf(stderr, "\nusage: %s [output interval in secs]\n\n", *argv);
      exit(1);
   }
   else if (argc == 2)
   {
      period = strtoul(argv[1], NULL, 0);
   }

   /* We must print data always */
   if (nice(-19) == -1) {
      perror("Warning: failed to change process priority.");
   }

   printf ("time:\t\ttotal:\tavail:\tused:\tuse-%%:\tstatus:\n");
   while (1)
   {
       MEMUSAGE usage;

      /* Load all values from meminfo file */
      if (0 == memusage(&usage))
      {
         const time_t tv = time(NULL);
         struct tm*   ts = localtime(&tv);
         const char*  bg = (check_flag("/sys/kernel/low_watermark") ? "BgKill" : "");
         const char*  lm = (check_flag("/sys/kernel/high_watermark") ? ",LowMem" : "");

          printf ("%02u:%02u:%02u\t%u\t%u\t%u\t%u\t%s%s\n",
                     ts->tm_hour, ts->tm_min, ts->tm_sec,
                     usage.total, usage.free, usage.used, usage.util,
                     bg, lm
                  );

	  fflush(stdout);
      }
      else
      {
         printf ("unable to load values from /proc/meminfo file\n");
         return -1;
      }

      sleep(period);
   }

   /* That is all */
   return 0;
} /* main */

/* ========================================================================= *
 *                    No more code in file mem-monitor.c                     *
 * ========================================================================= */
