/*
htop - linux/Platform.c
(C) 2014 Hisham H. Muhammad
(C) 2020 Red Hat, Inc.  All Rights Reserved.
Released under the GNU GPLv2, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h"

#include "Platform.h"

#include <math.h>

#include "BatteryMeter.h"
#include "ClockMeter.h"
#include "Compat.h"
#include "CPUMeter.h"
#include "DateMeter.h"
#include "DateTimeMeter.h"
#include "DiskIOMeter.h"
#include "HostnameMeter.h"
#include "LoadAverageMeter.h"
#include "Macros.h"
#include "MainPanel.h"
#include "MemoryMeter.h"
#include "Meter.h"
#include "NetworkIOMeter.h"
#include "Object.h"
#include "Panel.h"
#include "PCPProcess.h"
#include "PCPProcessList.h"
#include "ProcessList.h"
#include "ProvideCurses.h"
#include "Settings.h"
#include "SwapMeter.h"
#include "TasksMeter.h"
#include "UptimeMeter.h"
#include "XUtils.h"

#include "linux/PressureStallMeter.h"
#include "linux/ZramMeter.h"
#include "linux/ZramStats.h"

typedef struct Platform_ {
   int context;			/* PMAPI(3) context identifier */
   unsigned int total;		/* total number of all metrics */
   const char** names;		/* name array indexed by Metric */
   pmID* pmids;			/* all known metric identifiers */
   pmID* fetch;			/* enabled identifiers for sampling */
   pmDesc* descs;		/* metric desc array indexed by Metric */
   pmResult* result;		/* sample values result indexed by Metric */
} Platform;

Platform* pcp;

ProcessField Platform_defaultFields[] = { PID, USER, PRIORITY, NICE, M_VIRT, M_RESIDENT, (int)M_SHARE, STATE, PERCENT_CPU, PERCENT_MEM, TIME, COMM, 0 };

int Platform_numberOfFields = LAST_PROCESSFIELD;

const SignalItem Platform_signals[] = {
   { .name = " 0 Cancel",    .number = 0 },
};

const unsigned int Platform_numberOfSignals = ARRAYSIZE(Platform_signals);

const MeterClass* const Platform_meterTypes[] = {
   &CPUMeter_class,
   &ClockMeter_class,
   &DateMeter_class,
   &DateTimeMeter_class,
   &LoadAverageMeter_class,
   &LoadMeter_class,
   &MemoryMeter_class,
   &SwapMeter_class,
   &TasksMeter_class,
   &UptimeMeter_class,
   &BatteryMeter_class,
   &HostnameMeter_class,
   &AllCPUsMeter_class,
   &AllCPUs2Meter_class,
   &AllCPUs4Meter_class,
   &AllCPUs8Meter_class,
   &LeftCPUsMeter_class,
   &RightCPUsMeter_class,
   &LeftCPUs2Meter_class,
   &RightCPUs2Meter_class,
   &LeftCPUs4Meter_class,
   &RightCPUs4Meter_class,
   &LeftCPUs8Meter_class,
   &RightCPUs8Meter_class,
   &BlankMeter_class,
   &PressureStallCPUSomeMeter_class,
   &PressureStallIOSomeMeter_class,
   &PressureStallIOFullMeter_class,
   &PressureStallMemorySomeMeter_class,
   &PressureStallMemoryFullMeter_class,
   &ZramMeter_class,
   &DiskIOMeter_class,
   &NetworkIOMeter_class,
   NULL
};

