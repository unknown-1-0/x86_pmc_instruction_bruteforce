#include "control_registers.h"
#include "disasm.h"
#include "exception_context.h"
#include "msr.h"
#include "print.h"
#include "save_file.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <xed-decode.h>
#include <xed-decoded-inst-api.h>


static inline void __attribute__((noreturn)) halt(void)
{
    print(L"Halting CPU\r\n");
    while(1)
    {
        __asm__("cli");
        __asm__("hlt");
    }
}

#define MSR_IA32_PMC0 0xc1
#define MSR_IA32_PMC(i) (MSR_IA32_PMC0 + (i))

#define MSR_IA32_PERFEVTSEL0 0x186
#define MSR_IA32_PERFEVTSEL(i) (MSR_IA32_PERFEVTSEL0 + (i))
#define PERFEVTSEL_ENABLE (1ULL<<22)
#define PERFEVTSEL_OS (1ULL<<17)
#define PERFEVTSEL_USER (1ULL<<16)

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

#define SKYLAKE 1
#define ALDER_LAKE 2

static const uint16_t perf_counters_event_masks[PERF_EVENTS_COUNT] = {
#if TARGET_UARCH == SKYLAKE
    [UOPS_ISSUED_ANY] = 0x010e,
#ifdef COUNT_RETIRED
    // From Haswell and Broadwell docs, seems to work fine on Skylake
    [UOPS_RETIRED_ALL] = 0x01c2,
    [UOPS_RETIRED_SLOTS] = 0x02c2,
#endif
#ifdef COUNT_IDQ
    [IDQ_MITE_UOPS] = 0x0479,
    [IDQ_DSB_UOPS] = 0x0879,
    [IDQ_MS_UOPS] = 0x3079,
#endif
#elif TARGET_UARCH == ALDER_LAKE
    [UOPS_ISSUED_ANY] = 0x01ae,
#ifdef COUNT_RETIRED
    // Actually UOPS_RETIRED.HEAVY
    [UOPS_RETIRED_ALL] = 0x01c2,
    [UOPS_RETIRED_SLOTS] = 0x02c2,
#endif
#ifdef COUNT_IDQ
    [IDQ_MITE_UOPS] = 0x0479,
    [IDQ_DSB_UOPS] = 0x0879,
    [IDQ_MS_UOPS] = 0x2079,
#endif
#else
#error Unknown target CPU microarchitecture
#endif

#ifdef COUNT_NOPS
    [INST_RETIRED_NOP] = 0x02c0,
#endif
};

static const CHAR16* perf_events_names[PERF_EVENTS_COUNT] = {
    [UOPS_ISSUED_ANY] = L"UOPS_ISSUED.ANY",
#ifdef COUNT_RETIRED
#if TARGET_UARCH == SKYLAKE
    [UOPS_RETIRED_ALL] = L"UOPS_RETIRED.ALL",
    [UOPS_RETIRED_SLOTS] = L"UOPS_RETIRED.RETIRE_SLOTS",
#elif TARGET_UARCH == ALDER_LAKE
    [UOPS_RETIRED_ALL] = L"UOPS_RETIRED.HEAVY",
    [UOPS_RETIRED_SLOTS] = L"UOPS_RETIRED.SLOTS",
#endif
#endif
#ifdef COUNT_NOPS
    [INST_RETIRED_NOP] = L"INST_RETIRED.NOP",
#endif
#ifdef COUNT_IDQ
    [IDQ_MITE_UOPS] = L"IDQ.MITE_UOPS",
    [IDQ_DSB_UOPS] = L"IDQ.DSB_UOPS",
    [IDQ_MS_UOPS] = L"IDQ.MS_UOPS",
#endif
};

#undef SKYLAKE
#undef ALDER_LAKE

