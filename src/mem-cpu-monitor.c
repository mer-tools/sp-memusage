/* ========================================================================= *
 *
 * mem-cpu-monitor
 * ---------------
 *
 * mem-cpu-monitor aims to be a lightweight tool for monitoring both system
 * memory and CPU usage. Additionally, it can be used to track memory and CPU
 * usage of specific processes.
 *
 * Couple additional tweaks are used when printing to console (ie. isatty()
 * returns true for the file descriptor):
 *
 *    - reprint the headers when we have printed a screenful of updates
 *    - highlight alternating process columns and the memory watermark column
 *      with colors
 *
 * This file is part of sp-memusage.
 *
 * Copyright (C) 2005-2009 by Nokia Corporation
 *
 * Authors: Leonid Moiseichuk <leonid.moiseichuk@nokia.com>
 *          Arun Srinivasan <arun.srinivasan@nokia.com>
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
 * ========================================================================= */

/* System total memory: 262144 kB RAM, 768000 kB swap
 * PID  1547: browser
 *                _______________  ____________  _____________________________
 * ________  __  / system memory \/ system CPU \/PID 1547  browser         ...\
 * time:   \/BL\/  used:  change:     %:  MHz:   clean:  dirty: change: CPU-%:
 * 02:22:31  --   143272       +0   0.00     0    1252    2784      +0   0.00
 * 02:22:34  --   143272       +0   1.32   253    1252    2784      +0   0.00
 * 02:22:37  --   143332      +60   1.98   253    1252    2784      +0   0.00
 * 02:22:40  --   143332       +0   2.30   252    1252    2784      +0   0.00
 */

/* Output format without memory flags:
 *
 *             _______________  ____________  _____________________________
 * _________  / system memory \/ system CPU \/PID 6603  ssh root@192.168...\
 * time:    \/  used:  change:     %:  MHz:   clean:  dirty: change: CPU-%:
 * 12:28:32    498324       +0   0.00     2       0     460      +0   0.00
 * 12:28:35    498084     -240   8.25   805       0     460      +0   0.00
 */

#define _GNU_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include "mem-monitor-util.h"

static const char progname[] = "mem-cpu-monitor";

// Nokia specific memory watermarks. These files contain 0/1 in ASCII depending
// on whether the flag it set or not.
static const char watermark_low[] = "/sys/kernel/low_watermark";
static const char watermark_high[] = "/sys/kernel/high_watermark";

// Output to stdout by default, otherwise to file given by user.
static FILE* output = NULL;

// Bitmask holding a number of option flags
static unsigned int 	option_flags = 0;

static int 				sys_mem_change_threshold = 0;
static float 			sys_cpu_change_threshold = 0.0f;

static bool 			do_print_report_default = true;

// Flags should have values of powers of 2
enum OPTION_VALUE_FLAGS {
	OF_PROC_MEM_CHANGES_ONLY 	= 1,
	OF_PROC_CPU_CHANGES_ONLY 	= 2,
	OF_SYS_MEM_CHANGES_ONLY 	= 4,
	OF_SYS_CPU_CHANGES_ONLY 	= 8,
	OF_INTERVAL_OPTION_SET 		= 16
};
// Adds a flag to the bitmask
#define ADD_OPTION_VALUE_FLAG(option_flags_var, value) \
	option_flags_var |= (value);
	
// Checks whether a flag is set in the bitmask
#define IS_OPTION_VALUE_FLAG_SET(option_flags_var, value) \
	( option_flags_var & value ? 1 : 0 )

// Dynamic buffer to be used with getline().
static char* dynbuf = NULL;
static size_t dynbuf_cap = 0u;

// Show some colors if we're printing to console.
static bool colors = true;
#define COLOR_CLEAR    "\033[0m"
#define COLOR_LOWMARK  "\033[33m"
#define COLOR_HIGHMARK "\033[31m"
#define COLOR_PROCESS  "\033[32m"

#define DEFAULT_SLEEP_INTERVAL 3u
#define UNKNOWN_PROCESS_NAME "<unknown>"

// Die gracefully when we get interrupted with Ctrl-C. Makes it easier to see
// memory leaks with Valgrind.
static volatile sig_atomic_t quit = 0;
static void quit_app(int sig) { (void)sig; if (quit++) _exit(1); }