static const char *Platform_metricNames[] = {
   [PCP_CONTROL_THREADS] = "proc.control.perclient.threads",

   [PCP_HINV_NCPU] = "hinv.ncpu",
   [PCP_HINV_CPUCLOCK] = "hinv.cpu.clock",
   [PCP_LOAD_AVERAGE] = "kernel.all.load",
   [PCP_PID_MAX] = "kernel.all.pid_max",
   [PCP_UPTIME] = "kernel.all.uptime",
   [PCP_BOOTTIME] = "kernel.all.boottime",
   [PCP_CPU_USER] = "kernel.all.cpu.user",
   [PCP_CPU_NICE] = "kernel.all.cpu.nice",
   [PCP_CPU_SYSTEM] = "kernel.all.cpu.sys",
   [PCP_CPU_IDLE] = "kernel.all.cpu.idle",
   [PCP_CPU_IOWAIT] = "kernel.all.cpu.wait.total",
   [PCP_CPU_IRQ] = "kernel.all.cpu.intr",
   [PCP_CPU_SOFTIRQ] = "kernel.all.cpu.irq.soft",
   [PCP_CPU_STEAL] = "kernel.all.cpu.steal",
   [PCP_CPU_GUEST] = "kernel.all.cpu.guest",
   [PCP_CPU_GUESTNICE] = "kernel.all.cpu.guest_nice",
   [PCP_PERCPU_USER] = "kernel.percpu.cpu.user",
   [PCP_PERCPU_NICE] = "kernel.percpu.cpu.nice",
   [PCP_PERCPU_SYSTEM] = "kernel.percpu.cpu.sys",
   [PCP_PERCPU_IDLE] = "kernel.percpu.cpu.idle",
   [PCP_PERCPU_IOWAIT] = "kernel.percpu.cpu.wait.total",
   [PCP_PERCPU_IRQ] = "kernel.percpu.cpu.intr",
   [PCP_PERCPU_SOFTIRQ] = "kernel.percpu.cpu.irq.soft",
   [PCP_PERCPU_STEAL] = "kernel.percpu.cpu.steal",
   [PCP_PERCPU_GUEST] = "kernel.percpu.cpu.guest",
   [PCP_PERCPU_GUESTNICE] = "kernel.percpu.cpu.guest_nice",
   [PCP_MEM_TOTAL] = "mem.physmem",
   [PCP_MEM_USED] = "mem.util.used",
   [PCP_MEM_BUFFERS] = "mem.util.bufmem",
   [PCP_MEM_CACHED] = "mem.util.cached",
   [PCP_MEM_SHARED] = "mem.util.shmem",
   [PCP_MEM_SRECLAIM] = "mem.util.slabReclaimable",
   [PCP_SWAP_TOTAL] = "swap.length",
   [PCP_SWAP_USED] = "swap.used",
   [PCP_DISK_READB] = "disk.all.read_bytes",
   [PCP_DISK_WRITEB] = "disk.all.write_bytes",
   [PCP_DISK_ACTIVE] = "disk.all.avactive",
   [PCP_NET_RECVB] = "network.all.in.bytes",
   [PCP_NET_SENDB] = "network.all.out.bytes",
   [PCP_NET_RECVP] = "network.all.in.packets",
   [PCP_NET_SENDP] = "network.all.out.packets",

   [PCP_PSI_CPUSOME] = "kernel.all.pressure.cpu.some.avg",
   [PCP_PSI_IOSOME] = "kernel.all.pressure.io.some.avg",
   [PCP_PSI_IOFULL] = "kernel.all.pressure.io.full.avg",
   [PCP_PSI_MEMSOME] = "kernel.all.pressure.memory.some.avg",
   [PCP_PSI_MEMFULL] = "kernel.all.pressure.memory.full.avg",

   [PCP_ZRAM_CAPACITY] = "zram.capacity",
   [PCP_ZRAM_ORIGINAL] = "zram.mm_stat.data_size.original",
   [PCP_ZRAM_COMPRESSED] = "zram.mm_stat.data_size.compressed",

   [PCP_PROC_PID] = "proc.psinfo.pid",
   [PCP_PROC_PPID] = "proc.psinfo.ppid",
   [PCP_PROC_TGID] = "proc.psinfo.tgid",
   [PCP_PROC_PGRP] = "proc.psinfo.pgrp",
   [PCP_PROC_SESSION] = "proc.psinfo.session",
   [PCP_PROC_STATE] = "proc.psinfo.sname",
   [PCP_PROC_TTY] = "proc.psinfo.tty",
   [PCP_PROC_TTYPGRP] = "proc.psinfo.tty_pgrp",
   [PCP_PROC_FLAGS] = "proc.psinfo.flags",
   [PCP_PROC_MINFLT] = "proc.psinfo.minflt",
   [PCP_PROC_MAJFLT] = "proc.psinfo.maj_flt",
   [PCP_PROC_CMINFLT] = "proc.psinfo.cmin_flt",
   [PCP_PROC_CMAJFLT] = "proc.psinfo.cmaj_flt",
   [PCP_PROC_UTIME] = "proc.psinfo.utime",
   [PCP_PROC_STIME] = "proc.psinfo.stime",
   [PCP_PROC_CUTIME] = "proc.psinfo.cutime",
   [PCP_PROC_CSTIME] = "proc.psinfo.cstime",
   [PCP_PROC_PRIORITY] = "proc.psinfo.priority",
   [PCP_PROC_NICE] = "proc.psinfo.nice",
   [PCP_PROC_THREADS] = "proc.psinfo.threads",
   [PCP_PROC_STARTTIME] = "proc.psinfo.start_time",
   [PCP_PROC_EXITSIGNAL] = "proc.psinfo.exit_signal",
   [PCP_PROC_PROCESSOR] = "proc.psinfo.processor",
   [PCP_PROC_CMD] = "proc.psinfo.cmd",
   [PCP_PROC_PSARGS] = "proc.psinfo.psargs",
   [PCP_PROC_CGROUPS] = "proc.psinfo.cgroups",
   [PCP_PROC_OOMSCORE] = "proc.psinfo.oom_score",
   [PCP_PROC_VCTXSW] = "proc.psinfo.vctxsw",
   [PCP_PROC_NVCTXSW] = "proc.psinfo.nvctxsw",
   [PCP_PROC_LABELS] = "proc.psinfo.labels",
   [PCP_PROC_ENVIRON] = "proc.psinfo.environ",
   [PCP_PROC_TTYNAME] = "proc.psinfo.ttyname",
   [PCP_PROC_ID_UID] = "proc.id.uid",
   [PCP_PROC_ID_USER] = "proc.id.uid_nm",
   [PCP_PROC_IO_RCHAR] = "proc.io.rchar",
   [PCP_PROC_IO_WCHAR] = "proc.io.wchar",
   [PCP_PROC_IO_SYSCR] = "proc.io.syscr",
   [PCP_PROC_IO_SYSCW] = "proc.io.syscw",
   [PCP_PROC_IO_READB] = "proc.io.read_bytes",
   [PCP_PROC_IO_WRITEB] = "proc.io.write_bytes",
   [PCP_PROC_IO_CANCELLED] = "proc.io.cancelled_write_bytes",
   [PCP_PROC_MEM_SIZE] = "proc.memory.size",
   [PCP_PROC_MEM_RSS] = "proc.memory.rss",
   [PCP_PROC_MEM_SHARE] = "proc.memory.share",
   [PCP_PROC_MEM_TEXTRS] = "proc.memory.textrss",
   [PCP_PROC_MEM_LIBRS] = "proc.memory.librss",
   [PCP_PROC_MEM_DATRS] = "proc.memory.datrss",
   [PCP_PROC_MEM_DIRTY] = "proc.memory.dirty",
   [PCP_PROC_SMAPS_PSS] = "proc.smaps.pss",
   [PCP_PROC_SMAPS_SWAP] = "proc.smaps.swap",
   [PCP_PROC_SMAPS_SWAPPSS] = "proc.smaps.swappss",

   [PCP_METRIC_COUNT] = NULL
};