void dump_bruteforce_config(void)
{
    for (size_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        printf(L"%s PMC UMask:EventCode = 0x%lx\r\n",
            perf_events_names[i], perf_counters_event_masks[i]);
    }

#if CPU_MODE == 64
    print(L"Bruteforce in 64-bit userspace\r\n");
#elif CPU_MODE == 32
    print(L"Bruteforce in 32-bit userspace\r\n");
#elif CPU_MODE == 16
    print(L"Bruteforce in 16-bit userspace\r\n");
#else
#error Unknown CPU mode
#endif
}

#define MSR_IA32_FIXED_CTR_CTRL 0x38d
#define MSR_IA32_PERF_GLOBAL_CTRL 0x38f

static uint64_t perf_global_ctrl_enable_all = 0;
void init_perf_counters(void)
{
    uint32_t eax = 0xa;
    __asm__("cpuid":"+a"(eax)::"rbx","rcx","rdx");

    uint8_t counters_available = (eax >> 8) & 0xff;

    printf(L"General-purpose performance counters available: 0x%x\r\n", counters_available);

    if (PERF_EVENTS_COUNT > counters_available)
    {
        printf(L"Not enough performance counters! Events to count: 0x%x > 0x%x!\r\n",
                PERF_EVENTS_COUNT, counters_available);
        print(L"Try to disable hyper-threading in the firmware settings.\r\n"
              L"If that does not help, reduce the number of events to count.\r\n");
        halt();
    }


    wrmsr(MSR_IA32_FIXED_CTR_CTRL, 0);
    wrmsr(MSR_IA32_PERF_GLOBAL_CTRL, 0);
    for (uint16_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        perf_global_ctrl_enable_all |= 1ULL << i;
        wrmsr(MSR_IA32_PERFEVTSEL(i),
                PERFEVTSEL_ENABLE | PERFEVTSEL_USER | perf_counters_event_masks[i]);
    }
}

uint8_t* user_code_page_rw = NULL;
uint8_t* user_code_page_for_exec = NULL;
void __attribute__((noreturn)) enter_user(void* rip, void* rsp);
void execute_instruction(const uint8_t* instruction, size_t size)
{
    wrmsr(MSR_IA32_PERF_GLOBAL_CTRL, 0);

    for (uint16_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        wrmsr(MSR_IA32_PMC(i), 0);
    }

    wrmsr(MSR_IA32_PERF_GLOBAL_CTRL, perf_global_ctrl_enable_all);

    size_t instruction_start_offset = 0x1000 - size;

    uint8_t* user_code_start = user_code_page_rw + instruction_start_offset;
    memcpy(user_code_start, instruction, size);
    __asm__ volatile("clflush %0"::"m"(*user_code_start));
    enter_user(user_code_page_for_exec + instruction_start_offset, NULL);
}

uint8_t instruction_bytes[15] = {0x00};
size_t cur_instruction_length = 1;
size_t cur_byte_index = 0;

void execute_current_instruction(void)
{
    execute_instruction(instruction_bytes, cur_instruction_length);
}

#define EXCEPTION_DEBUG 1
#define EXCEPTION_INVALID_OPCODE 6
#define EXCEPTION_PAGE_FAULT 14
#define PF_USER (1ULL<<2)
#define PF_INSTRUCTION_FETCH (1ULL<<4)

#define EXCEPTION_MACHINE_CHECK 18
#define MSR_IA32_MCG_STATUS 0x17a
#define MCG_STATUS_MCIP (1ULL<<2)

#define MSR_IA32_MC0_STATUS 0x401
#define MC_STATUS_VALID (1ULL<<63)

size_t last_instruction_length = 0;
void increment_instruction_length_and_retry_exec(void)
{
    if (cur_instruction_length >= sizeof(instruction_bytes)/sizeof(instruction_bytes[0]))
    {
        return;
    }

    cur_instruction_length++;

    execute_current_instruction();
}