/* One for each PID that user wants to monitor.
 *
 *   @pid              Process ID.
 *   @name             Process name. For normal processes this is the command
 *                     line (/proc/<pid>/cmdline), and for kernel threads it is
 *                     the Name field from /proc/<pid>/status. Dynamically
 *                     allocated, may be NULL.
 *   @smaps_path       Preformatted string ''/proc/<pid>/smaps''. Dynamically
 *                     allocated, never NULL.
 *   @stat_path        Preformatted string ''/proc/<pid>/stat''. Dynamically
 *                     allocated, never NULL.
 *
 *   @mem_clean        Amount of Private Clean memory in kilobytes, calculated
 *                     by combining Private_Clean from /proc/<pid>/smaps.
 *   @mem_dirty        Amount of Private Dirty memory in kilobytes, calculated
 *                     by combining Private_Dirty and Swap from
 *                     /proc/<pid>/smaps.
 *   @mem_change       Per round change of clean + dirty, in kilobytes.
 *
 *   @cputicks_total   Amount of CPU ticks this process has been scheduled in
 *                     kernel & user modes, as reported by /proc/<pid>/stat.
 *   @cputicks_change  Per round change of sys+user CPU ticks.
 */
typedef struct {
	unsigned pid;
	char* name;
	char* smaps_path;
	char* stat_path;
	size_t mem_clean;
	size_t mem_dirty;
	ssize_t mem_change;
	size_t cputicks_total;
	size_t cputicks_change;
} monitored_process_t;


/*
 *  CPU stats monitoring
 */

#define CPU_STATS_SOURCE  "/sys/devices/system/cpu/cpu0/cpufreq/stats/time_in_state"

/**
 * The cpu stats data record.
 * This structure holds how many ticks cpu has spent at the
 * specifc frequency.
 */
typedef struct _cpu_stats_record_t {
  int freq;
  int ticks;
  struct _cpu_stats_record_t* next;
} cpu_stats_record_t;

typedef cpu_stats_record_t* cpu_stats_t;

static cpu_stats_t cpu_stats[2] = {NULL, NULL};
static cpu_stats_t* cpu_stats_start = &cpu_stats[0];
static cpu_stats_t* cpu_stats_end = &cpu_stats[1];

/**
 * Retrieves cpu stats record for the specified frequency.
 *
 * A new cpu stat record will be added, if the cpu stats holds no
 * records for the specified frequency.
 * @param stats[in]   the cpu stats.
 * @param freq[in]    the cpu frequency.
 * @return            the cpu stat record (or NULL if no records were found and
 *                    failed to add a new one).
 */
static cpu_stats_record_t*
cpu_stats_get_freq_record(cpu_stats_t* stats, int freq) {
  cpu_stats_record_t** new_rec = stats;
  cpu_stats_record_t* rec = *stats;

  while (rec && rec->freq != freq) {
    if (!rec->next) {
      new_rec = &rec->next;
      break;
    }
    rec = rec->next;
  }
  if (!(*new_rec)) {
    *new_rec = rec = malloc(sizeof(cpu_stats_record_t));
    if (!rec) return NULL;
    rec->freq = freq;
    rec->ticks = 0;
    rec->next = NULL;
  }
  return rec;
}

/**
 * Releases cpu stats data structures.
 *
 * @param stats[in]   the cpu stats.
 * @return
 */
static void
cpu_stats_clear(cpu_stats_t* stats) {
  cpu_stats_record_t* rec = *stats;
  while (rec) {
    cpu_stats_t tmp = rec->next;
    free(rec);
    rec = tmp;
  }
  *stats = NULL;
}

/**
 * Sets ticks value for the specified frequency.
 *
 * @param stats[in]   the cpu stats.
 * @param freq[in]    the cpu frequency.
 * @param ticks[in]   the cpu ticks spent in the specified frequency.
 */
static void
cpu_stats_set_freq_ticks(cpu_stats_t* stats, int freq, int ticks) {
  cpu_stats_record_t* record = cpu_stats_get_freq_record(stats, freq);
  if (record) {
    record->ticks = ticks;
  }
}


/**
 * Calculates average cpu frequency between two cpu stats snapshots.
 *
 * @param start[in]   the starting cpu snapshot.
 * @param end[in]     the ending cpu snapshot.
 * @return            the average cpu frequency.
 */
static int
cpu_stats_get_avg_diff(cpu_stats_t* start, cpu_stats_t* end) {
  cpu_stats_record_t* rec_end = *end;
  int total_freq = 0;
  int total_time = 0;
  while (rec_end) {
    cpu_stats_record_t* record = cpu_stats_get_freq_record(start, rec_end->freq);
    if (record) {
      int diff = rec_end->ticks - record->ticks;
      total_time += diff;
      total_freq += rec_end->freq * diff;
    }
    rec_end = rec_end->next;
  }
  return total_time ? total_freq / total_time : 0;
}

/**
 * Parses the cpu stats file and creates cpu stats snapshot.
 *
 * @param stats[in, out]    the cpu stats.
 * @return
 */
static void
cpu_stats_take_snapshot(cpu_stats_t* stats) {
  FILE* fp = fopen(CPU_STATS_SOURCE, "r");
  if (fp) {
    char line[128];
    int freq, ticks;
    while (fgets(line, sizeof(line), fp)) {
      if (sscanf(line, "%d %d", &freq, &ticks) == 2) {
        cpu_stats_set_freq_ticks(stats, freq, ticks);
      }
    }
    fclose(fp);
  }
}