pmAtomValue* Metric_values(Metric metric, pmAtomValue *atom, int count, int type) {

   pmValueSet* vset = pcp->result->vset[metric];
   if (!vset || vset->numval <= 0)
      return NULL;

   /* allocate space for atom if needed */
   if (!atom || !count) {
      if (!count)
         count = vset->numval;
      atom = xCalloc(count, sizeof(pmAtomValue));
   }

   /* extract requested number of values as requested type */
   const pmDesc* desc = &pcp->descs[metric];
   for (int i = 0; i < vset->numval; i++) {
      if (i == count)
         break;
      const pmValue *value = &vset->vlist[i];
      int sts = pmExtractValue(vset->valfmt, value, desc->type, &atom[i], type);
      if (sts < 0) {
         if (pmDebugOptions.appl0)
            fprintf(stderr, "Error: cannot extract metric value: %s\n",
                            pmErrStr(sts));
         memset(&atom[i], 0, sizeof(pmAtomValue));
      }
   }
   return atom;
}

int Metric_instanceCount(Metric metric) {
   pmValueSet* vset = pcp->result->vset[metric];
   if (vset)
      return vset->numval;
   return 0;
}

int Metric_instanceOffset(Metric metric, int inst) {
   pmValueSet* vset = pcp->result->vset[metric];
   if (!vset || vset->numval <= 0)
      return 0;

   /* search for optimal offset for subsequent inst lookups to begin */
   for (int i = 0; i < vset->numval; i++) {
      if (inst == vset->vlist[i].inst)
         return i;
   }
   return 0;
}

