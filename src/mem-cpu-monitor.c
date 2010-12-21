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
 * Copyright (C) 2005-2010 by Nokia Corporation
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
#include <dirent.h>
#include <sys/wait.h>

#include <sp_measure.h>

#include "sp_report.h"


static const char progname[] = "mem-cpu-monitor";

// Output to stdout by default, otherwise to file given by user.
static FILE* output = NULL;

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

// Show some colors if we're printing to console.
static bool colors = true;
#define COLOR_CLEAR    "\033[0m"
#define COLOR_LOWMARK  "\033[33m"
#define COLOR_HIGHMARK "\033[31m"
#define COLOR_PROCESS  "\033[32m"

#define COLORIZE(prefix, text, postfix) 	(colors ? prefix text postfix : text)

#define DEFAULT_SLEEP_INTERVAL 3000000u

#define PROCESS_NAME(proc) (proc->common->name ? proc->common->name : "<unknown>")

#define HEADER_TITLE_TIMESTAMP   "time:"

// Die gracefully when we get interrupted with Ctrl-C. Makes it easier to see
// memory leaks with Valgrind.
static volatile sig_atomic_t quit = 0;
static void quit_app(int sig) { (void)sig; if (quit++) _exit(1); }

/* a mark to print for process data when process is not available */
#define NO_DATA    "n/a"


static void
usage()
{
	fprintf(stderr,
		"%s is a lightweight tool for monitoring the status of your system\n"
		"and (optionally) the status of some processes.\n"
		"\n"
		"Usage:\n"
		"        %s [OPTIONS] [[PID] [PID...]]\n"
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
		"     -c, --cpu-change          Perform output only when there was any change in cpu usage for any process being monitored.\n"
		"     -m, --mem-change          Perform output only when there was any change in memory usage for any process being monitored.\n"
		"     -n, --name=NAME       Monitor processes starting with NAME.\n"
		"     -h, --help            Display this help.\n"
		"     -x, --exec=CMD        Executes and starts monitoring the CMD command line.\n"
		"\n"
		"Examples:\n"
		"\n"
		"   Monitor system memory and CPU usage with default interval:\n"
		"        %s\n"
		"\n"
		"   Monitor all bash shells with 2 second interval:\n"
		"        %s -i 2 $(pidof bash)\n"
		"\n"
		"   Monitor PIDS 1234 and 5678 with default interval:\n"
		"        %s -p 1234 -p 5678\n"
		"\n",
		progname, progname, DEFAULT_SLEEP_INTERVAL / 1000000, progname, progname,
		progname, progname);
}

static struct option long_opts[] = {
	{"help", 0, 0, 'h'},
	{"no-colors", 0, 0, 1001},
	{"file", 1, 0, 'f'},
	{"pid", 1, 0, 'p'},
	{"self", 0, 0, 1002},
	{"mem-change", 0, 0, 'm'},
	{"cpu-change", 0, 0, 'c'},
	{"system-cpu-change", 1, 0, 'C'},
	{"system-mem-change", 1, 0, 'M'},
	{"interval", 1, 0, 'i'},
	{"name", 1, 0, 'n'},
	{"exec", 1, 0, 'x'},
	{0,0,0,0}
};

/**
 * Structure to store monitored process name.
 */
typedef struct proc_name_t {
	char* name;
	size_t size;
} proc_name_t;


/**
 * Process data structure.
 *
 * Holds process snapshots, header and reference to the
 * application data structure.
 *
 * This structure is used as data for process column printing
 * functions.
 */
typedef struct proc_data_t {
	/* process command line */
	char cmdline[256];
	/* process snapshot data */
	sp_measure_proc_data_t data[2];
	sp_measure_proc_data_t* data1;
	sp_measure_proc_data_t* data2;

	bool has_data;

	int resource_flags;

	sp_report_header_t* header;

	struct app_data_t* app_data;

	struct proc_data_t* next;
} proc_data_t;


/**
 * Application data structure.
 *
 * Holds system snapshots, list of processes, root header and
 * a reference to watermark header.
 *
 * This structure is used as data for system column printing
 * functions.
 */
typedef struct app_data_t {
	sp_measure_sys_data_t sys_data[2];
	sp_measure_sys_data_t* sys_data1;
	sp_measure_sys_data_t* sys_data2;

	int resource_flags;

	proc_data_t* proc_list;
	int proc_count;

	sp_report_header_t root_header;
	sp_report_header_t* watermark_header;

	/* process monitoring by name */
	proc_name_t name_list[32];
	int name_index;
	
	unsigned long sleep_interval;
	bool timestamp_print_msecs;

	// Bitmask holding a number of option flags
	unsigned int option_flags;
} app_data_t;

/*
 * Writer functions used to output the system/process statistics.
 */

/**
 * Writes system timestamp.
 */
int
write_sys_timestamp(char* buffer, int size, void* args)
{
	app_data_t* data = (app_data_t*)args;
	int timestamp = FIELD_SYS_TIMESTAMP(data->sys_data2);
	int hours = timestamp / (60 * 60 * 1000);
	timestamp %= 60 * 60 * 1000;
	int minutes = timestamp / (60 * 1000);
	timestamp %= 60 * 1000;
	int seconds = timestamp / 1000;
	if (data->timestamp_print_msecs) {
		int msecs = timestamp % 1000;
		return snprintf(buffer, size + 1, "%02d:%02d:%02d.%03d", hours, minutes, seconds, msecs);
	}
	return snprintf(buffer, size + 1, "%02d:%02d:%02d", hours, minutes, seconds);
}

