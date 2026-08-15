#ifndef _INSTRUCTION_EXECUTION_LOOP_H_
#define _INSTRUCTION_EXECUTION_LOOP_H_

#include <efi.h>
#include "uarch_config.h"

EFI_STATUS init_perf_counters(const struct perf_counter_info* this_cpu_perf_counters);
void execute_ud(void);
void dump_bruteforce_config(void);

#endif
