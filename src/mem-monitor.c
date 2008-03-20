/* ========================================================================= *
 * File: mem-momitor.c, part of sp-memusage
 * 
 * Copyright (C) 2005-2008 by Nokia Corporation
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

#include "mem-monitor.h"

/* ========================================================================= *
 * Definitions.
 * ========================================================================= */

/* Compile-time array capacity calculation */
#define CAPACITY(a)  (sizeof(a) / sizeof(*a))

/* Correct division of 2 unsigned values */
#define DIVIDE(a,b)  (((a) + ((b) >> 1)) / (b))

typedef struct
{
   const char* name;    /* /proc/meminfo parameter with ":" */
   unsigned    data;    /* loaded value                     */
} MEMINFO;

/* ========================================================================= *
 * Local data.
 * ========================================================================= */

/* ========================================================================= *
 * Local methods.
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * mu_load -- opens meminfo file and load values.
 * parameters:
 *    path - path to file to handle.
 *    vals - array of values to be handled.
 *    size - size of vals array.
 * returns: number of sucessfully loaded values.
 * ------------------------------------------------------------------------- */
static unsigned mu_load(const char* path, MEMINFO* vals, unsigned size)
{
   unsigned counter = 0;
   FILE*    meminfo = fopen(path, "rt");

   if ( meminfo )
   {
      char line[256];

      /* Load all lines in file */
      while ( fgets(line, CAPACITY(line),meminfo) )
      {
         unsigned idx;

         /* Search and setup parameter */
         for (idx = 0; idx < size; idx++)
         {
            if ( line == strstr(line, vals[idx].name) )
            {
               /* Parameter has a format SomeName:\tValue, we expect that MEMINFO::name contains ":" */
               vals[idx].data = (unsigned)strtoul(line + strlen(vals[idx].name) + 1, NULL, 0);
               counter++;
               break;
            }
         }
      } /* while have data */

      fclose(meminfo);
   }

   return counter;
} /* mu_load */

/* ------------------------------------------------------------------------- *
 * mu_check_flag -- opens specified flag and return true if it set on.
 * parameters:
 *    path - path to file to handle.
 * returns: 1 if flag is available and set on.
 * ------------------------------------------------------------------------- */
static int mu_check_flag(const char* path)
{
   FILE* fp = fopen(path, "r");

   if ( fp )
   {
      const int value = fgetc(fp);
      fclose(fp);
      return (value == '1');
   }

   return 0;
} /* mu_check_flag */


/* ========================================================================= *
 * Public methods.
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * memusage -- returns memory usage for current system in MEMUSAGE structure.
 * parameters:
 *    usage - parameters to be updated.
 * returns:
 *    0 if values loaded successfuly OR negative error code.
 * ------------------------------------------------------------------------- */
int memusage(MEMUSAGE* usage)
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
         { "SwapFree:",  0 }
      }; /* vals */

      /* Load values from the meminfo file */
      if ( CAPACITY(vals) == mu_load("/proc/meminfo", vals, CAPACITY(vals)) )
      {
         /* Discover memory information using loaded numbers */
         usage->total = vals[0].data + vals[1].data;
         usage->free  = vals[2].data + vals[3].data + vals[4].data + vals[5].data;
         usage->used  = usage->total - usage->free;
         usage->util  = DIVIDE(100 * usage->used, usage->total);

         /* We are succeed */
         return 0;
      }
      else
      {
         /* Clean-up values */
         memset(usage, 0, sizeof(usage));
      }
   }

   /* Something wrong, shows as error */
   return -1;
} /* memusage */

/* ========================================================================= *
 * main function, just for testing purposes.
 * ========================================================================= */
#ifdef TESTING

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
   nice(-19);

   printf ("time:\t\ttotal:\tfree:\tused:\tuse-%%:\tstatus:\n");
   while (1)
   {
       MEMUSAGE usage;

      /* Load all values from meminfo file */
      if (0 == memusage(&usage))
      {
         const time_t tv = time(NULL);
         struct tm*   ts = gmtime(&tv);
         const char*  bg = (mu_check_flag("/sys/kernel/low_watermark") ? "BgKill" : "");
         const char*  lm = (mu_check_flag("/sys/kernel/high_watermark") ? ",LowMem" : "");

          printf ("%02u:%02u:%02u\t%u\t%u\t%u\t%u\t%s%s\n",
                     ts->tm_hour, ts->tm_min, ts->tm_sec,
                     usage.total, usage.free, usage.used, usage.util,
                     bg, lm
                  );
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

#endif /* TESTING */

/* ========================================================================= *
 *                    No more code in file mem-monitor.c                     *
 * ========================================================================= */