static size_t unique_instructions_executed = 0;
static size_t instructions_saved = 0;
static size_t kernel_exceptions = 0;
static size_t kernel_page_faults = 0;
static size_t undocumented_behind_ud = 0;
static size_t undocumented_not_ud = 0;
static size_t vex_malformed_but_accepted = 0;
#ifdef COUNT_NOPS
static size_t nops_with_side_effects = 0;
#endif
static size_t machine_checks = 0;
#ifdef COUNT_XED_VS_CPU_MISMATCHES
static size_t cpu_xed_length_mismatches = 0;
#endif

static CHAR16* exception_names[] = {
    L"#DE", L"#DB", L"NMI", L"#BP",
    L"#OF", L"#BR", L"#UD", L"#NM",
    L"#DF", NULL,   L"#TS", L"#NP",
    L"#SS", L"#GP", L"#PF", NULL,
    L"#MF", L"#AC", L"#MC", L"#XM",
    L"#VE", L"#CP"
};

void dump_stats(struct context* context,
                uint8_t* instruction_bytes,
                size_t instruction_length,
                bool extra_info_present,
                uint64_t extra_info,
                uint64_t perf_counters_values[PERF_EVENTS_COUNT],
                uint64_t xed_length)
{
    printf(L"Unique instructions executed: 0x%lx, saved: 0x%lx\r\n",
            unique_instructions_executed, instructions_saved);

    printf(L"Current save file size: 0x%lx bytes\r\n", get_save_file_position());
    printf(L"Kernel: page faults: 0x%lx, exceptions: 0x%lx\r\n",
            kernel_page_faults, kernel_exceptions);

    printf(L"Undocumented instructions: hidden behind #UD: 0x%lx, not hidden: 0x%lx\r\n",
            undocumented_behind_ud, undocumented_not_ud);

#ifdef COUNT_NOPS
    printf(L"VEX malformed but accepted: 0x%lx, NOPs with side effects: 0x%lx\r\n", vex_malformed_but_accepted, nops_with_side_effects);
#else
    printf(L"VEX malformed but accepted: 0x%lx\r\n", vex_malformed_but_accepted);
#endif

#ifdef COUNT_XED_VS_CPU_MISMATCHES
    printf(L"Machine checks: 0x%lx, Intel XED vs CPU decoder length mismatches: 0x%lx\r\n", machine_checks, cpu_xed_length_mismatches);
#else
    printf(L"Machine checks: 0x%lx\r\n", machine_checks);
#endif
    print(L"Current instruction info:\r\n");
    uint64_t exception_number = context->exception_number;
    bool has_error_code = ((1ULL << exception_number) & EXCEPTIONS_WITH_ERROR_CODE_MASK) != 0;

    CHAR16* exception_name = exception_number >= sizeof(exception_names)/sizeof(exception_names[0]) ? NULL : exception_names[exception_number];

    if (exception_name == NULL)
    {
        printf(L"#%lx", exception_number);
    }
    else
    {
        print(exception_name);
    }

    if (has_error_code)
    {
        printf(L"(0x%lx)", context->error_code);
    }

    print(L" -");

    for (size_t i = 0; i < instruction_length; i++)
    {
        printf(L" %hx", instruction_bytes[i]);
    }

    printf(L" (Intel XED length: 0x%lx", xed_length);
    if (extra_info_present)
    {
        printf(L", extra info: 0x%lx", extra_info);
    }

    print(L")\r\n");

    print(L"Performance counters values:\r\n");

    for (size_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        printf(L"%s: 0x%lx\r\n", perf_events_names[i], perf_counters_values[i]);
    }

}

static bool is_prefix(uint8_t byte, uint8_t next_byte)
{
    // Segment override prefixes
    if ((byte & ~(0x26U ^ 0x3eU)) == 0x26U)
    {
        return true;
    }
#if CPU_MODE == 64
    // REX
    if ((byte & 0xf0U) == 0x40)
    {
        return true;
    }
#endif
    // EVEX
    if (byte == 0x62)
    {
#if CPU_MODE == 64
        (void)next_byte;
        return true;
#else
        return next_byte >= 0xc0;
#endif
    }

    switch(byte & 0xfeU)
    {
    case 0x66: // Operand (0x66)/address (0x67) size override
    case 0x64: // FS (0x64)/GS (0x65) segment override
    case 0xc4: // VEX (0xC4/0xC5)
    case 0xf2: // REPNE (0xF2) / REP/REPE (0xF3)
        return true;
    default:
        return byte == 0xf0; // LOCK prefix
    }
}

