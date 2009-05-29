/* ========================================================================= *
 * File: _____________________, part of sp-memusage
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
 * 1st April 2009 - Arun Srinivasan:
 * Description and History will be added shortly
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

#include "mem-cpu-monitor.h"

/* ========================================================================= *
 * Definitions.
 * ========================================================================= */

/* Compile-time array capacity calculation */
#define CAPACITY(a)  (sizeof(a) / sizeof(*a))

/* Correct division of 2 unsigned values */
#define DIVIDE(a,b)  (((a) + ((b) >> 1)) / (b))

#define FALSE 0
#define TRUE 1

typedef struct
{
	const char* name;    /* /proc/meminfo parameter with ":" */
	unsigned    data;    /* loaded value                     */
} MEMINFO;

typedef struct
{
	const char* name;    /* /proc/pid/smaps parameter with ":" */
	unsigned    data;    /* loaded value                     */
} PMEMINFO;

typedef struct
{
	const char* name;    /* /proc/stat parameter with "cpu" */
	char* data;    /* loaded value                     */
} CPUINFO;

typedef struct
{
	char* pcvalue;
} PCPUINFO;

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
	FILE*    meminfo = fopen(path, "r");

	if ( meminfo )
	{
		char line[256];

		/* Load all lines in file */
		while ( fgets(line, CAPACITY(line), meminfo) )
		{
			unsigned idx;

			/* Search and setup parameter */
			for (idx = 0; idx < size; idx++)
			{
				if ( line == strstr(line, vals[idx].name) )
				{
					//printf("Line read is %s\n", line);
					/* Parameter has a format SomeName:\tValue, we expect that MEMINFO::name contains ":" */
					vals[idx].data = (unsigned)strtoul(line + strlen(vals[idx].name) + 1, NULL, 0);
					counter++;
					break;
				}
			}
		} /* while have data */

		fclose(meminfo);
	}

	return 0;
} /* mu_load */

/* ------------------------------------------------------------------------- *
 * mu_pload -- opens /proc/pid/smaps file and load values.
 * parameters:
 *    path - path to file to handle.
 *    vals - array of values to be handled.
 *    size - size of vals array.
 * returns: '0' on sucessfully loading values and -1 on failure.
 * ------------------------------------------------------------------------- */
static unsigned mu_pload(const char* path, PMEMINFO* pvals, PMEMUSAGE* pusage)
{
	FILE* pmeminfo = fopen(path, "r");

	if ( pmeminfo )
	{
		char line[256];
		/* Load all lines in file */
		while ( fgets(line, CAPACITY(line),pmeminfo) )
		{
			/* Search and setup parameter */
			if ( line == strstr(line, pvals[0].name) )
			{
				/* Parameter has a format SomeName:\tValue */
				pvals[0].data += (unsigned)strtoul(line + strlen(pvals[0].name) + 1, NULL, 0);
			}
			else if ( line == strstr(line, pvals[1].name) )
			{
				/* Parameter has a format SomeName:\tValue */
				pvals[1].data += (unsigned)strtoul(line + strlen(pvals[1].name) + 1, NULL, 0);
			}
			else if ( line == strstr(line, pvals[2].name) )
			{
				/* Parameter has a format SomeName:\tValue */
				pvals[2].data += (unsigned)strtoul(line + strlen(pvals[2].name) + 1, NULL, 0);
			}
			else if ( line == strstr(line, pvals[3].name) )
			{
				/* Parameter has a format SomeName:\tValue */
				pvals[3].data += (unsigned)strtoul(line + strlen(pvals[3].name) + 1, NULL, 0);
			}
		} /* while have data */

		fclose(pmeminfo);
		return 0;
	}
	else
	{
		/*Error opening the /proc/pid/file*/
		return -1;
	}
}

