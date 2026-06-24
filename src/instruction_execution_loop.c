#include "control_registers.h"
#include "disasm.h"
#include "exception_context.h"
#include "msr.h"
#include "print.h"
#include "save_file.h"
#include <stdbool.h>
#include <stdint.h>
#include <xed-decode.h>
#include <xed-decoded-inst-api.h>


static inline void __attribute__((noreturn)) halt(void)
{
    while(1)
    {
        __asm__("cli");
        __asm__("hlt");
    }
}

#define MSR_IA32_PMC0 0xc1
#define MSR_IA32_PMC1 0xc2

#define MSR_IA32_PERFEVTSEL0 0x186
#define MSR_IA32_PERFEVTSEL1 0x187
#define PERFEVTSEL_ENABLE (1ULL<<22)
#define PERFEVTSEL_OS (1ULL<<17)
#define PERFEVTSEL_USER (1ULL<<16)

#define SKYLAKE 1
#define ALDER_LAKE 2

#if TARGET==SKYLAKE
#define UOPS_ISSUED_ANY 0x010e
#elif TARGET==ALDER_LAKE
#define UOPS_ISSUED_ANY 0x01ae
#else
#error Unknown target CPU microarchitecture
#endif

#undef SKYLAKE
#undef ALDER_LAKE

#ifdef COUNT_NOPS
#define INST_RETIRED_NOP 0x02c0
#endif

#define MSR_IA32_FIXED_CTR_CTRL 0x38d
#define MSR_IA32_PERF_GLOBAL_CTRL 0x38f
uint8_t* user_code_page = NULL;
void __attribute__((noreturn)) enter_user(void* rip, void* rsp);
void execute_instruction(const uint8_t* instruction, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        user_code_page[0x1000 - size + i] = instruction[i];
    }

    wrmsr(MSR_IA32_FIXED_CTR_CTRL, 0);
    wrmsr(MSR_IA32_PERF_GLOBAL_CTRL,
            (1ULL<<0)
#ifdef COUNT_NOPS
            |(1ULL<<1)
#endif
         );
    wrmsr(MSR_IA32_PERFEVTSEL0, 0);
    wrmsr(MSR_IA32_PMC0, 0);
    wrmsr(MSR_IA32_PERFEVTSEL0, PERFEVTSEL_USER | PERFEVTSEL_ENABLE | UOPS_ISSUED_ANY);
#ifdef COUNT_NOPS
    wrmsr(MSR_IA32_PERFEVTSEL1, 0);
    wrmsr(MSR_IA32_PMC1, 0);
    wrmsr(MSR_IA32_PERFEVTSEL1, PERFEVTSEL_USER | PERFEVTSEL_ENABLE | INST_RETIRED_NOP);
#endif

    __asm__("clflush [%0]"::"r"(user_code_page+0x1000-size));
    enter_user(user_code_page + 0x1000 - size, NULL);
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
static size_t hidden_behind_ud_instructions = 0;
static size_t undocumented_instructions = 0;
static size_t malformed_but_valid_instructions = 0;
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
                uint64_t extra_info,
                uint64_t uops_issued_any,
                uint64_t xed_length)
{
    printf(L"Unique instructions executed: 0x%lx, saved: 0x%lx\r\n", unique_instructions_executed, instructions_saved);
    printf(L"Current save file size: 0x%lx bytes\r\n", get_save_file_position());
    printf(L"Kernel: page faults: 0x%lx, exceptions: 0x%lx\r\n", kernel_page_faults, kernel_exceptions);
    printf(L"Undocumented instructions: hidden behind #UD: 0x%lx, not hidden: 0x%lx\r\n", hidden_behind_ud_instructions, undocumented_instructions);
#ifdef COUNT_NOPS
    printf(L"Malformed but valid instructions: 0x%lx, NOPs with side effects: 0x%lx\r\n", malformed_but_valid_instructions, nops_with_side_effects);
#else
    printf(L"Malformed but valid instructions: 0x%lx\r\n", malformed_but_valid_instructions);
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

    printf(L" (extra info: 0x%lx, UOPS_ISSUED.ANY = 0x%lx, Intel XED length: 0x%lx)\r\n", extra_info, uops_issued_any, xed_length);
}