/**
 * Writes memory watermark information.
 *
 * Memory watermarks are maemo5 specific.
 */
int
write_sys_mem_watermark(char* buffer, int size __attribute((unused)), void* args)
{
	app_data_t* data = (app_data_t*)args;
	int flag_high = FIELD_SYS_MEM_WATERMARK(data->sys_data2) & MEM_WATERMARK_HIGH;
	int flag_low = FIELD_SYS_MEM_WATERMARK(data->sys_data2) & MEM_WATERMARK_LOW;
	if (flag_low) {
		if (flag_high) {
			strcpy(buffer, COLORIZE(COLOR_HIGHMARK, "BL", COLOR_CLEAR));
		}
		else {
			strcpy(buffer, COLORIZE(COLOR_LOWMARK, "B-", COLOR_CLEAR));
		}
	}
	else if (flag_high) {
		strcpy(buffer, COLORIZE(COLOR_HIGHMARK, "-L", COLOR_CLEAR));
	}
	else {
		strcpy(buffer, "--");
	}
	return 2;
}


/**
 * Writes used system memory information.
 */
int
write_sys_mem_used(char* buffer, int size, void* args)
{
	app_data_t* data = (app_data_t*)args;
	if (FIELD_SYS_MEM_USED(data->sys_data2) == ESPMEASURE_UNDEFINED) {
		strcpy(buffer, NO_DATA);
		return sizeof(NO_DATA) - 1;
	}
	return snprintf(buffer, size + 1, "%8d", FIELD_SYS_MEM_USED(data->sys_data2));	return 0;
}

/**
 * Writes used system memory change.
 */
int
write_sys_mem_change(char* buffer, int size, void* args)
{
	app_data_t* data = (app_data_t*)args;
	int value;
	if (sp_measure_diff_sys_mem_used(data->sys_data1, data->sys_data2, &value) == 0) {
		return snprintf(buffer, size + 1, "%+6d", value);
	}
	strcpy(buffer, NO_DATA);
	return sizeof(NO_DATA) - 1;
}

/**
 * Writes system cpu usage data.
 */
int
write_sys_cpu_usage(char* buffer, int size, void* args)
{
	app_data_t* data = (app_data_t*)args;
	int value;
	if (sp_measure_diff_sys_cpu_usage(data->sys_data1, data->sys_data2, &value) == 0) {
		return snprintf(buffer, size + 1, "%5.1f%%", (float)value / 100);
	}
	strcpy(buffer, NO_DATA);
	return sizeof(NO_DATA) - 1;
}

/**
 * Writes average cpu frequency data/
 */
int
write_sys_cpu_freq(char* buffer, int size, void* args)
{
	static int last_cpu_freq = 0;
	app_data_t* data = (app_data_t*)args;
	int value;
	if (sp_measure_diff_sys_cpu_avg_freq(data->sys_data1, data->sys_data2, &value) == 0) {
		if (value) {
			last_cpu_freq = value;
		}
		else {
			value = last_cpu_freq;
		}
		return snprintf(buffer, size + 1, "%4d", value / 1000);
	}
	strcpy(buffer, NO_DATA);
	return sizeof(NO_DATA) - 1;
}

/**
 * Writes process private clean memory size (Kb).
 */
int
write_proc_mem_clean(char* buffer, int size, void* args)
{
	proc_data_t* proc = (proc_data_t*)args;
	if (!proc->has_data || FIELD_PROC_MEM_PRIVATE_CLEAN(proc->data2) == -1) {
		strcpy(buffer, NO_DATA);
		return sizeof(NO_DATA) - 1;
	}
	return snprintf(buffer, size + 1, "%8d", FIELD_PROC_MEM_PRIVATE_CLEAN(proc->data2));
}

/**
 * Writes process private dirty + swap memory size (Kb).
 */
int
write_proc_mem_dirty(char* buffer, int size, void* args)
{
	proc_data_t* proc = (proc_data_t*)args;
	if (!proc->has_data || FIELD_PROC_MEM_SWAP(proc->data2) == -1 || FIELD_PROC_MEM_PRIVATE_DIRTY(proc->data2) == -1) {
		strcpy(buffer, NO_DATA);
		return sizeof(NO_DATA) - 1;
	}
	return snprintf(buffer, size + 1, "%8d", FIELD_PROC_MEM_PRIV_DIRTY_SUM(proc->data2));
}

/**
 * Writes process private dirty + swap memory size change (Kb)
 */
int
write_proc_mem_change(char* buffer, int size, void* args)
{
	proc_data_t* proc = (proc_data_t*)args;
	int value;
	if (!proc->has_data || sp_measure_diff_proc_mem_private_dirty(proc->data1, proc->data2, &value) != 0) {
		strcpy(buffer, NO_DATA);
		return sizeof(NO_DATA) - 1;
	}
	return snprintf(buffer, size + 1, "%+7d", value);
}