static unsigned cpu_load(const char* path, CPUINFO* cpuvals, CPUUSAGE* cusage)
{
	FILE* cpuinfo = fopen(path, "r");
	const char* str_token = NULL;
	int count = 0;
	char** end = NULL;

	if ( cpuinfo )
	{
		char line[256];

		fgets(line, CAPACITY(line),cpuinfo);

		if ( line == strstr(line, cpuvals[0].name) )
		{
			cpuvals[0].data = line;
			str_token = (const char*)strtok(cpuvals[0].data," ");
		}

		while(str_token != NULL)
		{
			if(count == 1)
			{
				cusage->curr_user = (unsigned)strtol(str_token, end, 10);
			}
			else if(count == 2)
			{
				cusage->curr_nice = (unsigned)strtol(str_token, end, 10);
			}
			else if(count == 3)
			{
				cusage->curr_system = (unsigned)strtol(str_token, end, 10);
			}
			else if(count == 4)
			{
				cusage->curr_idle = (unsigned)strtol(str_token, end, 10);
			}
			count++;
			str_token = strtok (NULL, " ");
		}

		fclose(cpuinfo);
		return 0;
	}
	else
	{
		/*Error opening the /proc/stat file*/
		return -1;
	}
}

static unsigned pcpu_load(const char* ppath, PCPUINFO* pcpuvals, PCPUUSAGE* pcusage)
{
	FILE* pcpuinfo = fopen(ppath, "r");
	const char* str_token;
	int count = 0;
	char** end = NULL;

	if ( pcpuinfo )
	{
		char line[256];

		fgets(line, CAPACITY(line), pcpuinfo);
		pcpuvals[0].pcvalue = line;
		str_token = (const char*)strtok(pcpuvals[0].pcvalue," ");

		while(str_token != NULL)
		{
			if(count == 13)
			{
				pcusage->curr_utime = (unsigned)strtol(str_token, end, 10);
			}
			else if(count == 14)
			{
				pcusage->curr_stime = (unsigned)strtol(str_token, end, 10);
			}
			count++;
			str_token = strtok (NULL, " ");
		}

		fclose(pcpuinfo);
		return 0;
	}
	else
	{
		/*Error opening the /proc/[PID]/stat file*/
		return -1;
	}
}

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
		if ( !mu_load("/proc/meminfo", vals, CAPACITY(vals)) )
		{
			/* Discover memory information using loaded numbers */
			usage->total			= vals[0].data;
			usage->swap			= vals[1].data;
			usage->free			= vals[2].data + vals[3].data + vals[4].data;
			usage->used			= usage->total - usage->free;
			usage->mem_change		= usage->used - usage->prev_used;
			usage->prev_used		= usage->used;

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

/* ------------------------------------------------------------------------- *
 * pmemusage -- call mu_pload
 * parameters:
 *    usage - parameters to be updated.
 * returns:
 *    0 if values loaded successfuly OR negative error code.
 * ------------------------------------------------------------------------- */
int pmemusage(PMEMUSAGE* pusage, char* path)
{
	if( pusage )
	{
		PMEMINFO pvals[] =
		{
			{ "Size:",			0 },
			{ "Private_Clean:",		0 },
			{ "Private_Dirty:",		0 },
			{ "Swap:",			0 }
		};

		/* Load values from the /proc/PID/smaps file */
		if (!mu_pload(path, pvals, pusage))
		{
			/*Populate the final data into the pusage structure*/
			pusage->size			= pvals[0].data;
			pusage->pclean			= pvals[1].data;
			pusage->pdirty			= pvals[2].data + pvals[3].data;
			pusage->dirty_change		= pusage->pdirty - pusage->prev_dirty;
			pusage->prev_dirty		= pusage->pdirty;
			return 0;
		}
		else
		{
			/* Clean-up values */
			memset(pusage, 0, sizeof(pusage));
		}
	}
	return 0;
}

int cpuusage(CPUUSAGE* cusage, const char* path)
{
	if( cusage )
	{
		CPUINFO cpuvals[] =
		{
			{"cpu ", "no data"}
		};

		/* Load values from the /proc/stat file */
		if (!cpu_load(path, cpuvals, cusage))
		{
			cusage->curr_total		=	(cusage->curr_user + cusage->curr_nice + cusage->curr_system + cusage->curr_idle);
			cusage->total			=	(cusage->curr_total - cusage->prev_total);
			cusage->user			=	(cusage->curr_user - cusage->prev_user);
			cusage->nice			=	(cusage->curr_nice - cusage->prev_nice);
			cusage->system			=	(cusage->curr_system - cusage->prev_system);
			cusage->idle			=	(cusage->curr_idle - cusage->prev_idle);

			cusage->idle_perc		=	((100 * cusage->idle) / cusage->total);
			cusage->user_perc		=	((100* cusage->user) / cusage->total);
			cusage->total_perc		=	(100 - cusage->idle_perc );

			cusage->prev_total		=	cusage->curr_total;
			cusage->prev_user		=	cusage->curr_user;
			cusage->prev_nice		=	cusage->curr_nice;
			cusage->prev_system		=	cusage->curr_system;
			cusage->prev_idle		=	cusage->curr_idle;

			cusage->curr_total		=	0;
			cusage->curr_user		=	0;
			cusage->curr_nice		=	0;
			cusage->curr_system		=	0;
			cusage->curr_idle		=	0;
			return 0;
		}
		else
		{
			/* Clean-up values */
			memset(cusage, 0, sizeof(cusage));
		}
	}
	return 0;
}

int pcpuusage(PCPUUSAGE* pcusage, const char* ppath)
{
	if( pcusage )
	{
		PCPUINFO pcpuvals[] =
		{
			{"data"}
		};

		/* Load values from the /proc/[PID]stat file */
		if (!pcpu_load(ppath, pcpuvals, pcusage))
		{
			pcusage->diff_utime	= (pcusage->curr_utime - pcusage->prev_utime);
			pcusage->diff_stime	= (pcusage->curr_stime - pcusage->prev_stime);
			pcusage->cpu_process	= ((pcusage->diff_utime + pcusage->diff_stime) / pcusage->delay) ;
			pcusage->prev_utime	= pcusage->curr_utime;
			pcusage->prev_stime	= pcusage->curr_stime;
			pcusage->curr_utime	= 0;
			pcusage->curr_stime	= 0;
		}
		else
		{
			/* Clean-up values */
			memset(pcusage, 0, sizeof(pcusage));
		}
	}
	return 0;
}

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

int printheader(INITPARAM *initparam, MEMUSAGE* usage)
{
	FILE* soft_ver			= fopen("/etc/osso_software_version", "r");
	const char *version	= NULL;
	char line_ver[128];

	if(soft_ver)
	{
		fgets(line_ver, CAPACITY(line_ver), soft_ver);
		version = (const char*)strtok(line_ver,"\n");
		fclose(soft_ver);
	}
	else
	{
		version = "Cant open /etc/osso_software_version";
	}

	printf("|---------------------------------------------------------|\n");
	printf("|\t\t\tSYSTEM INFO\t\t\t  |\n");
	printf("|---------------------------------------------------------|\n");
	printf("|Total RAM Memory  : %u-KB\t\t\t\t  |\n", usage->total);
	printf("|Total SWAP Memory : %u-KB\t\t\t\t  |\n", usage->swap);
	printf("|Software Version  : %s\t\t\t\n", version);
	printf("|---------------------------------------------------------|\n");
	printf("|\t\t\t|SYSTEM MEMORY  | SYSTEM \t  |\n");
	printf("----------------------------------------------------------|\n");
	printf("Time:\t\tMS:\t Used:\tChange:\t  CPU-%%: \t  |\n");
	printf("----------------------------------------------------------|\n");

	initparam->header_flag = FALSE;
	return 0;
}

int printpheader(INITPARAM *initparam, MEMUSAGE* usage)
{
	FILE* soft_ver	= fopen("/etc/osso_software_version", "r");
	FILE* ppid_name	= fopen(initparam->cppath, "rt");

	const char *version	= NULL;
	const char* pname 	= NULL;
	char line_ver[128];
	char line_name[128];

	if(soft_ver)
	{
		fgets(line_ver, CAPACITY(line_ver), soft_ver);
		version = (const char*)strtok(line_ver,"\n");
		fclose(soft_ver);
	}
	else
	{
		version = "Cant open /etc/osso_software_version";
	}

	if(ppid_name)
	{
		fgets(line_name, CAPACITY(line_name), ppid_name);
		pname = (const char*)strtok(line_name,"(");
		pname = strtok (NULL, ") ");
		fclose(ppid_name);
	}
	else
	{
		pname = "Cant get process name";
	}

	if(!initparam->output)
	{
		printf("|-----------------------------------------------------------------------------------------------|\n");
		printf("|\t\t\t\tSYSTEM INFO\t\t\t\t\t\t\t|\n");
		printf("|-----------------------------------------------------------------------------------------------|\n");
		printf("|Total RAM Memory\t: %u-KB\t\t\t\t\t\t\t\t|\n", usage->total);
		printf("|Total SWAP Memory\t: %u-KB\t\t\t\t\t\t\t\t|\n", usage->swap);
		printf("|Software Verison\t: %s\t\t\t\n", version);
		printf("|Process Name\t\t: %s\t\t\t\n", pname);
		printf("|Process PID\t\t: %d\t\t\t\n", initparam->ppid);
		printf("|-----------------------------------------------------------------------------------------------|\n");
		printf("|\t\t\t|SYSTEM MEMORY\t|\tPROCESS MEMORY\t|\tCPU USAGE\t\t|\n");
		printf("------------------------------------------------------------------------------------------------|\n");
		printf("Time:\t\tMS:\t Used:\tChange:\t Size:\tDirty:\tChange:\tUser-%%:\tProcess-%%:\tTotal-%%:|\n");
		printf("------------------------------------------------------------------------------------------------|\n");
	}

	if(initparam->output)
	{
		initparam->output_file = fopen(initparam->fname,"a+"); /* open for writing */
		fprintf(initparam->output_file ,"|-----------------------------------------------------------------------------------------------|\n");
		fprintf(initparam->output_file ,"|\t\t\t\tSYSTEM INFO\t\t\t\t\t\t\t|\n");
		fprintf(initparam->output_file ,"|-----------------------------------------------------------------------------------------------|\n");
		fprintf(initparam->output_file ,"|Total RAM Memory\t: %u-KB\t\t\t\t\t\t\t\t|\n", usage->total);
		fprintf(initparam->output_file ,"|Total SWAP Memory\t: %u-KB\t\t\t\t\t\t\t\t|\n", usage->swap);
		fprintf(initparam->output_file ,"|Software Verison\t: %s\t\t\t\n", version);
		fprintf(initparam->output_file ,"|Process Name\t\t: %s\t\t\t\n", pname);
		fprintf(initparam->output_file ,"|Process PID\t\t: %d\t\t\t\n", initparam->ppid);
		fprintf(initparam->output_file ,"|-----------------------------------------------------------------------------------------------|\n");
		fprintf(initparam->output_file ,"|\t\t\t|SYSTEM MEMORY\t|\tPROCESS MEMORY\t|\tCPU USAGE\t\t|\n");
		fprintf(initparam->output_file ,"------------------------------------------------------------------------------------------------|\n");
		fprintf(initparam->output_file ,"Time:\t\tMS:\t Used:\tChange:\t Size:\tDirty:\tChange:\tUser-%%:\tProcess-%%:\tTotal-%%:|\n");
		fprintf(initparam->output_file ,"------------------------------------------------------------------------------------------------|\n");
		fclose(initparam->output_file ); /* close the file before ending program */
	}

	initparam->header_flag = FALSE;
	return 0;
}

int main(const int argc, const char* argv[])
{
	static CPUUSAGE cusage;
	static MEMUSAGE usage;
	static PMEMUSAGE pusage;
	static PCPUUSAGE pcusage;
	static INITPARAM initparam;
	initparam.output		= FALSE;
	initparam.period		= 3;
	initparam.ppid			= 0;
	initparam.pmonitor		= FALSE;
	initparam.mppath		= NULL;
	initparam.cpath			= "/proc/stat";
	initparam.cppath		= NULL;
	initparam.header_flag		= TRUE;
	initparam.output_file		= NULL;
	usage.prev_used			= 0;
	pusage.prev_dirty		= 0;

	if ((argc > 4 ) || (argc == 2 && !isdigit(argv[1][0])) || (argc == 3 && !isdigit(argv[2][0])))
	{
		fprintf(stderr,
				"\nUSAGE:\t%s [output interval in secs] - OUTPUT IN TERMINAL\n\n"
				"\t%s [output interval in secs] [pid of the process] - OUTPUT IN TERMINAL\n\n"
				"\t%s [output interval in secs] [pid of process] [file name] - REDIRECTION TO FILE\n\n",
				*argv, *argv, *argv);
		exit(1);
	}
	else if (argc==2)
	{
		initparam.period		= strtoul(argv[1], NULL, 0);
		initparam.pmonitor		= FALSE;
	}
	else if (argc == 3 || argc == 4)
	{
		initparam.period	=	strtoul(argv[1], NULL, 0);
		initparam.ppid		=	strtoul(argv[2], NULL, 0);
		pcusage.delay		=	initparam.period;
		initparam.pmonitor	=	TRUE;

		const char* path1 = "/proc/";
		const char* path2 = argv[2];
		const char* path3 = "/smaps";
		const char* path4 = "/stat";

		initparam.mppath = (char*)calloc(strlen(path1) + strlen(path2) + strlen(path3) + 1, sizeof(char));
		strcat(initparam.mppath, path1);
		strcat(initparam.mppath, path2);
		strcat(initparam.mppath, path3);

		initparam.cppath = (char*)calloc(strlen(path1) + strlen(path2) + strlen(path4) + 1, sizeof(char));
		strcat(initparam.cppath, path1);
		strcat(initparam.cppath, path2);
		strcat(initparam.cppath, path4);

		if(argc == 4)
		{
			initparam.fname		=	argv[3];
			initparam.output	=	TRUE;
		}
	}

	/* We must print data always */
	nice(-59);

	if(!initparam.pmonitor)
	{
		while (1)
		{
			/* Load all values from meminfo file */

			if(0 == (memusage(&usage) || cpuusage(&cusage, initparam.cpath)))
			{
				const time_t tv = time(NULL);
				struct tm*   ts = gmtime(&tv);
				const char*  bg = (mu_check_flag("/sys/kernel/low_watermark") ? "B" : "");
				const char*  lm = (mu_check_flag("/sys/kernel/high_watermark") ? ",L" : "");

				if(initparam.header_flag)
				{
					if(!printheader(&initparam, &usage))
					{
						usage.mem_change = 0;
						cusage.total_perc = 0;
					}
				}

				printf("%02u:%02u:%02u\t%s%s\t%zd\t%6zd\t%6.2f\t\n",
						ts->tm_hour, ts->tm_min, ts->tm_sec, bg, lm,
						usage.used, usage.mem_change,
						cusage.total_perc);
			}
			else
			{
				printf ("unable to load values from /proc/meminfo file\n");
				return -1;
			}
			sleep(initparam.period);
		}
	}
	else if (initparam.pmonitor)
	{

		while (1)
		{
			/* Load all values from meminfo file */

			if (0 == (memusage(&usage) || cpuusage(&cusage, initparam.cpath)
						|| pmemusage(&pusage, initparam.mppath) || pcpuusage(&pcusage, initparam.cppath)))
			{
				const time_t tv = time(NULL);
				struct tm*   ts = gmtime(&tv);
				const char*  bg = (mu_check_flag("/sys/kernel/low_watermark") ? "BgKill" : "");
				const char*  lm = (mu_check_flag("/sys/kernel/high_watermark") ? ",LowMem" : "");

				if(initparam.header_flag)
				{
					if(!printpheader(&initparam, &usage))
					{
						usage.mem_change	= 0;
						cusage.total_perc	= 0;
						pusage.dirty_change = 0;
						pcusage.cpu_process = 0;
					}
				}

				if(!initparam.output)
				{
					printf("%02u:%02u:%02u\t%s%s\t%u\t%6zd\t%u\t%5u\t%6zd\t%6.2f\t%6.2f\t%14.2f\t\n",
							ts->tm_hour, ts->tm_min, ts->tm_sec, bg, lm,
							usage.used, usage.mem_change, pusage.size, pusage.pdirty, pusage.dirty_change,
							cusage.user_perc, pcusage.cpu_process, cusage.total_perc
					      );
				}
				if(initparam.output)
				{
					initparam.output_file = fopen(argv[3],"a+"); /* open for writing */
					fprintf(initparam.output_file ,"%02u:%02u:%02u\t%s%s\t%u\t%6zd\t%u\t%5u\t%6zd\t%6.2f\t%6.2f\t%14.2f\t\n",
							ts->tm_hour, ts->tm_min, ts->tm_sec, bg, lm,
							usage.used, usage.mem_change, pusage.size, pusage.pdirty, pusage.dirty_change,
							cusage.user_perc, pcusage.cpu_process, cusage.total_perc);
					fclose(initparam.output_file ); /* close the file before ending program */
				}
			}
			else
			{
				printf ("unable to load values from /proc/meminfo file\n");
				return -1;
			}
			sleep(initparam.period);
		}
	}
	return 0;
} /* main ends*/

/* ========================================================================= *
 *            No more code in file process-mem-monitor.c                     *
 * ========================================================================= */