static size_t count_prefixes(const uint8_t* bytes, size_t length)
{
    size_t prefixes = 0;
    while (prefixes < length)
    {
        uint8_t byte = bytes[prefixes];
        uint8_t next_byte = (prefixes < length + 1) ? bytes[prefixes + 1] : 0;

        if (is_prefix(byte, next_byte))
        {
            prefixes++;

            // VEX prefix, if present, is the last prefix
	        if ((byte & 0xfeU) == 0xc4U)
            {
                break;
            }
        }
        else
        {
            break;
        }
    }

    return prefixes;
}

#define MAX_PREFIXES 1
static bool malformed_instruction_expect_ud = false;
static bool contains_invalid_count_of_prefixes(const uint8_t* bytes, size_t length)
{
    bool segment_override_seen = false;
    bool operand_size_override_seen = false;
    bool address_size_override_seen = false;
    bool lock_seen = false;
    bool repne_seen = false;
    bool rep_seen = false;
    bool rex_seen = false;
    bool evex_seen = false;
    bool vex_seen = false;
    bool vex_invalid = false;
    bool vex_malformed_instruction_expect_ud = false;

    malformed_instruction_expect_ud = false;
    for (size_t i = 0; i < length; i++)
    {
        uint8_t byte = bytes[i];
        switch(byte)
        {
        case 0x26:
        case 0x2e:
        case 0x36:
        case 0x3e:
        case 0x64:
        case 0x65:
            if (rex_seen || segment_override_seen)
            {
                return true;
            }
            segment_override_seen = true;
            break;
        case 0x62:
#if CPU_MODE != 64
            if (i + 1 == length || bytes[i+1] < 0xc0)
            {
                // May be BOUND instruction
                return false;
            }
#endif
            evex_seen = true;
            break;
        case 0x66:
            if (rex_seen || operand_size_override_seen)
            {
                return true;
            }
            operand_size_override_seen = true;
            break;
        case 0x67:
            if (rex_seen || address_size_override_seen)
            {
                return true;
            }
            address_size_override_seen = true;
            break;
        case 0xf0:
            if (rex_seen || lock_seen)
            {
                return true;
            }
            lock_seen = true;
            break;
        case 0xf2:
            if (rex_seen || repne_seen)
            {
                return true;
            }
            repne_seen = true;
            break;
        case 0xf3:
            if (rex_seen || rep_seen)
            {
                return true;
            }
            rep_seen = true;
            break;
        case 0xc4:
        case 0xc5:
#if CPU_MODE != 64
            if (i+1 >= length || bytes[i+1] < 0xc0)
            {
                // LDS/LES instruction
                return false;
            }
#endif
            // VEX prefix contains various fields in the next 1-2 bytes,
            // and, if it's present, the opcode bytes are located immediately after it

            // Intel SDM says that instructions with 0x66, 0xf2, 0xf3, LOCK and REX prefixes preceding VEX will #UD.
            // However, VEX prefix can only encode 0x66, 0xf2, 0xf3 prefixes (+ REX, in 3-byte form)
            // For this reason, keep LOCK prefix (and REX, if using 2-byte form of VEX) just in case some instructions hide there
            vex_invalid = (byte == 0xc4 && rex_seen) || operand_size_override_seen || rep_seen || repne_seen;
            vex_malformed_instruction_expect_ud = (!vex_invalid && ((byte == 0xc5 && rex_seen) || lock_seen));
            vex_seen = true;
            break;
        default:
#if CPU_MODE == 64
            if ((byte & 0xf0U) == 0x40U)
            {
                if (rex_seen)
                {
                    return true;
                }
                rex_seen = true;
                break;
            }
            else
#endif
            {
                return false;
            }
        }

        if (i >= MAX_PREFIXES)
        {
            return true;
        }

        if (evex_seen)
        {
            if (i + 3 >= length)
            {
                return false;
            }

            uint8_t p0 = bytes[i+1];
            if ((p0 & 0b11110000) != 0b11110000)
            {
                return true;
            }

            uint8_t p1 = bytes[i+2];
            if (p1 & 0b01111000)
            {
                return true;
            }

            uint8_t p2 = bytes[i+3];
            if ((p2 & 0b10011111) != 0b00001000)
            {
                return true;
            }

            return false;
        }

        if (vex_seen)
        {
            if (byte == 0xc4 && i+2 < length)
            {
                uint8_t b1 = bytes[i+1];
                if ((b1 & 0b11100000) != 0b11100000)
                {
                    return true;
                }

                uint8_t b2 = bytes[i+2];
                if ((b2 & 0b01111000) != 0b01111000)
                {
                    return true;
                }
            }
            else if (byte == 0xc5 && i+1 < length)
            {
                uint8_t b1 = bytes[i+1];
                if ((b1 & 0b11111000) != 0b11111000)
                {
                    return true;
                }
            }

            malformed_instruction_expect_ud = vex_malformed_instruction_expect_ud;
            return vex_invalid;
        }

    }
    return false;
}