/**
 * Writes process cpu usage.
 */
int
write_proc_cpu_usage(char* buffer, int size, void* args)
{
	proc_data_t* proc = (proc_data_t*)args;
	int total_ticks, proc_ticks;
	if (!proc->has_data || sp_measure_diff_sys_cpu_ticks(proc->app_data->sys_data1, proc->app_data->sys_data2, &total_ticks) != 0 ||
			                                              sp_measure_diff_proc_cpu_ticks(proc->data1, proc->data2, &proc_ticks) != 0) {
		strcpy(buffer, NO_DATA);
		return sizeof(NO_DATA) - 1;
	}
	return snprintf(buffer, size + 1, "%5.1f%%", total_ticks ? (float)proc_ticks * 100 / total_ticks : 0);
}

/*
 * End of writer functions.
 */


/*
 * Executes expression and checks the return value for warnings/errors
 */
#define CHECK_SNAPSHOT_RC(expression, text, ...) { \
	int __rc = expression; \
	if (__rc < 0) { \
		fprintf(stderr, "ERROR: " text " Aborting.\n", __VA_ARGS__);\
		exit(-1);\
	}\
	if (__rc > 0) { \
		fprintf(stderr, "Warning: " text " Some data might be absent.\n", __VA_ARGS__);\
	}\
}

/**
 * Initializes system snapshots.
 *
 * @param self[in]   application data.
 * @return           0 for success.
 */
static int
app_data_init_sys_snapshots(app_data_t* self)
{
	int rc = 0;
	self->resource_flags = SNAPSHOT_SYS;

	CHECK_SNAPSHOT_RC(sp_measure_init_sys_data(&self->sys_data[0], self->resource_flags, NULL),
			"system /proc/ data snapshot initialization returned (%d).", rc |= __rc);

	CHECK_SNAPSHOT_RC(sp_measure_init_sys_data(&self->sys_data[1], 0, &self->sys_data[0]),
			"system /proc/ data snapshot initialization returned (%d).", rc |= __rc);

	/* take initial system snapshot */
	CHECK_SNAPSHOT_RC(sp_measure_get_sys_data(&self->sys_data[0], self->resource_flags, NULL),
			"System resource usage initial snapshot returned (%d).", rc = __rc);

	if (rc & SNAPSHOT_SYS_MEM_WATERMARK) {
		fprintf(stderr, "Note: (Maemo) kernel memory limits not found.\n");
	}

	self->resource_flags &= (~rc);

	self->sys_data1 = &self->sys_data[0];
	self->sys_data2 = &self->sys_data[1];

	return rc;
}

/**
 * Creates system information headers(columns).
 *
 * @param self[in]   application data.
 * @return           0 for success.
 */
static int
app_data_create_header(app_data_t* self)
{
	memset(&self->root_header, 0, sizeof(sp_report_header_t));
	/* timestamp header*/
	if (sp_report_header_add_child(&self->root_header, HEADER_TITLE_TIMESTAMP, 12, SP_REPORT_ALIGN_CENTER, write_sys_timestamp, (void*)self) == NULL) return -ENOMEM;

	/* watermarks header if necessary */
	if (self->resource_flags & SNAPSHOT_SYS_MEM_WATERMARK) {
		self->watermark_header = sp_report_header_add_child(&self->root_header, "BL", 2, SP_REPORT_ALIGN_CENTER, write_sys_mem_watermark, (void*)self);
		if (self->watermark_header == NULL) return -ENOMEM;
	}

	/* memory header containing used system memory and it's change from the previous snapshot columns*/
	sp_report_header_t* mem_header = sp_report_header_add_child(&self->root_header, "system memory", 0, SP_REPORT_ALIGN_CENTER, NULL, NULL);
	if (mem_header == NULL) return -ENOMEM;
	if (sp_report_header_add_child(mem_header, "used:", 10, SP_REPORT_ALIGN_RIGHT, write_sys_mem_used, (void*)self) == NULL) return -ENOMEM;
	if (sp_report_header_add_child(mem_header, "change:", 8, SP_REPORT_ALIGN_RIGHT, write_sys_mem_change, (void*)self) == NULL) return -ENOMEM;

	/* cpu header containing cpu usage and average frequency columns */
	sp_report_header_t* cpu_header = sp_report_header_add_child(&self->root_header, "system CPU", 0, SP_REPORT_ALIGN_CENTER, NULL, NULL);
	if (cpu_header == NULL) return -ENOMEM;
	if (sp_report_header_add_child(cpu_header, "%:", 6, SP_REPORT_ALIGN_RIGHT, write_sys_cpu_usage, (void*)self) == NULL) return -ENOMEM;
	if (sp_report_header_add_child(cpu_header, "MHz:", 5, SP_REPORT_ALIGN_RIGHT, write_sys_cpu_freq, (void*)self) == NULL) return -ENOMEM;

	return 0;
}

/**
 * Initializes application data.
 *
 * @param self[in]   application data.
 * @return           0 for success.
 */
static int
app_data_init(app_data_t* self)
{
	int rc;
	memset(self, 0, sizeof(app_data_t));

	if ( (rc = app_data_init_sys_snapshots(self)) < 0) return rc;
	if ( (rc = app_data_create_header(self)) != 0) return rc;

	self->sleep_interval = DEFAULT_SLEEP_INTERVAL;
	return 0;
}


