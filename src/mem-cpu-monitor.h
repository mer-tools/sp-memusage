#ifndef MEM_MONITOR_H_USED
#define MEM_MONITOR_H_USED

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
typedef struct
{
	/* Update interval once per 3 seconds by default */
	unsigned	period;
	unsigned	ppid;
	unsigned	pmonitor;
	char*		mppath;
	const char*	cpath;
	char*		cppath;
	unsigned	header_flag;
	unsigned	output;
	FILE*		output_file;
	const char*	fname;
} INITPARAM;
/* Structure that used to report about memory consumption */
typedef struct
{
	size_t		total;     /* Total amount of memory in system: RAM */
	size_t		free;      /* Free memory in system, bytes                 */
	size_t		used;      /* Used memory in system, bytes                 */
	size_t		swap;   /* Total swap memory */
	size_t		prev_used;
	size_t		mem_change;
	unsigned	header_flag;
} MEMUSAGE;

/* Structure that used to report about process memory consumption */
typedef struct
{
	size_t	size;			/* Process size memory, kilo-bytes					*/
	size_t	pclean;		/* Private clean memory of process, kilo-bytes		*/
	size_t	pdirty;		/* Private dirty memory of the process, kilo-bytes	*/
	size_t	swap;			/* Referenced memory of the process, kilo-bytes		*/
	size_t	prev_dirty;
	size_t	dirty_change;
} PMEMUSAGE;

typedef struct
{
	size_t prev_total;     /*  */
	size_t prev_user;      /*  */
	size_t prev_nice;      /*  */
	size_t prev_system;      /*  */
	size_t prev_idle;      /*  */

	size_t curr_total;     /*  */
	size_t curr_user;      /*  */
	size_t curr_nice;      /*  */
	size_t curr_system;      /*  */
	size_t curr_idle;      /*  */

	float total;     /*  */
	float user;      /*  */
	float nice;      /*  */
	float system;      /*  */
	float idle;      /*  */

	float user_perc;
	float total_perc;
	float idle_perc;
} CPUUSAGE;

typedef struct
{
	unsigned	prev_utime;     /*  */
	unsigned	prev_stime;      /*  */
	unsigned	curr_utime;     /*  */
	unsigned	curr_stime;      /*  */
	float		diff_utime;
	float		diff_stime;
	float		cpu_process;
	float		delay;
} PCPUUSAGE;
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
int printheader (INITPARAM* initpara, MEMUSAGE* usagem);
int printpheader(INITPARAM* initparam, MEMUSAGE* usage);
int memusage(MEMUSAGE* usage);
int pmemusage(PMEMUSAGE* pusage, char* path);
int cpuusage(CPUUSAGE* cusage, const char* path);
int pcpuusage(PCPUUSAGE* pusage, const char* ppath);

#ifdef __cplusplus
}
#endif

#endif /* MEM_MONITOR_H_USED */
