#include "print.h"
#include "uarch_config.h"

static const struct cpuid_family_model_info skylake_cpu_models[] = {
    { .family = 6, .model = 0x4e },
    { .family = 6, .model = 0x5e },
    { .family = 6, .model = 0x8e },
    { .family = 6, .model = 0x9e },
    { .family = 6, .model = 0xa5 },
    { .family = 6, .model = 0xa6 },
};
__attribute__((maybe_unused))
static const struct uarch_config intel_uarch_configs[] = {
    {
        .uarch_name = L"Skylake",
        .xed_chip = XED_CHIP_SKYLAKE,
        .cpu_models = skylake_cpu_models,
        .cpu_models_count =
            sizeof(skylake_cpu_models)/sizeof(skylake_cpu_models[0]),
        .perf_counters = {
            [UOPS_ISSUED_ANY] =
            {.name=L"UOPS_ISSUED.ANY", .event_selector=0x010e},
#ifdef COUNT_RETIRED
            // From Haswell and Broadwell docs, seems to work fine on Skylake
            [UOPS_RETIRED_ALL] =
            {.name=L"UOPS_RETIRED.ALL", .event_selector=0x01c2},
            [UOPS_RETIRED_SLOTS] =
            {.name=L"UOPS_RETIRED.RETIRE_SLOTS", .event_selector=0x02c2},
#endif
#ifdef COUNT_NOPS
            [INST_RETIRED_NOP] =
            {.name=L"INST_RETIRED.NOP", .event_selector=0x02c0},
#endif
#ifdef COUNT_IDQ
            [IDQ_MITE_UOPS]=
            {.name=L"IDQ.MITE_UOPS", .event_selector=0x0479},
            [IDQ_DSB_UOPS]=
            {.name=L"IDQ.DSB_UOPS", .event_selector=0x0879},
            [IDQ_MS_UOPS]=
            {.name=L"IDQ.MS_UOPS", .event_selector=0x2079},
#endif
        }
    }
};

static const struct uarch_config* get_uarch_config_for_intel_cpu(void)
{
    print(L"Intel CPU detected!\r\n");

    uint32_t eax = 1;
    __asm__("cpuid":"+a"(eax)::"rbx","rcx","rdx");

    uint16_t model = (eax >> 4) & 0xf;
    uint16_t family = (eax >> 8) & 0xf;

    if (family == 0x6 || family == 0xf)
    {
        model |= (eax >> (16-4)) & 0xf0;
    }

    if (family == 0xf)
    {
        family += (eax >> 20) & 0xff;
    }

    printf(L"CPU family: 0x%x model: 0x%x\r\n", family, model);

    for (size_t i = 0;
            i < sizeof(intel_uarch_configs)/sizeof(intel_uarch_configs[0]); i++)
    {
        const struct uarch_config* cur_uarch = &intel_uarch_configs[i];
        for (size_t j = 0; j < cur_uarch->cpu_models_count; j++)
        {
            const struct cpuid_family_model_info* cur_info = &cur_uarch->cpu_models[j];
            if (family == cur_info->family && model == cur_info->model)
            {
                printf(L"Detected microarchitecture: %s\r\n", cur_uarch->uarch_name);
                return cur_uarch;
            }
        }
    }
    return NULL;
}

const struct uarch_config* get_uarch_config_for_cpu(void)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    __asm__("cpuid":"+a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx));

    if (ebx == 0x756e6547 && ecx == 0x6c65746e && edx == 0x49656e69)
    {
        return get_uarch_config_for_intel_cpu();
    }

    printf(L"Unknown CPU vendor!\r\n"
           L"CPUID.00H: EAX=0x%x EBX=0x%x ECX=0x%x EDX=0x%x\r\n",
           eax, ebx, ecx, edx);

    return NULL;
}