/**
 * Initializes timestamp printing options.
 *
 * This function checks if the millisecond part of timestamps should be
 * printed and makes necessary header adjustments.
 * The timestamps are printed only when the decimal part of update interval
 * is specified.
 * @param self
 * @return
 */
static int
app_data_init_timestamps(app_data_t* self)
{
	self->timestamp_print_msecs = self->sleep_interval % 1000000;
	if (!self->timestamp_print_msecs) {
		sp_report_header_set_title(self->root_header.child, HEADER_TITLE_TIMESTAMP, 8, SP_REPORT_ALIGN_RIGHT);
	}
	return 0;
}

/**
 * Releases application data resources allocated by app_data_init() function/
 *
 * @param self[in]   application data.
 * @return           0 for success.
 */
static int
app_data_release(app_data_t* self)
{
	sp_measure_free_sys_data(&self->sys_data[0]);
	sp_measure_free_sys_data(&self->sys_data[1]);

	sp_report_header_free(self->root_header.child);
	self->root_header.child = NULL;

	/* free monitored process names */
	int i;
	for (i = 0; i < self->name_index; i++) {
		free(self->name_list[i].name);
	}

	return 0;
}

/**
 * Formats process header title.
 *
 * @param[in] proc     the process data structure.
 * @param[out] buffer  the output buffer.
 * @param[in] size     the output buffer size.
 * @return             the number of characters written into output buffer
 *                     not counting the trailing '\0' character.
 */
static int
proc_data_format_title(const proc_data_t* proc, char* buffer, int size)
{
	return snprintf(buffer, size, "PID %d %s", FIELD_PROC_PID(proc->data1), PROCESS_NAME(proc->data1));
}

/**
 * Creates process data structure.
 *
 * This function creates process data structure, initializes its snapshots and
 * creates output header.
 * @param pproc[out]    the created process data structure.
 * @param pid[in]       the process identifier.
 * @param app_data[in]  a reference to application data.
 * @return              0 for success.
 */
static int
proc_data_create(proc_data_t** pproc, int pid, app_data_t* app_data)
{
	char buffer[256];
	int rc = 0;
	*pproc = (proc_data_t*)malloc(sizeof(proc_data_t));
	proc_data_t* proc = *pproc;
	if (proc == NULL) return -ENOMEM;

	proc->next = NULL;
	proc->app_data = app_data;
	proc->resource_flags = SNAPSHOT_PROC;
	*proc->cmdline = '\0';

	/* initialize process snapshots */

	CHECK_SNAPSHOT_RC(sp_measure_init_proc_data(&proc->data[0], pid, SNAPSHOT_PROC, NULL),
			"proc /proc/<pid>/ data snapshot initialization returned (%d).", rc |= __rc);

	CHECK_SNAPSHOT_RC(sp_measure_init_proc_data(&proc->data[1], 0, 0, &proc->data[0]),
			"proc /proc/<pid>/ data snapshot initialization returned (%d).", rc |= __rc);

	proc->data1 = &proc->data[0];
	proc->data2 = &proc->data[1];

	CHECK_SNAPSHOT_RC(sp_measure_get_proc_data(proc->data1, proc->resource_flags, NULL),
			"proc /proc/<pid>/ data snapshot returned (%d).", rc |= __rc);
	proc->resource_flags &= (~rc);

	/* create process header */
	proc_data_format_title(proc, buffer, sizeof(buffer));
	proc->header = sp_report_header_add_child(&app_data->root_header, buffer, 30, SP_REPORT_ALIGN_LEFT, NULL, NULL);
	if (proc->header == NULL) return -ENOMEM;
	if (sp_report_header_add_child(proc->header, "clean:", 8, SP_REPORT_ALIGN_RIGHT, write_proc_mem_clean, (void*)proc) == NULL) return -ENOMEM;
	if (sp_report_header_add_child(proc->header, "dirty:", 8, SP_REPORT_ALIGN_RIGHT, write_proc_mem_dirty, (void*)proc) == NULL) return -ENOMEM;
	if (sp_report_header_add_child(proc->header, "change:", 8, SP_REPORT_ALIGN_RIGHT, write_proc_mem_change, (void*)proc) == NULL) return -ENOMEM;
	if (sp_report_header_add_child(proc->header, "CPU-%:", 7, SP_REPORT_ALIGN_RIGHT, write_proc_cpu_usage, (void*)proc) == NULL) return -ENOMEM;

	proc->has_data = true;

	return rc;
}

/**
 * Fress process data structure.
 *
 * This structure frees process data resources allocated by proc_data_create() function
 * together with the process data structure itself.
 *
 * @param proc[in]  the process data.
 * @return          0 for success.
 */
static int
proc_data_free(proc_data_t* proc)
{
	if (proc) {
		sp_measure_free_proc_data(&proc->data[0]);
		sp_measure_free_proc_data(&proc->data[1]);

		sp_report_header_remove(&proc->app_data->root_header, proc->header);
		sp_report_header_free(proc->header);

		free(proc);
	}
	return 0;
}