static pmAtomValue *Metric_extract(Metric metric, int inst, int offset, pmValueSet *vset, pmAtomValue *atom, int type) {

   /* extract value (using requested type) of given metric instance */
   const pmDesc* desc = &pcp->descs[metric];
   const pmValue *value = &vset->vlist[offset];
   int sts = pmExtractValue(vset->valfmt, value, desc->type, atom, type);
   if (sts < 0) {
      if (pmDebugOptions.appl0)
         fprintf(stderr, "Error: cannot extract %s instance %d value: %s\n",
                         pcp->names[metric], inst, pmErrStr(sts));
      memset(atom, 0, sizeof(pmAtomValue));
   }
   return atom;
}

pmAtomValue *Metric_instance(Metric metric, int inst, int offset, pmAtomValue *atom, int type) {

   pmValueSet* vset = pcp->result->vset[metric];
   if (!vset || vset->numval <= 0)
      return NULL;

   /* fast-path using heuristic offset based on expected location */
   if (offset >= 0 && offset < vset->numval && inst == vset->vlist[offset].inst)
      return Metric_extract(metric, inst, offset, vset, atom, type);

   /* slow-path using a linear search for the requested instance */
   for (int i = 0; i < vset->numval; i++) {
      if (inst == vset->vlist[i].inst)
         return Metric_extract(metric, inst, i, vset, atom, type);
   }
   return NULL;
}

/*
 * Iterate over a set of instances (incl PM_IN_NULL)
 * returning the next instance identifier and offset.
 *
 * Start it off by passing offset -1 into the routine.
 */
bool Metric_iterate(Metric metric, int* instp, int* offsetp) {
   pmValueSet* vset = pcp->result->vset[metric];
   if (!vset || vset->numval <= 0)
      return false;

   int offset = *offsetp;
   offset = (offset < 0) ? 0 : offset + 1;
   if (offset > vset->numval - 1)
      return false;

   *offsetp = offset;
   *instp = vset->vlist[offset].inst;
   return true;
}

/* Switch on/off a metric for value fetching (sampling) */
void Metric_enable(Metric metric, bool enable) {
   pcp->fetch[metric] = enable ? pcp->pmids[metric] : PM_ID_NULL;
}

bool Metric_enabled(Metric metric) {
   return pcp->fetch[metric] != PM_ID_NULL;
}

void Metric_enableThreads(void) {
   pmValueSet* vset = xCalloc(1, sizeof(pmValueSet));
   vset->vlist[0].inst = PM_IN_NULL;
   vset->vlist[0].value.lval = 1;
   vset->valfmt = PM_VAL_INSITU;
   vset->numval = 1;
   vset->pmid = pcp->pmids[PCP_CONTROL_THREADS];

   pmResult* result = xCalloc(1, sizeof(pmResult));
   result->vset[0] = vset;
   result->numpmid = 1;

   int sts = pmStore(result);
   if (sts < 0 && pmDebugOptions.appl0)
      fprintf(stderr, "Error: cannot enable threads: %s\n", pmErrStr(sts));

   pmFreeResult(result);
}