/* Does @s1 begin with @s2?
 */
static bool
begins_with(const char* s1, const char* s2)
{
	while (*s1 && *s2) {
		if (*s1++ != *s2++) return false;
	}
	// If we reached the end of @s2, then @s1 did begin with @s2.
	return *s2==0;
}

/* Truncate long strings by replacing last three characters with '...'.
 * Always returns a string.
 */
static const char*
str_truncate(const char* str, unsigned max)
{
	if (!str) return "";
	size_t len = strlen(str);
	if (len <= (size_t)max) return str;
	if (len >= dynbuf_cap) {
		void* p = NULL;
		if ((p = realloc(dynbuf, len+1)) == NULL) return "";
		dynbuf = (char*)p;
		dynbuf_cap = len+1;
	}
	strncpy(dynbuf, str, max-3);
	dynbuf[max-1] = dynbuf[max-2] = dynbuf[max-3] = '.';
	dynbuf[max] = 0;
	return dynbuf;
}

/* Returns the command line for the PID, by parsing /proc/pid/cmdline. It is
 * used as the process name for the PID, because for example with Maemo
 * Launcher the filename of the executable will not be meaningful. NULL bytes
 * from the strings are replaced with whitespace and path is removed from
 * the command name. Except for last, this should give the same result as:
 *
 *    $ tr '\0' ' ' < /proc/self/cmdline
 *
 * The string is allocated dynamically, and must be manually freed. In case of
 * any error, NULL is returned.
 */
static char*
cmdline(unsigned pid)
{
	FILE* fp = NULL;
	char* line = NULL, *base;
	size_t line_n = 0;
	ssize_t read = 0, i = 0;

	if (asprintf(&line, "/proc/%u/cmdline", pid) == -1) goto error;
	if ((fp = fopen(line, "r")) == NULL) goto error;
	line_n = strlen(line)+1;
	if ((read = getline(&line, &line_n, fp)) < 1) goto error;
	if ((base = strrchr(line, '/'))) {
		int offset = base - line - 1;
		memmove(line, base + 1, read - offset);
		read -= offset;
	}
	for (i=0; i < read-1; ++i) {
		if (line[i] == 0) line[i] = ' ';
	}
	goto done;
error:
	if (line) { free(line); line = NULL; }
done:
	if (fp) fclose(fp);
	return line;
}

/* Returns the process name, as reported by /proc/pid/status, for example:
 *
 *    $ grep Name: /proc/2/status
 *    Name:   kthreadd
 *
 * We use this if the command line happens to be empty, which is the case for
 * example with kernel threads.
 *
 * The string is allocated dynamically, and must be manually freed. In case of
 * any error, NULL is returned.
 */
static char*
process_name(unsigned pid)
{
	FILE* fp = NULL;
	char* line = NULL;
	char* name = NULL;
	size_t line_n = 0;
	ssize_t read = 0, i = 0;
	int got = 0;
	if ((got = asprintf(&line, "/proc/%u/status", pid)) == -1) goto error;
	line_n = got;
	if ((fp = fopen(line, "r")) == NULL) goto error;
	if ((read = getline(&line, &line_n, fp)) < 1) goto error;
	if (!begins_with(line, "Name:")) goto error;
	for (i=5; isblank(line[i]); ++i) ;
	if (line[i] == 0) goto error;
	if (line[read-1] == '\n') line[read-1] = 0;
	if (asprintf(&name, "[%s]", line+i) == -1) goto error;
	goto done;
error:
	if (name) { free(name); name = NULL; }
done:
	free(line);
	if (fp) fclose(fp);
	return name;
}

static char*
pid2name(unsigned pid)
{
	char* name = NULL;
	if ((name = cmdline(pid)) == NULL) {
		name = process_name(pid);
	}
	return name;
}

static const char smaps_private[] = "Private_";
static const char smaps_pclean[]  = "Clean: ";
static const char smaps_pdirty[]  = "Dirty: ";
static const char smaps_swap[]    = "Swap: ";

/* Updates per-process (private) Clean and Dirty memory usage values. If swap
 * is enabled, we add that value to Dirty, because those pages go to swap
 * first. It can be incorrect (shared pages can be swapped as well), but it's
 * good enough in practise.
 *
 * All data is taken from /proc/<pid>/smaps.
 *
 * NOTE: When monitoring processes, most CPU time that this tool uses will be
 * spent in this function.
 */
