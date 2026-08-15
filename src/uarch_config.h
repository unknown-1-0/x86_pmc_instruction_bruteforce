#ifndef _UARCH_CONFIG_H_
#define _UARCH_CONFIG_H_

#include <efi.h>
#include <stdint.h>
#include <xed-chip-enum.h>

enum perf_counters_ids
{
    UOPS_ISSUED_ANY = 0,
#ifdef COUNT_RETIRED
    UOPS_RETIRED_ALL,
    UOPS_RETIRED_SLOTS,
#endif
#ifdef COUNT_NOPS
    INST_RETIRED_NOP,
#endif
#ifdef COUNT_IDQ
    IDQ_MITE_UOPS,
    IDQ_DSB_UOPS,
    IDQ_MS_UOPS,
#endif
    PERF_EVENTS_COUNT
};

struct perf_counter_info
{
    CHAR16* name;
    uint16_t event_selector;
};

struct cpuid_family_model_info
{
    uint16_t family;
    uint16_t model;
};

struct uarch_config
{
    CHAR16* uarch_name;
    xed_chip_enum_t xed_chip;
    const struct cpuid_family_model_info* cpu_models;
    size_t cpu_models_count;
    struct perf_counter_info perf_counters[PERF_EVENTS_COUNT];
};

const struct uarch_config* get_uarch_config_for_cpu(void);

#endif