void execute_ud(void)
{
    uint8_t ud2[] = { 0x0f, 0x0b };
    execute_instruction(ud2, sizeof(ud2));
}

static bool measuring_ud = true;
static uint64_t ud_perf_counters_values[PERF_EVENTS_COUNT] = {0};

void check_ud2_measurements(struct context* context, uint64_t cur_perf_counters_values[PERF_EVENTS_COUNT])
{
    if (context->exception_number != EXCEPTION_INVALID_OPCODE)
    {
        printf(L"Unexpected exception %lx while measuring performance counters values for UD2\r\n", context->exception_number);

        bool has_error_code = ((1ULL << context->exception_number) & EXCEPTIONS_WITH_ERROR_CODE_MASK) != 0;
        struct iretq_frame* frame = has_error_code ? &context->iretq_frame_with_error_code : &context->iretq_frame_no_error_code;

        if (has_error_code)
        {
            printf(L"Error code: %lx\r\n", context->error_code);
        }

        printf(L"CS:RIP = %lx:%lx RFLAGS=%lx\r\n", frame->cs, frame->rip, frame->rflags);
        printf(L"SS:RSP = %lx:%lx\r\n", frame->ss, frame->rsp);
        close_save_file();
        halt();
    }

    bool restart_required = false;

    for (size_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        restart_required = restart_required ||
        (cur_perf_counters_values[i] != ud_perf_counters_values[i]);

        ud_perf_counters_values[i] = cur_perf_counters_values[i];
    }

    if (restart_required)
    {
        execute_ud();
    }

    measuring_ud = false;
    print(L"UD2 performance counters values:\r\n");
    for (size_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        printf(L"%s: 0x%lx\r\n",
            perf_events_names[i], ud_perf_counters_values[i]);
    }

    EFI_STATUS status = save_uint64_array(ud_perf_counters_values, PERF_EVENTS_COUNT);

    print(L"\r\n");

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not save UD2 performance counters values, status = 0x%lx\r\n", status);
        print(L"Closing save file\r\n");
        close_save_file();
        halt();
    }
}

#ifdef COUNT_NOPS
void execute_nop(void)
{
    uint8_t nop[] = { 0x90 };
    execute_instruction(nop, sizeof(nop));
}

static bool measuring_nop = false;
static uint64_t nop_perf_counters_values[PERF_EVENTS_COUNT] = {0};

