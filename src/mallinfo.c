/* ========================================================================= *
 * File: mallinfo.c, part of sp-memusage
 * 
 * Copyright (C) 2005 by Nokia Corporation
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
 *    Quite improved version of Andrei's mallinfo utility, useful for
 *    memory usage tracking.
 *
 *    To enable tracing you have to set MALLINFO variable:
 *       export MALLINFO="yes"        -- use 5 seconds timeout and SIGALRM
 *       export MALLINFO="signal=10"  -- use SIGUSR1 to generate the report
 *       export MALLINFO="period=10"  -- periodic report for 10 secons
 *
 *    The report format is the following:
 *       time    - time of report since application started
 *       arena   - size of non-mmapped space allocated from system
 *       ordblks - number of free chunks
 *       smblks  - number of fastbin blocks
 *       hblks   - number of mmapped regions
 *       hblkhd  - space in mmapped regions
 *       usmblks -  maximum total allocated space
 *       fsmblks - space available in freed fastbin blocks
 *       uordblks - total allocated space
 *       fordblks - total free space
 *       keepcost - top-most, releasable (via malloc_trim) space
 *       total    - mi.uordblks + mi.fordblks + mi.hblkhd,
 *       sbrk     - sbrk pointer at the specified time
 *
 * History:
 *
 * 20-Dec-2005 Leonid Moiseichuk
 * - Added environment variable MALLINFO analysis and working for signal.
 *
 * 01-Sep-2005 Leonid Moiseichuk
 * - initial version of created.
 *
 * ========================================================================= */

/* ========================================================================= *
 * Includes
 * ========================================================================= */

#define _GNU_SOURCE

#include <sys/types.h>

#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ========================================================================= *
 * General settings.
 * ========================================================================= */

#define TOOL_NAME    "mallinfo"
#define TOOL_VERS    "0.2.0"
#define TOOL_FILE    "%s/mallinfo-%d.trace"
#define TOOL_VAR     "MALLINFO"
#define TOOL_SIGNAL  SIGALRM
#define TOOL_PERIOD  5     /* reporting time in seconds */

#define TOOL_LOGO    1

/* ========================================================================= *
 * Definitions.
 * ========================================================================= */

/* ========================================================================= *
 * Local data.
 * ========================================================================= */

static time_t  s_epoch = 0;   /* Time of application launch */
static char    s_path[256];   /* Path for storing report    */

static time_t  s_period = 0;  /* Period of reporting          */
static int     s_signal = 0;  /* Default signal for reporting */

/* ========================================================================= *
 * Local methods.
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * mi_get -- find the specified option in the configuration string and get it.
 * parameters: configuration, option name, default value.
 * returns: new or default option value.
 * ------------------------------------------------------------------------- */
static unsigned mi_get(const char* config, const char* opt, unsigned def)
{
   const char* ptr = strstr(config, opt);
   return (ptr ? (unsigned)strtoul(ptr + strlen(opt) + 1, NULL, 0) : def);
} /* mi_get */

/* ------------------------------------------------------------------------- *
 * mi_dump -- Create the file and dump trace information into it.
 * parameters: singal number. 0 means that we shall not create timer any more.
 * returns: none.
 * ------------------------------------------------------------------------- */

static void mi_dump(int signo)
{
   /* Got the tracing information first */
   static time_t pred = 0;
   const time_t    tm = time(NULL);

   /* Check that at least 1 second passed from the time of the previous call */
   if (pred != tm)
   {
      /* Information about memory status */
      const unsigned        bk = (unsigned)sbrk(0);
      const struct mallinfo mi = mallinfo();

      /* File to print */
      FILE* file = fopen(s_path, "at");

      /* Dump header: check the file is opened correctly and it is not new */
      if (NULL == file || 0 == ftell(file))
      {
         if ( !file )
            file = stderr;
         fprintf(file,"time,arena,ordblks,smblks,hblks,hblkhd,usmblks,fsmblks,uordblks,fordblks,keepcost,total,sbrk\n");
      }

      /* Dump the number of allocated blocks */
      fprintf(file,"%u,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,0x%08x\n",
               (unsigned)(tm - s_epoch),
               mi.arena,
               mi.ordblks,
               mi.smblks,
               mi.hblks,
               mi.hblkhd,
               mi.usmblks,
               mi.fsmblks,
               mi.uordblks,
               mi.fordblks,
               mi.keepcost,
               mi.uordblks + mi.fordblks + mi.hblkhd,
               bk
            );

      /* Close file if it not stderr */
      fflush(file);
      if (file != stderr)
         fclose(file);
   }

   /* Setup new alarm if it necessary */
   if (signo && s_period)
   {
      alarm(s_period);
      pred = tm;
   }
} /* mi_trace_dump */

/* ========================================================================= *
 * initializer and finalizer that allowed static linking.
 * ========================================================================= */

static void mi_init(void) __attribute__((constructor));
static void mi_fini(void) __attribute__((destructor));

 /* ------------------------------------------------------------------------- *
 * mi_init -- this function shall be called by Loader when library is loaded.
 * parameters: none.
 * returns: none.
 * ------------------------------------------------------------------------- */

static void mi_init(void)
{
   const char* value = getenv(TOOL_VAR);

   if (value && *value)
   {
      /* Variables for storing signal and period */
      const unsigned signum = mi_get(value, "signal", 0);
      const unsigned period = mi_get(value, "period", 0);

      /* Initialize all variables first */
      s_epoch = time(NULL);
      snprintf(s_path, sizeof(s_path), TOOL_FILE, getenv("HOME"), getpid());

      /* Setting the working values according to passed */
      if ( period )
      {
         /* The period is set -> should be used the default signal */
         s_period = period;
         s_signal = TOOL_SIGNAL;
      }
      else if ( signum )
      {
         /* The signal is set -> should be event-driven reporting */
         s_period = 0;
         s_signal = (int)signum;
      }
      else
      {
         /* No period or signal set but variable is exists -> using defaults */
         s_period = TOOL_PERIOD;
         s_signal = TOOL_SIGNAL;
      }

#if TOOL_LOGO
      fprintf(stderr, "%s version %s build %s %s\n", TOOL_NAME, TOOL_VERS, __DATE__, __TIME__);
      fprintf(stderr, "(c) 2005 Nokia\n\n");

      fprintf(stderr, "detected variable %s with value '%s'\n", TOOL_VAR, value);
      fprintf(stderr, "signal %d (%s) is used for reporting\n", s_signal, strsignal(s_signal));
      fprintf(stderr, "report will be created every %u seconds\n", (unsigned)s_period);
      fprintf(stderr, "report file %s\n", s_path);
#endif

      signal(s_signal, mi_dump);
      /* We should report first line if periodic reports are switched on */
      if ( s_period )
         mi_dump(s_signal);
   }
} /* mi_init */

/* ------------------------------------------------------------------------- *
 * mi_fini -- this function shall be called by Loader when library is unloaded.
 * parameters: none.
 * returns: none.
 * ------------------------------------------------------------------------- */

static void mi_fini(void)
{
   /* We should report the last line if periodic reports are used */
   if ( s_period )
      mi_dump(0);
#if TOOL_LOGO
   if ( s_period || s_signal )
      fprintf(stderr, "\n%s finalization completed\n", TOOL_NAME);
#endif
} /* mi_fini */

/* ========================================================================= *
 *                    No more code in file mallinfo.c                        *
 * ========================================================================= */