/**
 * Checks if the process command line has been changed.
 *
 * This function is used to determine if the process data should
 * be reinitialized.
 * @param[in] proc  the process data.
 * @return          0 if the process data should not be reinitialized.
 */
static int
proc_data_check_cmdline(proc_data_t* proc) {
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "/proc/%d/cmdline", proc->data1->common->pid);
	int len = 0;
	int fd = open(buffer, O_RDONLY);
	if (fd != -1) {
		len = read(fd, buffer, sizeof(buffer) - 1);
		close(fd);
	}
	buffer[len] = '\0';
	int rc = strcmp(proc->cmdline, buffer);
	if (rc) {
		strcpy(proc->cmdline, buffer);
	}
	return rc;
}

/**
 * Adds process to monitored process list.
 *
 * This function creates process data structure and adds it to the
 * application data process list.
 * @param self[in]   the application data.
 * @param pid[in]    the process identifier.
 * @return           0 for success.
 */
static int
app_data_add_proc(app_data_t* self, int pid)
{
	int rc;
	proc_data_t* proc;
	/* create process data structure */
	if ( (rc = proc_data_create(&proc, pid, self)) != 0) {
		proc_data_free(proc);
		return rc;
	}
	/* the first process to be monitored, set as the start of process list*/
	if (self->proc_list == NULL) {
		self->proc_list = proc;
	}
	else {
		/* add at the end of process list */
		proc_data_t* next = self->proc_list;
		while (next->next != NULL) {
			next = next->next;
		}
		next->next = proc;
	}
	self->proc_count++;
	/* set process column color if necessary */
	if (colors && (self->proc_count & 1)) {
		sp_report_header_set_color(proc->header, COLOR_PROCESS, COLOR_CLEAR);
	}
	return 0;
}

/**
 * Removes process from monitored process list.
 *
 * The process will be removed and all associated resources freed.
 * @param self[in]  the application data.
 * @param pid[in]   the process identifier.
 * @return          0 for success.
 */
static int
app_data_remove_proc(app_data_t* self, int pid)
{
	proc_data_t** pproc = &self->proc_list;
	while (*pproc && FIELD_PROC_PID(&(*pproc)->data[0]) != pid) {
		pproc = &(*pproc)->next;
	}
	if (*pproc) {
		proc_data_t* free_proc = *pproc;
		*pproc = (*pproc)->next;
		proc_data_free(free_proc);
		self->proc_count--;
	}
	/* reset color ordering which could get broken with column removal */
	if (colors) {
		int index = 1;
		proc_data_t* proc = self->proc_list;
		while (proc) {
			sp_report_header_set_color(proc->header, index ? COLOR_PROCESS : NULL, index ? COLOR_CLEAR : NULL);
			index ^= 1;
			proc = proc->next;
		}
	}
	return 0;
}

static int
app_data_set_sleep_interval(app_data_t* self, const char* interval)
{
	char buffer[256] = {0};
	int secs = 0, msecs = 0;
	int rc;
	/* scanf "%f" & float secs->msecs conversion has rounding issues,
	 * whereas handling the digits directly gives exact results.
	 */
	if ( (rc = sscanf(interval, "%d.%s", &secs, buffer)) < 1) {
		fprintf(stderr, "ERROR: invalid interval value: %s\n", interval);
		return -1;
	}
	if (rc > 1) {
		int power10[] = {0, 100, 10, 1};
		buffer[3] = '\0';
		int len = strlen(buffer);
		msecs = atoi(buffer) * power10[len];
		if (!secs && msecs < 10) {
			fprintf(stderr, "ERROR: Interval value 0.%03d is too small\n", msecs);
			return -1;
		}
	}
	self->sleep_interval = secs * 1000000 + msecs * 1000;
	ADD_OPTION_VALUE_FLAG(self->option_flags, OF_INTERVAL_OPTION_SET);
	return 0;
}

/**
 * Queues process name for monitoring.
 *
 * @param[in] name   name of the process to monitor.
 * @return           0 for success.
 */
static int
app_data_monitor_process_name(app_data_t* self, const char* name)
{
	if (!name) return -1;
	if (self->name_index == sizeof(self->name_list) / sizeof(self->name_list[0])) {
		return -1;
	}
	proc_name_t* proc = &self->name_list[self->name_index++];
	proc->name = strdup(name);
	proc->size = strlen(name);
	return proc->name != NULL ? 0 : -ENOMEM;
}


/**
 * Checks if the process is being monitored.
 *
 * @param[in]   the process name.
 * @return      true if the process is being monitored.
 */
static bool
app_data_is_process_monitored(app_data_t* self, const char* name)
{
	proc_name_t* proc = self->name_list;
	while (proc - self->name_list < self->name_index) {
		if (!strncmp(name, proc->name, proc->size)) {
			return true;
		}
		proc++;
	}
	return false;
}


/**
 * Scans running processes and updates monitored process list
 *
 * @param[in] self    the application data.
 * @return            -1 - failure, 0 - monitored process list was not changed,
 *                    1 - a process was added or removed to the list.
 */