void check_nop_measurements(struct context* context, uint64_t cur_perf_counters_values[PERF_EVENTS_COUNT])
{
    if (context->exception_number != EXCEPTION_DEBUG)
    {
        printf(L"Unexpected exception %lx while measuring performance counters values for NOP\r\n", context->exception_number);

        bool has_error_code = ((1ULL << context->exception_number) & EXCEPTIONS_WITH_ERROR_CODE_MASK) != 0;
        struct iretq_frame* frame = has_error_code ? &context->iretq_frame_with_error_code : &context->iretq_frame_no_error_code;

        if (has_error_code)
        {
            printf(L"Error code: %lx\r\n", context->error_code);
        }

        printf(L"CS:RIP = %lx:%lx RFLAGS=%lx\r\n", frame->cs, frame->rip, frame->rflags);
        printf(L"SS:RSP = %lx:%lx\r\n", frame->ss, frame->rsp);
        close_save_file();
        halt();
    }

    if (cur_perf_counters_values[INST_RETIRED_NOP] != 1)
    {
        printf(L"Unexpected count of NOPs retired: expected: 1, got: 0x%lx\r\n",
            cur_perf_counters_values[INST_RETIRED_NOP]);
        close_save_file();
        halt();
    }

    bool restart_required = false;

    for (size_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        restart_required = restart_required ||
        (cur_perf_counters_values[i] != nop_perf_counters_values[i]);

        nop_perf_counters_values[i] = cur_perf_counters_values[i];
    }

    if (restart_required)
    {
        execute_nop();
    }

    measuring_nop = false;
    print(L"NOP performance counters values:\r\n");

    for (size_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        printf(L"%s: 0x%lx\r\n",
            perf_events_names[i], nop_perf_counters_values[i]);
    }

    EFI_STATUS status = save_uint64_array(nop_perf_counters_values, PERF_EVENTS_COUNT);

    print(L"\r\n");

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not save NOP performance counters values, status = 0x%lx\r\n", status);
        print(L"Closing save file\r\n");
        close_save_file();
        halt();
    }
}
#endif

static uint64_t last_perf_counters_values[PERF_EVENTS_COUNT] = {0};

static void restart_on_unstable_counters_values(uint64_t cur_perf_counters_values[PERF_EVENTS_COUNT])
{
    bool restart_required = false;

    for (size_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        restart_required = restart_required ||
            (last_perf_counters_values[i] != cur_perf_counters_values[i]);

        last_perf_counters_values[i] = cur_perf_counters_values[i];
    }

    if (restart_required)
    {
        execute_current_instruction();
    }
    else
    {
        memset(last_perf_counters_values, 0, sizeof(last_perf_counters_values));
    }
}

static bool perf_counters_values_match(
        const uint64_t cur_values[PERF_EVENTS_COUNT],
        const uint64_t reference_values[PERF_EVENTS_COUNT]
        )
{
    for (size_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        if (__builtin_expect(cur_values[i] != reference_values[i], 0))
        {
            return false;
        }
    }
    return true;
}

static bool flush_required = false;