bool Metric_fetch(struct timeval *timestamp) {
   int sts = pmFetch(pcp->total, pcp->fetch, &pcp->result);
   if (sts < 0) {
      if (pmDebugOptions.appl0)
         fprintf(stderr, "Error: cannot fetch metric values): %s\n",
                 pmErrStr(sts));
      return false;
   }
   if (timestamp)
	*timestamp = pcp->result->timestamp;
   return true;
}

static int Platform_addMetric(Metric id, const char *name) {
   unsigned int i = (unsigned int)id;

   if (i >= PCP_METRIC_COUNT && i >= pcp->total) {
      /* added via configuration files */
      unsigned int j = pcp->total + 1;
      pcp->fetch = xRealloc(pcp->fetch, j * sizeof(pmID));
      pcp->pmids = xRealloc(pcp->pmids, j * sizeof(pmID));
      pcp->names = xRealloc(pcp->names, j * sizeof(char*));
      pcp->descs = xRealloc(pcp->descs, j * sizeof(pmDesc));
      memset(&pcp->descs[i], 0, sizeof(pmDesc));
   }

   pcp->pmids[i] = pcp->fetch[i] = PM_ID_NULL;
   pcp->names[i] = name;
   return ++pcp->total;
}

void Platform_init(void) {
   int sts = pmNewContext(PM_CONTEXT_HOST, "local:");
   if (sts < 0)
      sts = pmNewContext(PM_CONTEXT_LOCAL, NULL);
   if (sts < 0) {
      fprintf(stderr, "Cannot setup PCP metric source: %s\n", pmErrStr(sts));
      exit(1);
   }
   pcp = xCalloc(1, sizeof(Platform));
   pcp->context = sts;
   pcp->fetch = xCalloc(PCP_METRIC_COUNT, sizeof(pmID));
   pcp->pmids = xCalloc(PCP_METRIC_COUNT, sizeof(pmID));
   pcp->names = xCalloc(PCP_METRIC_COUNT, sizeof(char*));
   pcp->descs = xCalloc(PCP_METRIC_COUNT, sizeof(pmDesc));

   for (unsigned int i = 0; i < PCP_METRIC_COUNT; i++)
      Platform_addMetric(i, Platform_metricNames[i]);

   sts = pmLookupName(pcp->total, pcp->names, pcp->pmids);
   if (sts < 0) {
      fprintf(stderr, "Error: cannot lookup metric names: %s\n", pmErrStr(sts));
      exit(1);
   }

   for (unsigned int i = 0; i < pcp->total; i++) {
      pcp->fetch[i] = PM_ID_NULL;	/* default is to not sample */

      /* expect some metrics to be missing - e.g. PMDA not available */
      if (pcp->pmids[i] == PM_ID_NULL)
         continue;

      sts = pmLookupDesc(pcp->pmids[i], &pcp->descs[i]);
      if (sts < 0) {
         if (pmDebugOptions.appl0)
            fprintf(stderr, "Error: cannot lookup metric %s(%s): %s\n",
                    pcp->names[i], pmIDStr(pcp->pmids[i]), pmErrStr(sts));
         pcp->pmids[i] = PM_ID_NULL;
         continue;
      }
   }

   /* set proc.control.perclient.threads to 1 for live contexts */
   Metric_enableThreads();

   /* extract values needed for setup - e.g. cpu count, pid_max */
   Metric_enable(PCP_PID_MAX, true);
   Metric_enable(PCP_BOOTTIME, true);
   Metric_enable(PCP_HINV_NCPU, true);
   Metric_enable(PCP_PERCPU_SYSTEM, true);
   Metric_fetch(NULL);

   for (Metric metric = 0; metric < PCP_PROC_PID; metric++)
      Metric_enable(metric, true);
   Metric_enable(PCP_PID_MAX, false);	/* needed one time only */
   Metric_enable(PCP_BOOTTIME, false);
}

void Platform_done(void) {
   pmDestroyContext(pcp->context);
   free(pcp->fetch);
   free(pcp->pmids);
   free(pcp->names);
   free(pcp->descs);
   free(pcp);
}