static bool is_prefix(uint8_t byte)
{
    // Segment override prefixes
    if ((byte & ~(0x26U ^ 0x3eU)) == 0x26U)
    {
        return true;
    }
#if MODE == 64
    // REX
    if ((byte & 0xf0U) == 0x40)
    {
        return true;
    }
#endif
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
        if (is_prefix(byte))
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
            // VEX prefix contains various fields in the next 1-2 bytes,
            // and, if it's present, the opcode bytes are located immediately after it

            // Intel SDM says that instructions with 0x66, 0xf2, 0xf3, LOCK and REX prefixes preceding VEX will #UD.
            // However, VEX prefix can only encode 0x66, 0xf2, 0xf3 prefixes (+ REX, in 3-byte form)
            // For this reason, keep LOCK prefix (and REX, if using 2-byte form of VEX) just in case some instruction hide there
            vex_invalid = (byte == 0xc4 && rex_seen) || operand_size_override_seen || rep_seen || repne_seen;
            vex_malformed_instruction_expect_ud = (!vex_invalid && ((byte == 0xc5 && rex_seen) || lock_seen));
            vex_seen = true;
            break;
        default:
#if MODE == 64
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

        if (vex_seen)
        {
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
#ifdef COUNT_NOPS
void execute_nop(void)
{
    uint8_t nop[] = { 0x90 };
    execute_instruction(nop, sizeof(nop));
}

static bool measuring_nop_uops_issued = false;
static uint64_t nop_uops_issued_any = (uint64_t)-1;
#endif

static bool measuring_ud_uops_issued = true;
static uint64_t ud_uops_issued_any = (uint64_t)-1;
static uint64_t last_uops_issued_any = (uint64_t)-1;

bool flush_required = false;

#define MSR_IA32_MCG_CAP 0x179
#define MCG_CAP_REPORTING_BANKS_COUNT_MASK 0xfULL
void handle_exception(struct context* context)
{
    bool has_error_code = ((1ULL << context->exception_number) & EXCEPTIONS_WITH_ERROR_CODE_MASK) != 0;
    struct iretq_frame* frame = has_error_code ? &context->iretq_frame_with_error_code : &context->iretq_frame_no_error_code;

    wrmsr(MSR_IA32_PERFEVTSEL0, 0);
    wrmsr(MSR_IA32_PERFEVTSEL1, 0);
    if (measuring_ud_uops_issued)
    {
        if (context->exception_number != EXCEPTION_INVALID_OPCODE)
        {
            printf(L"Unexpected exception %lx while measuring micro-ops count of a known invalid opcode\r\n", context->exception_number);
            if (has_error_code)
            {
                printf(L"Error code: %lx\r\n", context->error_code);
            }

            printf(L"CS:RIP = %lx:%lx RFLAGS=%lx\r\n", frame->cs, frame->rip, frame->rflags);
            printf(L"SS:RSP = %lx:%lx\r\n", frame->ss, frame->rsp);
            halt();
        }

        uint64_t current_ud_uops_issued_any = rdmsr(MSR_IA32_PMC0);

        if (ud_uops_issued_any != current_ud_uops_issued_any)
        {
            ud_uops_issued_any = current_ud_uops_issued_any;
            execute_ud();
        }

        measuring_ud_uops_issued = false;
        printf(L"UD2 issued micro-ops count: 0x%lx\r\n", ud_uops_issued_any);

        EFI_STATUS status = save_data(&ud_uops_issued_any, sizeof(ud_uops_issued_any));

        if (status != EFI_SUCCESS)
        {
            printf(L"Could not save UD issued micro-ops count, status = 0x%lx\r\n", status);
            print(L"Closing save file\r\n");
            close_save_file();
            print(L"Halting CPU\r\n");
            halt();
        }

#ifdef COUNT_NOPS
        measuring_nop_uops_issued = true;
        execute_nop();
    }

    if (measuring_nop_uops_issued)
    {
        if (context->exception_number != EXCEPTION_DEBUG)
        {
            printf(L"Unexpected exception %lx while measuring micro-ops count of a known NOP\r\n", context->exception_number);
            if (has_error_code)
            {
                printf(L"Error code: %lx\r\n", context->error_code);
            }

            printf(L"CS:RIP = %lx:%lx RFLAGS=%lx\r\n", frame->cs, frame->rip, frame->rflags);
            printf(L"SS:RSP = %lx:%lx\r\n", frame->ss, frame->rsp);
            halt();
        }

        uint64_t nops_retired = rdmsr(MSR_IA32_PMC1);

        if (nops_retired != 1)
        {
            printf(L"Unexpected count of NOPs retired: expected: 1, got: 0x%lx\r\n", nops_retired);
            halt();
        }

        uint64_t current_nop_uops_issued_any = rdmsr(MSR_IA32_PMC0);


        if (nop_uops_issued_any != current_nop_uops_issued_any)
        {
            nop_uops_issued_any = current_nop_uops_issued_any;
            execute_nop();
        }

        measuring_nop_uops_issued = false;
        printf(L"NOP issued micro-ops count: 0x%lx\r\n", nop_uops_issued_any);

        EFI_STATUS status = save_data(&nop_uops_issued_any, sizeof(nop_uops_issued_any));

        if (status != EFI_SUCCESS)
        {
            printf(L"Could not save NOP issued micro-ops count, status = 0x%lx\r\n", status);
            print(L"Closing save file\r\n");
            close_save_file();
            print(L"Halting CPU\r\n");
            halt();
        }
#endif
        printf(L"UOPS_ISSUED.ANY PMC UMask:EventCode = 0x%lx\r\n", UOPS_ISSUED_ANY);
#if MODE == 64
        print(L"Starting bruteforce in 64-bit userspace\r\n");
#elif MODE == 32
        print(L"Starting bruteforce in 32-bit userspace\r\n");
#elif MODE == 16
        print(L"Starting bruteforce in 16-bit userspace\r\n");
#else
#error Unknown CPU mode
#endif
        execute_current_instruction();
    }
    bool is_interesting_instruction = false;

    if (__builtin_expect((frame->cs & 3) == 3, 1))
    {
        if (__builtin_expect(context->exception_number == EXCEPTION_PAGE_FAULT && (context->error_code & PF_INSTRUCTION_FETCH), 1))
        {
            uint64_t cr2 = read_cr2();

            if (__builtin_expect(cr2 == (size_t)user_code_page + 0x1000 && frame->rip != cr2, 1))
            {
                increment_instruction_length_and_retry_exec();
            }
        }
    }
    else
    {
        printf(L"Kernel Exception! CS:RIP=%lx:%lx SS:RSP=%lx:%lx RFLAGS=%lx\r\n", frame->cs, frame->rip, frame->ss, frame->rsp, frame->rflags);
        is_interesting_instruction = true;

        print(L"Instruction bytes:");

        for (size_t i = 0; i < 0x40; i++)
        {
            printf(L" %hx", ((uint8_t*)frame->rip)[i]);
        }

        print(L"\r\n");

        kernel_exceptions++;
    }


    if (__builtin_expect(malformed_instruction_expect_ud && context->exception_number != EXCEPTION_INVALID_OPCODE, 0))
    {
        is_interesting_instruction = true;
        malformed_but_valid_instructions++;
    }

    uint64_t uops_issued_any = rdmsr(MSR_IA32_PMC0);
#ifdef COUNT_NOPS
    uint64_t nops_retired = rdmsr(MSR_IA32_PMC1);
#endif
    uint64_t extra_info = uops_issued_any;

    uint64_t disasm_length = disasm_get_instruction_length(instruction_bytes, cur_instruction_length);
    bool instruction_is_known = (disasm_length != 0);
#ifdef COUNT_XED_VS_CPU_MISMATCHES
    if (instruction_is_known && cur_instruction_length != disasm_length)
    {
        is_interesting_instruction = true;
        cpu_xed_length_mismatches++;
    }
#endif
    switch (context->exception_number)
    {
    case EXCEPTION_PAGE_FAULT:
        if (!(context->error_code & PF_USER))
        {
            is_interesting_instruction = true;
            extra_info = read_cr2();
            kernel_page_faults++;
        }
        __attribute__((fallthrough));
    default:
        if (!instruction_is_known)
        {
            is_interesting_instruction = true;
            undocumented_instructions++;
        }
#ifdef COUNT_NOPS
        if (nops_retired)
        {
            if (context->exception_number != EXCEPTION_DEBUG)
            {
                is_interesting_instruction = true;
                nops_with_side_effects++;
            }
            else if (last_uops_issued_any != uops_issued_any)
            {
                last_uops_issued_any = uops_issued_any;
                execute_current_instruction();
            }
            else
            {
                last_uops_issued_any = (uint64_t)-1;
                if (uops_issued_any != nop_uops_issued_any)
                {
                    is_interesting_instruction = true;
                    nops_with_side_effects++;
                }
            }
        }
#endif
        break;
    case EXCEPTION_INVALID_OPCODE:
        {
            if (last_uops_issued_any != uops_issued_any)
            {
                last_uops_issued_any = uops_issued_any;
                execute_current_instruction();
            }

            last_uops_issued_any = (uint64_t)-1;

            if (uops_issued_any != ud_uops_issued_any && !instruction_is_known)
            {
                is_interesting_instruction = true;
                hidden_behind_ud_instructions++;
            }
        }
        break;
    case EXCEPTION_MACHINE_CHECK:
        is_interesting_instruction = true;
        machine_checks++;
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

    if (is_interesting_instruction)
    {
        if (save_instruction_data(context,
                                  instruction_bytes,
                                  cur_instruction_length,
                                  extra_info) != EFI_SUCCESS)
        {
            print(L"Saving instruction info failed.\r\n");

            dump_stats(context,
                       instruction_bytes,
                       cur_instruction_length,
                       extra_info,
                       uops_issued_any,
                       disasm_length);

            print(L"Closing save file\r\n");
            close_save_file();
            print(L"Halting CPU\r\n");
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
                   extra_info,
                   uops_issued_any,
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
            print(L"BUG: Softlock detected!\r\n");
            printf(L"Current: instruction length: 0x%lx, byte index: 0x%lx\r\n", cur_instruction_length, cur_byte_index);
            printf(L"Last instruction length: 0x%lx\r\n", last_instruction_length);
            print(L"Instruction bytes:");

            for (size_t i = 0; i < cur_instruction_length; i++)
            {
                printf(L" %hx", instruction_bytes[i]);
            }

            print(L"\r\nClosing save file\r\n");
            close_save_file();
            print(L"Halting CPU\r\n");
            halt();
        }
        instruction_bytes[cur_byte_index]++;
    } while(contains_invalid_count_of_prefixes(instruction_bytes, cur_instruction_length));

    execute_current_instruction();
}