#define MSR_IA32_MCG_CAP 0x179
#define MCG_CAP_REPORTING_BANKS_COUNT_MASK 0xfULL
void check_last_instruction(struct context* context, uint64_t cur_perf_counters_values[PERF_EVENTS_COUNT])
{
    bool has_error_code = ((1ULL << context->exception_number) & EXCEPTIONS_WITH_ERROR_CODE_MASK) != 0;
    struct iretq_frame* frame = has_error_code ? &context->iretq_frame_with_error_code : &context->iretq_frame_no_error_code;

    enum instruction_type instruction_type = NOT_INTERESTING;

    if (__builtin_expect((frame->cs & 3) == 3, 1))
    {
        if (__builtin_expect(context->exception_number == EXCEPTION_PAGE_FAULT && (context->error_code & PF_INSTRUCTION_FETCH), 1))
        {
            uint64_t cr2 = read_cr2();

            if (__builtin_expect(cr2 == (size_t)user_code_page_for_exec + 0x1000 && frame->rip != cr2, 1))
            {
                increment_instruction_length_and_retry_exec();
            }
        }
    }
    else
    {
        printf(L"Kernel Exception! CS:RIP=%lx:%lx SS:RSP=%lx:%lx RFLAGS=%lx\r\n", frame->cs, frame->rip, frame->ss, frame->rsp, frame->rflags);
        instruction_type = KERNEL_EXCEPTION;

        print(L"Instruction bytes:");

        for (size_t i = 0; i < 0x40; i++)
        {
            printf(L" %hx", ((uint8_t*)frame->rip)[i]);
        }

        print(L"\r\n");
    }


    if (__builtin_expect(malformed_instruction_expect_ud && context->exception_number != EXCEPTION_INVALID_OPCODE, 0))
    {
        instruction_type = VEX_MALFORMED_BUT_ACCEPTED;
    }

    bool extra_info_present = false;
    uint64_t extra_info = 0;

    uint64_t disasm_length = disasm_get_instruction_length(instruction_bytes, cur_instruction_length);
    bool instruction_is_known = (disasm_length != 0);
#ifdef COUNT_XED_VS_CPU_MISMATCHES
    if (instruction_is_known && cur_instruction_length != disasm_length)
    {
        instruction_type = XED_LENGTH_MISMATCH;
    }
#endif
    switch (context->exception_number)
    {
    case EXCEPTION_PAGE_FAULT:
        if (__builtin_expect(!(context->error_code & PF_USER), 0))
        {
            extra_info = read_cr2();
            extra_info_present = true;
            instruction_type = KERNEL_PAGE_FAULT;
        }
        __attribute__((fallthrough));
    default:
        if (!instruction_is_known)
        {
            instruction_type = UNDOCUMENTED_NOT_UD;
        }
#ifdef COUNT_NOPS
        else if (cur_perf_counters_values[INST_RETIRED_NOP])
        {
            if (context->exception_number != EXCEPTION_DEBUG)
            {
                instruction_type = NOP_WITH_SIDE_EFFECTS;
            }
            else
            {
                restart_on_unstable_counters_values(cur_perf_counters_values);

                if (!perf_counters_values_match(
                            cur_perf_counters_values,
                            nop_perf_counters_values))
                {
                    instruction_type = NOP_WITH_SIDE_EFFECTS;
                }
            }
        }
#endif
        break;
    case EXCEPTION_INVALID_OPCODE:
        {
            restart_on_unstable_counters_values(cur_perf_counters_values);

            if (!instruction_is_known && !perf_counters_values_match(cur_perf_counters_values, ud_perf_counters_values))
            {
                instruction_type = UNDOCUMENTED_UD;
            }
        }
        break;
    case EXCEPTION_MACHINE_CHECK:
        instruction_type = MACHINE_CHECK;
        extra_info_present = true;
        print(L"!!! MACHINE CHECK !!!\r\n");
        uint64_t mcg_status = rdmsr(MSR_IA32_MCG_STATUS);
        printf(L"IA32_MCG_STATUS=%lx\r\n", mcg_status);
        uint64_t mc_banks = rdmsr(MSR_IA32_MCG_CAP) & MCG_CAP_REPORTING_BANKS_COUNT_MASK;
        for (uint64_t i = 0; i < mc_banks; i++)
        {
            uint64_t mc_status = rdmsr(MSR_IA32_MC0_STATUS + i * 4);
            printf(L"MC Status bank %lx contents: %lx\r\n", i, mc_status);
            wrmsr(MSR_IA32_MC0_STATUS + i * 4, 0);
            if (mc_status & MC_STATUS_VALID)
            {
                extra_info = mc_status;
            }
        }
        wrmsr(MSR_IA32_MCG_STATUS, mcg_status & ~MCG_STATUS_MCIP);
        break;
    }

    if (instruction_type != NOT_INTERESTING)
    {
        switch (instruction_type)
        {
        case NOT_INTERESTING:
            break;
        case KERNEL_EXCEPTION:
            kernel_exceptions++;
            break;
        case KERNEL_PAGE_FAULT:
            kernel_page_faults++;
            break;
        case MACHINE_CHECK:
            machine_checks++;
            break;
        case UNDOCUMENTED_UD:
            undocumented_behind_ud++;
            break;
        case UNDOCUMENTED_NOT_UD:
            undocumented_not_ud++;
            break;
        case VEX_MALFORMED_BUT_ACCEPTED:
            vex_malformed_but_accepted++;
            break;
        case NOP_WITH_SIDE_EFFECTS:
            nops_with_side_effects++;
            break;
        case XED_LENGTH_MISMATCH:
            cpu_xed_length_mismatches++;
            break;

        }

        if (save_instruction_data(context,
                                  instruction_type,
                                  instruction_bytes,
                                  cur_instruction_length,
                                  extra_info_present,
                                  extra_info,
                                  cur_perf_counters_values,
                                  PERF_EVENTS_COUNT) != EFI_SUCCESS)
        {
            print(L"Saving instruction info failed.\r\n");

            dump_stats(context,
                       instruction_bytes,
                       cur_instruction_length,
                       extra_info_present,
                       extra_info,
                       cur_perf_counters_values,
                       disasm_length);

            print(L"Closing save file\r\n");
            close_save_file();
            halt();
        }
        flush_required = true;
        instructions_saved++;
    }

    unique_instructions_executed++;
    if (unique_instructions_executed % 0x1000000 == 0)
    {
        dump_stats(context,
                   instruction_bytes,
                   cur_instruction_length,
                   extra_info_present,
                   extra_info,
                   cur_perf_counters_values,
                   disasm_length);

        if (flush_required)
        {
            print(L"Flushing save file\r\n");
            flush_save_file();
            flush_required = false;
        }
        print(L"\r\n");
    }

    bool cur_byte_is_prefix = cur_byte_index < count_prefixes(instruction_bytes, cur_instruction_length);
    if (cur_instruction_length == last_instruction_length && !cur_byte_is_prefix)
    {
update_byte_index:
        while (instruction_bytes[cur_byte_index] == 0xff)
        {
            instruction_bytes[cur_byte_index] = 0;
            if (cur_byte_index == 0)
            {
                printf(L"Done, 0x%lx instructions saved (0x%lx bytes), closing file\r\n", instructions_saved, get_save_file_position());
                close_save_file();
                halt();
            }

            cur_byte_index--;
        }
        // will be incremented during page fault handling if needed
        cur_instruction_length = cur_byte_index + 1;
    }
    else
    {
        cur_byte_index = cur_instruction_length - 1;
        last_instruction_length = cur_instruction_length;
    }

    do
    {
        if (__builtin_expect(instruction_bytes[cur_byte_index] == 0xff, 0))
        {
            goto update_byte_index;
        }
        instruction_bytes[cur_byte_index]++;
    } while(contains_invalid_count_of_prefixes(instruction_bytes, cur_instruction_length));
}

static inline uint64_t __attribute__((always_inline)) rdpmc(uint32_t index)
{
    uint32_t low = 0, high = 0;
    __asm__ ("rdpmc":"=a"(low),"=d"(high):"c"(index));
    uint64_t value = low | (uint64_t)high << 32;
    return value;
}

void handle_exception(struct context* context)
{
    uint64_t cur_perf_counters_values[PERF_EVENTS_COUNT] = {0};
    for (size_t i = 0; i < PERF_EVENTS_COUNT; i++)
    {
        cur_perf_counters_values[i] = rdpmc(i);
    }

    if (__builtin_expect(measuring_ud, 0))
    {
        check_ud2_measurements(context, cur_perf_counters_values);

#ifdef COUNT_NOPS
        measuring_nop = true;
        execute_nop();
    }
    else if (__builtin_expect(measuring_nop, 0))
    {
        check_nop_measurements(context, cur_perf_counters_values);
#endif
    }
    else
    {
        check_last_instruction(context, cur_perf_counters_values);
    }

    execute_current_instruction();
}