void Platform_setBindings(Htop_Action* keys) {
   /* no platform-specific key bindings */
   (void)keys;
}

int Platform_getUptime(void) {
   pmAtomValue value;
   if (Metric_values(PCP_UPTIME, &value, 1, PM_TYPE_32) == NULL)
      return 0;
   return value.l;
}

void Platform_getLoadAverage(double* one, double* five, double* fifteen) {
   *one = *five = *fifteen = 0.0;

   pmAtomValue values[3] = {0};
   if (Metric_values(PCP_LOAD_AVERAGE, values, 3, PM_TYPE_DOUBLE) != NULL) {
      *one = values[0].d;
      *five = values[1].d;
      *fifteen = values[2].d;
   }
}

int Platform_getMaxPid(void) {
   pmAtomValue value;
   if (Metric_values(PCP_PID_MAX, &value, 1, PM_TYPE_32) == NULL)
      return -1;
   return value.l;
}

static double Platform_setOneCPUValues(Meter* this, pmAtomValue* values) {

   unsigned long long value = values[CPU_TOTAL_PERIOD].ull;
   double total = (double) (value == 0 ? 1 : value);
   double percent;

   double* v = this->values;
   v[CPU_METER_NICE] = values[CPU_NICE_PERIOD].ull / total * 100.0;
   v[CPU_METER_NORMAL] = values[CPU_USER_PERIOD].ull / total * 100.0;
   if (this->pl->settings->detailedCPUTime) {
      v[CPU_METER_KERNEL]  = values[CPU_SYSTEM_PERIOD].ull / total * 100.0;
      v[CPU_METER_IRQ]     = values[CPU_IRQ_PERIOD].ull / total * 100.0;
      v[CPU_METER_SOFTIRQ] = values[CPU_SOFTIRQ_PERIOD].ull / total * 100.0;
      v[CPU_METER_STEAL]   = values[CPU_STEAL_PERIOD].ull / total * 100.0;
      v[CPU_METER_GUEST]   = values[CPU_GUEST_PERIOD].ull / total * 100.0;
      v[CPU_METER_IOWAIT]  = values[CPU_IOWAIT_PERIOD].ull / total * 100.0;
      this->curItems = 8;
      if (this->pl->settings->accountGuestInCPUMeter)
         percent = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6];
      else
         percent = v[0] + v[1] + v[2] + v[3] + v[4];
   } else {
      v[2] = values[CPU_SYSTEM_ALL_PERIOD].ull / total * 100.0;
      value = values[CPU_STEAL_PERIOD].ull + values[CPU_GUEST_PERIOD].ull;
      v[3] = value / total * 100.0;
      this->curItems = 4;
      percent = v[0] + v[1] + v[2] + v[3];
   }
   percent = CLAMP(percent, 0.0, 100.0);
   if (isnan(percent))
      percent = 0.0;

   v[CPU_METER_FREQUENCY] = values[CPU_FREQUENCY].d;
   v[CPU_METER_TEMPERATURE] = NAN;

   return percent;
}

double Platform_setCPUValues(Meter* this, int cpu) {
   const PCPProcessList* pl = (const PCPProcessList*) this->pl;
   if (cpu <= 0)	/* use aggregate values */
      return Platform_setOneCPUValues(this, pl->cpu);
   return Platform_setOneCPUValues(this, pl->percpu[cpu - 1]);
}

void Platform_setMemoryValues(Meter* this) {
   const ProcessList* pl = this->pl;
   long int usedMem = pl->usedMem;
   long int buffersMem = pl->buffersMem;
   long int cachedMem = pl->cachedMem;
   usedMem -= buffersMem + cachedMem;
   this->total = pl->totalMem;
   this->values[0] = usedMem;
   this->values[1] = buffersMem;
   this->values[2] = cachedMem;
}

void Platform_setSwapValues(Meter* this) {
   const ProcessList* pl = this->pl;
   this->total = pl->totalSwap;
   this->values[0] = pl->usedSwap;
}