static void
update_process_memstats(monitored_process_t* process)
{
	FILE* fp = NULL;
	size_t mem_clean=0, mem_dirty=0;
	if ((fp = fopen(process->smaps_path, "r")) == NULL) goto done;
	while (getline(&dynbuf, &dynbuf_cap, fp) != -1) {
		if (*dynbuf != 'P' && *dynbuf != 'S') continue;
		if (begins_with(dynbuf, smaps_private)) {
			if (begins_with(dynbuf+sizeof(smaps_private)-1,
						smaps_pclean)) {
				mem_clean += strtoul(dynbuf+
						sizeof(smaps_private)+
						sizeof(smaps_pclean)-2,
						NULL, 10);
			} else if (begins_with(dynbuf+sizeof(smaps_private)-1,
						smaps_pdirty)) {
				mem_dirty += strtoul(dynbuf+
						sizeof(smaps_private)+
						sizeof(smaps_pdirty)-2,
						NULL, 10);
			}
		} else if (begins_with(dynbuf, smaps_swap)) {
			mem_dirty += strtoul(dynbuf+sizeof(smaps_swap)-1,
					NULL, 10);
		}
	}
done:
	process->mem_change =
		(ssize_t)(mem_clean + mem_dirty) -
		(ssize_t)(process->mem_clean + process->mem_dirty);
	process->mem_clean = mem_clean;
	process->mem_dirty = mem_dirty;
	if (fp) fclose(fp);
}

static void
update_process_cpustats(monitored_process_t* process)
{
	FILE* fp = NULL;
	char* p = NULL;
	unsigned idx = 0;
	size_t utime=0, stime=0;
	if ((fp = fopen(process->stat_path, "r")) == NULL) goto done;
	if (getline(&dynbuf, &dynbuf_cap, fp) == -1) goto done;
	// Handle case where binary name contains spaces.
	if ((p = strrchr(dynbuf, ')')) == NULL) goto done;
	++p;
	idx = 2;
	while ((p = strchr(p+1, ' ')) != NULL) {
		++idx;
		if (idx == 13) {
			if (sscanf(p, "%zu", &utime) != 1)
				goto done;
			continue;
		}
		if (idx == 14) {
			if (sscanf(p, "%zu", &stime) != 1)
				goto done;
			break;
		}
	}
done:
	// Handle processes that died while monitoring.
	if (stime+utime >= process->cputicks_total) {
		process->cputicks_change =
			(stime + utime) -
			(process->cputicks_total);
	} else {
		process->cputicks_change = 0;
	}
	process->cputicks_total = stime + utime;
	if (fp) fclose(fp);
}

static void
update_processes(monitored_process_t* mprocs, unsigned mprocs_cnt)
{
	for (unsigned i=0; !quit && i < mprocs_cnt; ++i) {
		update_process_memstats(&mprocs[i]);
		update_process_cpustats(&mprocs[i]);
	}
}

/* Get system memory totals from /proc/meminfo.
 */
static bool
system_memory_totals(size_t* ram_total, size_t* swap_total)
{
	MEMINFO query[] = {
		{ "MemTotal:",  0 },
		{ "SwapTotal:", 0 },
	};
	if (parse_proc_meminfo(query, sizeof(query)/sizeof(query[0]))
			!= sizeof(query)/sizeof(query[0])) {
		return false;
	}
	*ram_total  = query[0].value;
	*swap_total = query[1].value;
	return true;
}

/* Get system used memory from /proc/meminfo.
 */
static bool
system_ram_used(size_t ram_total, size_t* ram_used)
{
	MEMINFO query[] = {
		{ "MemFree:",   0 },
		{ "Buffers:",   0 },
		{ "Cached:",    0 },
	};
	if (parse_proc_meminfo(query, sizeof(query)/sizeof(query[0]))
			!= sizeof(query)/sizeof(query[0])) {
		return false;
	}
	*ram_used = ram_total - query[0].value - query[1].value - query[2].value;
	return true;
}

/* Get CPU ticks from /proc/stat. We are only interested in the line:
 *
 *    cpu  838113 15940 166151 69829927 155772 1484 2638 0 0
 *                                 ^
 *                                  \
 *                                   `- idle ticks
 *
 * The total number of ticks is obtained by summing together all the integers.
 */
static void
system_cpu_usage(size_t* ticks_total, size_t* ticks_idle)
{
	FILE* fp = NULL;
	size_t total=0, idle=0;
	fp = fopen("/proc/stat", "r");
	if (!fp) goto done;
	while (getline(&dynbuf, &dynbuf_cap, fp) != -1) {
		if (strstr(dynbuf, "cpu ") != NULL) {
			char* p = dynbuf + 4;
			char* end = NULL;
			unsigned idx = 0;
			unsigned long value = 0;
			errno = 0;
			while (true) {
				value = strtoul(p, &end, 10);
				if (errno || p == end) break;
				p = end;
				total += value;
				if (idx == 3) idle = value;
				++idx;
			}
			goto done;
		}
	}
done:
	*ticks_total = total;
	*ticks_idle  = idle;
	if (fp) fclose(fp);
}