static int
app_data_scan_processes(app_data_t* self)
{
	int rc = 0;
	if (self->name_index) {
		static time_t last_timestamp = 0;
		time_t current_timestamp = time(NULL);
		char buffer[512];

		/* first check for a new processes */
		DIR* procDir = opendir("/proc");
		if (procDir) {
			struct dirent* item;
			while ( (item = readdir(procDir)) ) {
				int pid = atoi(item->d_name);
				if (pid != 0) {
					struct stat fs;
					sprintf(buffer, "/proc/%s", item->d_name);
					if (stat(buffer, &fs) == 0) {
						if (last_timestamp < fs.st_mtime) {
							sp_measure_proc_data_t data;
							if (sp_measure_init_proc_data(&data, pid, 0, NULL) == 0 && data.common->name &&
									app_data_is_process_monitored(self, data.common->name)) {
								app_data_add_proc(self, pid);
								rc = 1;
							}
							sp_measure_free_proc_data(&data);
						}
					}
				}
			}
			closedir(procDir);
		}
		last_timestamp = current_timestamp;

		/* check for terminated processes */
		proc_data_t* proc = self->proc_list;
		while (proc) {
			proc_data_t* proc_free = NULL;
			int pid = proc->data[0].common->pid;
			sprintf(buffer, "/proc/%d", pid);
			if (access(buffer, F_OK) != 0) {
				proc_free = proc;
			}
			proc = proc->next;
			if (proc_free) {
				app_data_remove_proc(self, pid);
				rc = 1;
			}
		}
	}
	return rc;
}

/**
 * Resets color flags for already monitored processes.
 *
 * @param[in]   the application data.
 * @return
 */
static void
app_data_set_nocolor_flag(app_data_t* self)
{
	proc_data_t* proc = self->proc_list;
	while (proc) {
		sp_report_header_set_color(proc->header, NULL, NULL);
		proc = proc->next;
	}
}


/**
 * Execute the specified application and start monitoring it.
 */
static int
execute_application(app_data_t* self, const char* cmd)
{
	int pid = fork();
	if (pid == 0) {
		char* argv[100];
		char buffer[512];
		int argc = 0;
		const char* in = cmd;
		char* out = buffer;

		/* parse the command line */
		while (*in) {
			switch (*in) {
				case '"':
				case '\'': {
					char quote = *in++;
					while (*in && *in != quote) *out++ = *in++;
					break;
				}
				case ' ':
				case '\0': {
					if (out != buffer) {
						*out = '\0';
						argv[argc++] = strdup(buffer);
						out = buffer;
					}
					break;
				}
				default: {
					*out++ = *in;
					break;
				}
			}
			in++;
		}
		if (out != buffer) {
			*out = '\0';
			argv[argc++] = strdup(buffer);
		}
		argv[argc] = NULL;
		/* execute application */
		if (execvp(argv[0], argv) == -1) {
			return -1;
		}

		while (argc--) {
			free(argv[argc]);
		}
	}
	if (pid == -1) {
		return -1;
	}
	return app_data_add_proc(self, pid);
}

/**
 * Cleanup terminated child processes.
 */
static void process_closed(int sig __attribute__((unused)))
{
	int status;
	wait(&status);
}

/**
 * Parses application command line.
 */