void Platform_setZramValues(Meter* this) {
   (void)this;

   int i, count = Metric_instanceCount(PCP_ZRAM_CAPACITY);
   pmAtomValue *values = xCalloc(count, sizeof(pmAtomValue));
   ZramStats stats = {0};

   if (Metric_values(PCP_ZRAM_CAPACITY, values, count, PM_TYPE_U64)) {
      for (i = 0; i < count; i++)
          stats.totalZram += values[i].ull;
   }
   if (Metric_values(PCP_ZRAM_ORIGINAL, values, count, PM_TYPE_U64)) {
      for (i = 0; i < count; i++)
          stats.usedZramOrig += values[i].ull;
   }
   if (Metric_values(PCP_ZRAM_COMPRESSED, values, count, PM_TYPE_U64)) {
      for (i = 0; i < count; i++)
          stats.usedZramComp += values[i].ull;
   }

   free(values);

   this->total = stats.totalZram;
   this->values[0] = stats.usedZramComp;
   this->values[1] = stats.usedZramOrig;
}

char* Platform_getProcessEnv(pid_t pid) {
   pmAtomValue value;
   if (!Metric_instance(PCP_PROC_ENVIRON, pid, 0, &value, PM_TYPE_STRING))
      return NULL;
   return value.cp;
}

char* Platform_getInodeFilename(pid_t pid, ino_t inode) {
   (void)pid;
   (void)inode;
   return NULL;
}

FileLocks_ProcessData* Platform_getProcessLocks(pid_t pid) {
   (void)pid;
   return NULL;
}

void Platform_getPressureStall(const char* file, bool some, double* ten, double* sixty, double* threehundred) {
   *ten = *sixty = *threehundred = 0;

   Metric metric;
   if (String_eq(file, "cpu"))
      metric = PCP_PSI_CPUSOME;
   else if (String_eq(file, "io"))
      metric = some ? PCP_PSI_IOSOME : PCP_PSI_IOFULL;
   else if (String_eq(file, "mem"))
      metric = some ? PCP_PSI_MEMSOME : PCP_PSI_MEMFULL;
   else
      return;

   pmAtomValue values[3] = {0};
   if (Metric_values(metric, values, 3, PM_TYPE_DOUBLE) != NULL) {
      *ten = values[0].d;
      *sixty = values[1].d;
      *threehundred = values[2].d;
   }
}

bool Platform_getDiskIO(DiskIOData* data) {
   data->totalBytesRead = 0;
   data->totalBytesWritten = 0;
   data->totalMsTimeSpend = 0;

   pmAtomValue value;
   if (Metric_values(PCP_DISK_READB, &value, 1, PM_TYPE_U64) != NULL)
      data->totalBytesRead = value.ull;
   if (Metric_values(PCP_DISK_WRITEB, &value, 1, PM_TYPE_U64) != NULL)
      data->totalBytesWritten = value.ull;
   if (Metric_values(PCP_DISK_ACTIVE, &value, 1, PM_TYPE_U64) != NULL)
      data->totalMsTimeSpend = value.ull;
   return true;
}

bool Platform_getNetworkIO(unsigned long int* bytesReceived,
                           unsigned long int* packetsReceived,
                           unsigned long int* bytesTransmitted,
                           unsigned long int* packetsTransmitted) {
   *bytesReceived = 0;
   *packetsReceived = 0;
   *bytesTransmitted = 0;
   *packetsTransmitted = 0;

   pmAtomValue value;
   if (Metric_values(PCP_NET_RECVB, &value, 1, PM_TYPE_U64) != NULL)
      *bytesReceived = value.ull;
   if (Metric_values(PCP_NET_SENDB, &value, 1, PM_TYPE_U64) != NULL)
      *bytesTransmitted = value.ull;
   if (Metric_values(PCP_NET_RECVP, &value, 1, PM_TYPE_U64) != NULL)
      *packetsReceived = value.ull;
   if (Metric_values(PCP_NET_SENDP, &value, 1, PM_TYPE_U64) != NULL)
      *packetsTransmitted = value.ull;
   return true;
}

void Platform_getBattery(double* level, ACPresence* isOnAC) {
   *level = NAN;
   *isOnAC = AC_ERROR;
}