/* Per-PID column coloring string.
 */
static const char* c_begin(unsigned i)
{ return (colors && i%2==0) ? COLOR_PROCESS : ""; }
static const char* c_end(unsigned i)
{ return (colors && i%2==0) ? COLOR_CLEAR   : ""; }

/* Prints monitored PIDS and process names, and returns how many lines were
 * printed in total.
 */
static unsigned
print_process_names(monitored_process_t* mprocs, unsigned mprocs_cnt)
{
	for (unsigned i=0; i < mprocs_cnt; ++i) {
		fprintf(output, "%sPID %5u: %s%s\n",
			c_begin(i),
			mprocs[i].pid,
			mprocs[i].name ? mprocs[i].name : UNKNOWN_PROCESS_NAME,
			c_end(i));
	}
	return mprocs_cnt;
}

/* Prints the headers, and returns how many lines were printed in total.
 *
 *   @watermaks_avail    Determines whether we print the BL column.
 */
static unsigned
print_headers(monitored_process_t* mprocs,
              unsigned mprocs_cnt,
              bool watermarks_avail)
{
	unsigned i;
	// First line.
	fprintf(output, "%s            _______________  ____________ ",
			watermarks_avail ? "   " : "");
	for (i=0; i < mprocs_cnt; ++i) {
		fprintf(output, "%s _____________________________ %s",
			c_begin(i), c_end(i));
	}
	fprintf(output, "\n");
	// Second line.
	fprintf(output, "________%s / system memory \\/ system CPU \\",
			watermarks_avail ? "  __ " : "_ ");
	for (i=0; i < mprocs_cnt; ++i) {
		fprintf(output, "%s/PID %-5u %-19s\\%s",
			c_begin(i),
			mprocs[i].pid,
			str_truncate(mprocs[i].name, 19),
			c_end(i));
	}
	fprintf(output, "\n");
	// Third line.
	fprintf(output, "time: %s\\/  used:  change:     %%:  MHz: ",
			watermarks_avail ? "  \\/BL" : "   ");
	for (i=0; i < mprocs_cnt; ++i) {
		fprintf(output, "%s  clean:  dirty: change: CPU-%%:%s",
			c_begin(i), c_end(i));
	}
	fprintf(output, "\n");
	return 3;
}

/* Add @pid to the collection of PIDs we shall monitor.
 */
static void
monitor_pid(unsigned pid,
            monitored_process_t** mprocs,
            unsigned* mprocs_cnt,
            unsigned* mprocs_cap)
{
	if (pid == 0) {
		fprintf(stderr, "ERROR: invalid PID\n");
		exit(1);
	}
	if (*mprocs_cnt >= *mprocs_cap) {
		*mprocs_cap *= 2;
		if (*mprocs_cap < 4) *mprocs_cap = 4;
		*mprocs = realloc(*mprocs, *mprocs_cap * sizeof(monitored_process_t));
		if (*mprocs == NULL) {
			fprintf(stderr, "ERROR: realloc() failure\n");
			exit(1);
		}
	}
	memset(*mprocs + *mprocs_cnt, 0, sizeof(monitored_process_t));
	(*mprocs)[*mprocs_cnt].pid = pid;
	if (asprintf(&((*mprocs)[*mprocs_cnt].smaps_path), "/proc/%u/smaps", pid) == -1) {
		fprintf(stderr, "ERROR: asprintf() failure\n");
		exit(1);
	}
	if (asprintf(&((*mprocs)[*mprocs_cnt].stat_path), "/proc/%u/stat", pid) == -1) {
		fprintf(stderr, "ERROR: asprintf() failure\n");
		exit(1);
	}
	(*mprocs)[*mprocs_cnt].name = pid2name(pid);
	*mprocs_cnt += 1;
}

static void
usage()
{
	fprintf(stderr,
		"%s is a lightweight tool for monitoring the status of your system\n"
		"and (optionally) the status of some processes.\n"
		"\n"
		"Usage:\n"
		"        %s [OPTIONS] [interval] [[PID] [PID...]]\n"
		"\n"
		"Default output interval is %u seconds.\n"
		"\n"
		"     -p, --pid=PID         Monitor process identified with PID.\n"
		"     -f, --file=FILE       Write to FILE instead of stdout.\n"
		"         --no-colors       Disable colors.\n"
		"         --self            Monitor this instance of %s.\n"
		"     -i, --interval=INTERVAL         Data acquisition interval.\n"
		"     -C, --system-cpu-change=THRESHOLD         Perform output only when the system cpu usage is greater then the specified threshold.\n"
		"     -M, --system-mem-change=THRESHOLD          Perform output only when the system memory change is greater then the specified threshold.\n"
		"     -c, --cpu-changes          Perform output only when there was any change in cpu usage for any process being monitored.\n"
		"     -m, --mem-changes          Perform output only when there was any change in memory usage for any process being monitored.\n"
		"     -h, --help            Display this help.\n"
		"\n"
		"Examples:\n"
		"\n"
		"   Monitor system memory and CPU usage with default interval:\n"
		"        %s\n"
		"\n"
		"   Monitor all bash shells with 2 second interval:\n"
		"        %s 2 $(pidof bash)\n"
		"\n"
		"   Monitor PIDS 1234 and 5678 with default interval:\n"
		"        %s -p 1234 -p 5678\n"
		"\n",
		progname, progname, DEFAULT_SLEEP_INTERVAL, progname, progname,
		progname, progname);
}