static void
parse_cmdline(int argc, char** argv, app_data_t* self)
{
	int opt, rc;
	char* output_path = NULL;
	while ((opt = getopt_long(argc, argv, "p:hf:mcM:C:i:n:x:", long_opts, NULL)) != -1) {
		/* getopt allows -<char><arg> which gives confusing results
		 * when one writes --name foobar as -name.  Complain about it.
		 */
		if (optarg && argv[optind - 1] != optarg && *(argv[optind - 1] + 1) != '-') {
			fprintf(stderr, "ERROR: failed to parse argument %s!\n", argv[optind - 1]);
			exit(1);
		}
		switch (opt) {
		case 'f':
			output_path = strdup(optarg);
			break;
		case 'h':
			usage();
			exit(0);
		case 1001:
			colors = false;
			/* reset color flags for already monitored processes */
			app_data_set_nocolor_flag(self);
			break;
		case 1002:
			if ( (rc = app_data_add_proc(self, getpid())) != 0) {
				fprintf(stderr, "Error %d during process %d monitoring initialization!\n",
							rc, getpid());
				exit(-1);
			}
			break;
		case 'p':
			if ( (rc = app_data_add_proc(self, atoi(optarg))) != 0) {
				fprintf(stderr, "Error %d during process %d monitoring initialization!\n",
							rc, atoi(optarg));
				exit(-1);
			}
			break;
		case 'm':
			ADD_OPTION_VALUE_FLAG(self->option_flags, OF_PROC_MEM_CHANGES_ONLY);
			break;
		case 'c':
			ADD_OPTION_VALUE_FLAG(self->option_flags, OF_PROC_CPU_CHANGES_ONLY);
			break;
		case 'M':
			ADD_OPTION_VALUE_FLAG(self->option_flags, OF_SYS_MEM_CHANGES_ONLY);
			sys_mem_change_threshold = atoi(optarg);
			do_print_report_default = false;
			break;
		case 'C':
			ADD_OPTION_VALUE_FLAG(self->option_flags, OF_SYS_CPU_CHANGES_ONLY);
			if (!sscanf(optarg, "%f", &sys_cpu_change_threshold) ) {
				fprintf(stderr, "ERROR: invalid CPU change threshold for the system\n");
				exit(1);
			}
			do_print_report_default = false;
			break;
		case 'i':
			if (app_data_set_sleep_interval(self, optarg) != 0) {
				exit(1);
			}
			break;
		case 'n':
			if (app_data_monitor_process_name(self, optarg) != 0) {
				fprintf(stderr, "ERROR: failed to monitor process %s\n", optarg);
				exit(1);
			}
			break;
		case 'x':
			if (execute_application(self, optarg) != 0) {
				fprintf(stderr, "ERROR: failed to execute command %s\n", optarg);
				exit(1);
			}
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
	while (optind < argc) {
		if ( (rc = app_data_add_proc(self, atoi(argv[optind]))) != 0) {
			fprintf(stderr, "Error %d occurred during process %d monitoring initialization!\n",
						rc, atoi(argv[optind]));
			exit(-1);
		}
		++optind;
	}
	// determine if no printing needs to be done by default
	if (self->proc_list &&
		(IS_OPTION_VALUE_FLAG_SET(self->option_flags, OF_PROC_MEM_CHANGES_ONLY) ||
		 IS_OPTION_VALUE_FLAG_SET(self->option_flags, OF_PROC_CPU_CHANGES_ONLY) ) ) {

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

/**
 * Main function
 */
int main(int argc, char** argv)
{
	bool is_atty = false;
	int rows=0, lines_printed=0;
	app_data_t app_data;
	int rc = 0, value;
	sp_measure_proc_data_t* proc_data_swap;
	sp_measure_sys_data_t* sys_data_swap;
	proc_data_t* proc;
	bool do_print_header = true;
	bool do_print_report;
	struct timeval timestamp = {0, 0};

	if (app_data_init(&app_data) < 0) {
		fprintf(stderr, "ERROR: program initialization failed.\n");
		exit(-1);
	}

	struct sigaction sa = {.sa_flags = 0, .sa_handler = process_closed};
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		fprintf(stderr, "ERROR: Failed to install SIGCHILD handler\n");
		exit(-1);
	}

	parse_cmdline(argc, argv, &app_data);
	if (nice(-19) == -1) {
		perror("Warning: failed to change process priority.");
	}

	app_data_init_timestamps(&app_data);

	is_atty = isatty(fileno(output));
	if (!is_atty) colors = false;
	fprintf(output, "System total memory: %u kB RAM, %u kB swap\n",
			FIELD_SYS_MEM_TOTAL(app_data.sys_data1), FIELD_SYS_MEM_SWAP(app_data.sys_data1));

	// Disable header reprinting if we're printing to console, or if the
	// screen seems to be very small.
	if (is_atty) { rows = win_rows(); if (rows < 10 + app_data.proc_count) rows = 0; }
	// Install our signal handler, unless someone specifically wanted
	// SIGINT to be ignored.
	if (sigaction(SIGINT, NULL, &sa) == 0 && sa.sa_handler != SIG_IGN) {
		sa.sa_handler = quit_app;
		sigaction(SIGINT, &sa, NULL);
	}

	/* take initial process snapshots */
	proc = app_data.proc_list;
	while (proc) {
		CHECK_SNAPSHOT_RC(sp_measure_get_proc_data(proc->data1, proc->resource_flags, NULL),
				"Process (name=%s, pid=%d) resource usage snapshot returned (%d).",
				PROCESS_NAME(proc->data2), proc->data2->common->pid, rc = __rc);
		proc->resource_flags &= (~rc);
		proc = proc->next;
	}

	gettimeofday(&timestamp, NULL);

	while (!quit) {

		do_print_report = do_print_report_default;

		/* scan for processes to monitor */
		if (app_data_scan_processes(&app_data) == 1) {
			do_print_header = true;
		}

		/* take system snapshot */
		CHECK_SNAPSHOT_RC(sp_measure_get_sys_data(app_data.sys_data2, app_data.resource_flags, NULL),
				"System resource usage snapshot returned (%d).", rc = __rc);
		app_data.resource_flags &= (~rc);

		/* check if report should be printed */
		if (!do_print_report) {
			int _sys_ram_change;
			bool is_data_retrieved = true;
			if ( (rc = sp_measure_diff_sys_mem_used(app_data.sys_data1, app_data.sys_data2, &_sys_ram_change)) != 0) {
				fprintf(stderr, "Warning: failed to compare used system memory between two snapshots (%d).\n", rc);
				is_data_retrieved = false;
			}
			int value;
			if ( (rc = sp_measure_diff_sys_cpu_usage(app_data.sys_data1, app_data.sys_data2, &value)) != 0) {
				fprintf(stderr, "Warning: failed to compare cpu usage between two snapshots (%d).\n", rc);
				is_data_retrieved = false;
			}
			if (is_data_retrieved) {
				float _sys_cpu_usage_change = (float)value / 100;

				if ( (IS_OPTION_VALUE_FLAG_SET(app_data.option_flags, OF_SYS_MEM_CHANGES_ONLY) && abs(_sys_ram_change) >= sys_mem_change_threshold) ||
					(IS_OPTION_VALUE_FLAG_SET(app_data.option_flags, OF_SYS_CPU_CHANGES_ONLY) && fabs(_sys_cpu_usage_change) >= sys_cpu_change_threshold) ) {

					do_print_report = true;
				}
			}
		}

		/* take process snapshots */
		proc = app_data.proc_list;
		while (proc) {
			proc_data_t* proc_free = NULL;
			/* Check if process name was retrieved, try to retrieve it if necessary.
			 * This became necessary when --exec option was added. As the process data
			 * is read directly after it is forked, the target process might not be
			 * yet executed and the process name can't be retrieved.
			 */
			if (proc_data_check_cmdline(proc) != 0) {
				sp_measure_reinit_proc_data(proc->data1);
				if (FIELD_PROC_NAME(proc->data1)) {
					char buffer[256];
					proc_data_format_title(proc, buffer, sizeof(buffer));
					sp_report_header_set_title(proc->header, buffer, 30, SP_REPORT_ALIGN_LEFT);
					do_print_header = true;
				}
			}
  			/* take snapshot */
			if ( (rc = sp_measure_get_proc_data(proc->data2, proc->resource_flags, NULL)) >= 0) {
				/* check if the report should be printed */
				if (!do_print_report) {
					if (IS_OPTION_VALUE_FLAG_SET(app_data.option_flags, OF_PROC_MEM_CHANGES_ONLY)) {
						if ( (rc = sp_measure_diff_proc_mem_private_dirty(proc->data1, proc->data2, &value)) != 0) {
							fprintf(stderr, "ERROR: failed to compare process private dirty memory change between\n"
									"two snapshots (%d) for process(name=%s, pid=%d).\n",
									rc, PROCESS_NAME(proc->data2), proc->data2->common->pid);
							exit(-1);
						}
						if (value != 0) {
							do_print_report = true;
						}
					}
					if (IS_OPTION_VALUE_FLAG_SET(app_data.option_flags, OF_PROC_CPU_CHANGES_ONLY)) {
						if ( (rc = sp_measure_diff_proc_cpu_ticks(proc->data1, proc->data2, &value)) != 0) {
							fprintf(stderr, "ERROR: failed to compare process cpu usage between\n"
									"two snapshots (%d) for process(name=%s, pid=%d).\n",
									rc, PROCESS_NAME(proc->data2), proc->data2->common->pid);
							exit(-1);
						}
						if (value != 0) {
							do_print_report = true;
						}
					}
				}
				if (rc > 0) {
					proc->resource_flags &= (~rc);
					fprintf(stderr, "Warning: Process resource usage snapshot returned (%d). Some data might be absent.\n", rc);
				}
			}
			else {
				/* if snapshot retrieval failed, assume that the process has been terminated and stop monitoring it */
				proc_free = proc;
			}
			proc = proc->next;
			if (proc_free) {
				app_data_remove_proc(&app_data, FIELD_PROC_PID(proc_free->data2));
				do_print_header = true;
			}
		}

		/* reprint header if its the first time or next screen or a process was added/removed */
		if (do_print_header) {
			if ( (rc = sp_report_print_header(output, &app_data.root_header)) != 0) {
				fprintf(stderr, "ERROR: failed to print report header (%d).\n", rc);
				exit(-1);
			}
			do_print_header = false;
		}

		/* print data */
		if (do_print_report) {
			sp_report_print_data(output, &app_data.root_header);
			fflush(output);

			/* swap snapshot references so last snapshot is again in app_data.sys_data1 and
			 * the next snapshot will be stored into app_data.sys_data2 */
			sys_data_swap = app_data.sys_data1;
			app_data.sys_data1 = app_data.sys_data2;
			app_data.sys_data2 = sys_data_swap;
			/* do the same for project snapshots */
			for (proc = app_data.proc_list; proc; proc = proc->next) {
				proc_data_swap = proc->data1;
				proc->data1 = proc->data2;
				proc->data2 = proc_data_swap;
			}
		}

		if (quit) break;

		struct timeval tv;
		gettimeofday(&tv, NULL);
		int interval  = (tv.tv_sec - timestamp.tv_sec) * 1000000 + tv.tv_usec - timestamp.tv_usec;
		/* the sleep could be interrupted, force interval to 0 in that case */
		if (interval < 0) interval = 0;
		if (interval > (int)app_data.sleep_interval) {
			fprintf(stderr, "Warning, the specified update interval is too small, please increase it.\n");
			gettimeofday(&timestamp, NULL);
		}
		else {
			usleep(app_data.sleep_interval - interval);
			timestamp.tv_usec += app_data.sleep_interval;
			timestamp.tv_sec += timestamp.tv_usec / 1000000;
			timestamp.tv_usec %= 1000000;
		}

		/* reprint report header if necessary */
		if (do_print_report) {
			if (is_atty && rows) {
				if (++lines_printed >= rows-1) {
					do_print_header = true;
					lines_printed = 3;
				}
			}
		}
	}

	while (app_data.proc_list) {
		app_data_remove_proc(&app_data, FIELD_PROC_PID(&app_data.proc_list->data[0]));
	}
	app_data_release(&app_data);
}

/* ========================================================================= *
 *            No more code in mem-cpu-monitor.c                              *
 * ========================================================================= */