static struct option long_opts[] = {
	{"help", 0, 0, 'h'},
	{"no-colors", 0, 0, 1001},
	{"file", 1, 0, 'f'},
	{"pid", 1, 0, 'p'},
	{"self", 0, 0, 1002},
	{"mem-changes", 0, 0, 'm'},
	{"cpu-changes", 0, 0, 'c'},
	{"system-cpu-change", 1, 0, 'C'},
	{"system-mem-change", 1, 0, 'M'},
	{"interval", 1, 0, 'i'},
	{0,0,0,0}
};

static void
parse_cmdline(int argc, char** argv,
              monitored_process_t** mprocs, unsigned* mprocs_cnt,
              unsigned* sleep_interval)
{
	int opt;
	char* output_path = NULL;
	unsigned cnt=0, cap=0;
	monitored_process_t* m = NULL;
	while ((opt = getopt_long(argc, argv, "p:hf:mcM:C:i:", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'f':
			output_path = strdup(optarg);
			break;
		case 'h':
			usage();
			exit(1);
		case 1001:
			colors = false;
			break;
		case 1002:
			monitor_pid(getpid(), &m, &cnt, &cap);
			break;
		case 'p':
			monitor_pid(atoi(optarg), &m, &cnt, &cap);
			break;
		case 'm':
			ADD_OPTION_VALUE_FLAG(option_flags, OF_PROC_MEM_CHANGES_ONLY);
			break;
		case 'c':
			ADD_OPTION_VALUE_FLAG(option_flags, OF_PROC_CPU_CHANGES_ONLY);
			break;
		case 'M':
			ADD_OPTION_VALUE_FLAG(option_flags, OF_SYS_MEM_CHANGES_ONLY);
			sys_mem_change_threshold = atoi(optarg);
			do_print_report_default = false;
			break;
		case 'C':
			ADD_OPTION_VALUE_FLAG(option_flags, OF_SYS_CPU_CHANGES_ONLY);
			if (!sscanf(optarg, "%f", &sys_cpu_change_threshold) ) {
				fprintf(stderr, "ERROR: invalid CPU change threshold for the system\n");
				exit(1);
			}
			do_print_report_default = false;
			break;
		case 'i':
			if (sscanf(optarg, "%u", sleep_interval) != 1 ||
				sleep_interval == 0u) {
				fprintf(stderr, "ERROR: invalid interval\n");
				exit(1);
			}
			ADD_OPTION_VALUE_FLAG(option_flags, OF_INTERVAL_OPTION_SET);
			break;
		default:
			usage();
			exit(1);
		}
	}
	if (output_path) {
		FILE* fp = fopen(output_path, "a");
		if (!fp) {
			perror("ERROR: unable to open output file");
			exit(1);
		}
		output = fp;
		free(output_path);
	} else {
		output = stdout;
	}
	if (optind < argc) {
		// read interval argument
		unsigned _interval = 0;
		if (sscanf(argv[optind], "%u", &_interval) != 1 ||
			sleep_interval == 0u) {
			fprintf(stderr, "ERROR: invalid interval\n");
			exit(1);
		}
		// only overwrite interval if interval is not already specified by -i option
		if (!IS_OPTION_VALUE_FLAG_SET(option_flags, OF_INTERVAL_OPTION_SET) ) {
			 *sleep_interval = _interval;
		} else {
			fprintf(stderr, "WARNING: interval argument '%u' is ignored as '-i option' is specified\n", _interval);
		}
		++optind;
	}
	while (optind < argc) {
		monitor_pid(atoi(argv[optind]), &m, &cnt, &cap);
		++optind;
	}
	if (cnt) {
		*mprocs = m;
		*mprocs_cnt = cnt;
	}
	
	// determine if no printing needs to be done by default 
	if (*mprocs_cnt > 0 && 
		(IS_OPTION_VALUE_FLAG_SET(option_flags, OF_PROC_MEM_CHANGES_ONLY) || 
		 IS_OPTION_VALUE_FLAG_SET(option_flags, OF_PROC_CPU_CHANGES_ONLY) ) ) {

		do_print_report_default = false;
	}
}

/* When printing results to console, we want to periodically reprint the
 * headers. In order to properly do this, we'll need the size of the user's
 * terminal.
 */
static unsigned
win_rows()
{
	struct winsize w;
	(void) memset(&w, 0, sizeof(struct winsize));
	int fd = fileno(output);
	if (fd == -1) goto error;
	if (ioctl(fd, TIOCGWINSZ, &w) == -1) goto error;
	return w.ws_row;
error:
	return 0;
}

/* Gives system wide CPU usage as percentage. This function will be called once
 * per round with the CPU tick differences, so the parameter values are assumed
 * to be relatively small.
 *
 *   @total_ticks    Difference between total CPU ticks between the start of
 *                   round and end of round.
 *   @idle_ticks     Difference between total CPU idle ticks between the start
 *                   of round and end of round.
 */
static float
cpu_usage(unsigned total_ticks, unsigned idle_ticks)
{
	if (total_ticks == 0) return 0.0f;
	if (idle_ticks == 0) return 100.0f;
	float ret = 100 * ((float)(total_ticks - idle_ticks) / (float)total_ticks);
	if (ret > 100.0f) return 100.0f;
	return ret;
}

/* Formatted flags for the BL column:
 *
 *     ""        /sys/kernel/{low,high}_watermark not available.
 *     "--"      Low & high marks not set.
 *     "B-"      Low mark set.
 *     "-L"      Only high mark set, should not happen.
 *     "BL"      Both low & high marks set.
 */
static const char*
mem_flags(bool watermarks_avail)
{
	if (!watermarks_avail) return "";
	const bool flag_low  = check_flag(watermark_low);
	const bool flag_high = check_flag(watermark_high);
	if (flag_low && flag_high) {
		if (colors) return COLOR_HIGHMARK " BL" COLOR_CLEAR;
		else        return " BL";
	}
	if (flag_low) {
		if (colors) return COLOR_LOWMARK " B-" COLOR_CLEAR;
		else        return " B-";
	}
	if (flag_high) {
		// Only highmark set? Should not happen...
		if (colors) return COLOR_HIGHMARK " -L" COLOR_CLEAR;
		else        return " -L";
	}
	return " --";
}

int main(int argc, char** argv)
{
	unsigned sleep_interval = DEFAULT_SLEEP_INTERVAL;
	unsigned ram_total=0, swap_total=0;
	unsigned ram_used=0, prev_ram_used=0;
	size_t cpu_ticks_total=0, cpu_ticks_idle=0;
	size_t cpu_ticks_total_prev=0, cpu_ticks_idle_prev=0;
	monitored_process_t* mprocs = NULL;
	unsigned mprocs_cnt = 0;
	bool watermarks_avail = false, is_atty = false;
	unsigned rows=0, lines_printed=0;
	cpu_stats_t* cpu_stats_swap;

	parse_cmdline(argc, argv, &mprocs, &mprocs_cnt, &sleep_interval);
	(void) nice(-19);
	if (!system_memory_totals(&ram_total, &swap_total)) {
		fprintf(stderr, "ERROR: unable to get MemTotal and SwapTotal from /proc/meminfo\n");
		return 1;
	}
	if (!system_ram_used(ram_total, &ram_used)) {
		fprintf(stderr, "ERROR: unable to read /proc/meminfo\n");
		return 1;
	}
	prev_ram_used = ram_used;
	system_cpu_usage(&cpu_ticks_total, &cpu_ticks_idle);
	cpu_ticks_total_prev = cpu_ticks_total;
	cpu_ticks_idle_prev  = cpu_ticks_idle;
	update_processes(mprocs, mprocs_cnt);
	for (unsigned i=0; i < mprocs_cnt; ++i) {
		mprocs[i].mem_change = 0;
		mprocs[i].cputicks_change = 0;
	}
	if (access(watermark_low, R_OK) == 0 &&
	    access(watermark_high, R_OK) == 0) {
		watermarks_avail = true;
	}
	is_atty = isatty(fileno(output));
	if (!is_atty) colors = false;
	fprintf(output, "System total memory: %u kB RAM, %u kB swap\n",
			ram_total, swap_total);
	
	(void) print_process_names(mprocs, mprocs_cnt);
	lines_printed += print_headers(mprocs, mprocs_cnt, watermarks_avail);
	// Disable header reprinting if we're printing to console, or if the
	// screen seems to be very small.
	if (is_atty) { rows = win_rows(); if (rows < 10+mprocs_cnt) rows = 0; }
	// Install our signal handler, unless someone specifically wanted
	// SIGINT to be ignored.
	if (signal(SIGINT, quit_app) == SIG_IGN) signal(SIGINT, SIG_IGN);
	
	bool do_print_report;
	
	unsigned sys_ram_used_last_printed = ram_used;
	float sys_cpu_usage_last_printed = cpu_usage(cpu_ticks_total-cpu_ticks_total_prev, cpu_ticks_idle-cpu_ticks_idle_prev);
	
  /* initialize cpu stats */
  cpu_stats_take_snapshot(cpu_stats_start);

	while (!quit) {

		do_print_report = do_print_report_default;
		
		const time_t t = time(NULL);
		const struct tm* ts = localtime(&t);
		if (!ts) {
			fprintf(stderr, "ERROR: localtime() failed\n");
			return 1;
		}
		// check system for changes in mem and cpu
		int _sys_ram_change = (int)ram_used - (int)sys_ram_used_last_printed;
		float _sys_cpu_usage = cpu_usage(cpu_ticks_total-cpu_ticks_total_prev, cpu_ticks_idle-cpu_ticks_idle_prev);
		float _sys_cpu_usage_change = _sys_cpu_usage - sys_cpu_usage_last_printed; 
		
		if ( (IS_OPTION_VALUE_FLAG_SET(option_flags, OF_SYS_MEM_CHANGES_ONLY) && abs(_sys_ram_change) >= sys_mem_change_threshold) || 
			(IS_OPTION_VALUE_FLAG_SET(option_flags, OF_SYS_CPU_CHANGES_ONLY) && fabs(_sys_cpu_usage_change) >= sys_cpu_change_threshold) ) {

			do_print_report = true;
		}
		
		if ( !do_print_report ) {
			// check all monitored processes for changes in mem and cpu
			for (unsigned i = 0; i < mprocs_cnt; ++i) {
				if ( (IS_OPTION_VALUE_FLAG_SET(option_flags, OF_PROC_MEM_CHANGES_ONLY) && mprocs[i].mem_change != 0) ||
					 
					 (IS_OPTION_VALUE_FLAG_SET(option_flags, OF_PROC_CPU_CHANGES_ONLY) && mprocs[i].cputicks_change != 0.0f) ) {
					
					do_print_report = true;
					break;
				}
			}
		}
			
		// print report for the system
		if (do_print_report) {
			fprintf(output,
				"%02u:%02u:%02u %s%9u %+8d %6.2f",
				ts->tm_hour, ts->tm_min, ts->tm_sec,
				mem_flags(watermarks_avail),
				ram_used, _sys_ram_change, _sys_cpu_usage);

	    /* Print cpu stats. */
	    cpu_stats_take_snapshot(cpu_stats_end);
	    fprintf(output, "%6d", cpu_stats_get_avg_diff(cpu_stats_start, cpu_stats_end) / 1000);
	    /* Because cpu stats can only grow we can simply swap between the start and end stats.
	     * The end stats will become the start stats and the old start stats data structures will be used
	     * to take new cpu stats snapshot, making it the new end stats.
	     */
	    cpu_stats_swap = cpu_stats_end;
	    cpu_stats_end = cpu_stats_start;
	    cpu_stats_start = cpu_stats_swap;
				
			sys_ram_used_last_printed = ram_used;
			sys_cpu_usage_last_printed = _sys_cpu_usage;
			
			// print report for the processes
			for (unsigned i=0; i < mprocs_cnt; ++i) {
				fprintf(output, "%s %7u %7u %+7d %6.2f%s",
					c_begin(i),
					mprocs[i].mem_clean,
					mprocs[i].mem_dirty,
					mprocs[i].mem_change,
					cpu_usage(cpu_ticks_total-cpu_ticks_total_prev,
							  (cpu_ticks_total-cpu_ticks_total_prev) -
							   mprocs[i].cputicks_change),
					c_end(i));
			}
			fprintf(output, "\n");
			fflush(output);
		}
				
		sleep(sleep_interval);
		if (quit) break;
		prev_ram_used = ram_used;
		if (!system_ram_used(ram_total, &ram_used)) {
			fprintf(stderr, "ERROR: unable to read /proc/meminfo\n");
			return 1;
		}
		
		cpu_ticks_total_prev = cpu_ticks_total;
		cpu_ticks_idle_prev  = cpu_ticks_idle;
		system_cpu_usage(&cpu_ticks_total, &cpu_ticks_idle);
		update_processes(mprocs, mprocs_cnt);
		
		if (do_print_report) {
			if (is_atty && rows) {
				if (++lines_printed >= rows-1) {
					lines_printed = print_headers(mprocs,
							mprocs_cnt, watermarks_avail);
				}
			}
		}
	}
	for (unsigned i=0; i < mprocs_cnt; ++i) {
		free(mprocs[i].smaps_path);
		free(mprocs[i].stat_path);
		free(mprocs[i].name);
	}
	free(mprocs);
	free(dynbuf);

	/* release cpu stats data */
	cpu_stats_clear(&cpu_stats[0]);
	cpu_stats_clear(&cpu_stats[1]);
}

/* ========================================================================= *
 *            No more code in mem-cpu-monitor.c                              *
 * ========================================================================= */
