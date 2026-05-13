#include "msr.h"
#include "print.h"
#include <stdbool.h>
#include <stdint.h>

#define MSR_IA32_PMC0 0xc1

#define MSR_IA32_PERFEVTSEL0 0x186
#define PERFEVTSEL_ENABLE (1ULL<<22)
#define PERFEVTSEL_OS (1ULL<<17)
#define PERFEVTSEL_USER (1ULL<<16)

#define UOPS_ISSUED_ANY 0x010e

uint8_t* user_code_page = NULL;
void __attribute__((noreturn)) enter_user(void* rip, void* rsp);
void execute_instruction(const uint8_t* instruction, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        user_code_page[0x1000 - size + i] = instruction[i];
    }

    wrmsr(MSR_IA32_PERFEVTSEL0, 0);
    wrmsr(MSR_IA32_PMC0, 0);
    wrmsr(MSR_IA32_PERFEVTSEL0, PERFEVTSEL_USER | PERFEVTSEL_ENABLE | UOPS_ISSUED_ANY);

    __asm__("clflush [%0]"::"r"(user_code_page+0x1000-size));
    enter_user(user_code_page + 0x1000 - size, NULL);
}

uint8_t instruction_bytes[20] = {0};
size_t cur_instruction_length = 1;
size_t cur_byte_index = 0;

void execute_current_instruction(void)
{
    execute_instruction(instruction_bytes, cur_instruction_length);
}

// Exception handling

struct iretq_frame
{
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

struct context
{
#ifdef SAVE_REGS_ON_EXCEPTION
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
#endif
    uint64_t exception_number;
    union
    {
        struct iretq_frame iretq_frame_no_error_code;
        struct
        {
            uint64_t error_code;
            struct iretq_frame iretq_frame_with_error_code;
        };
    };
};

#define EXCEPTION_INVALID_OPCODE 6
#define EXCEPTION_PAGE_FAULT 14
#define PF_INSTRUCTION_FETCH (1ULL<<4)
static const uint32_t exceptions_with_error_code_mask = 
    (1ULL<<8) | (1ULL<<10) | (1ULL<<11) | (1ULL<<12) | (1ULL<<13) | (1ULL<<14) | (1ULL<<17) | (1ULL<<21);

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

void execute_ud(void)
{
    uint8_t ud2[] = { 0x0f, 0x0b };
    execute_instruction(ud2, sizeof(ud2));
}

bool measuring_ud_uops_issued = true;
uint64_t ud_uops_issued_any = (uint64_t)-1;
uint64_t last_uops_issued_any = (uint64_t)-1;
void handle_exception(struct context* context)
{
    __asm__("push 0\n"
            "popfq\n":::"memory","flags");

    bool has_error_code = ((1ULL << context->exception_number) & exceptions_with_error_code_mask) != 0;
    struct iretq_frame* frame = has_error_code ? &context->iretq_frame_with_error_code : &context->iretq_frame_no_error_code;

    wrmsr(MSR_IA32_PERFEVTSEL0, 0);
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
            while(1);
        }

        uint64_t current_ud_uops_issued_any = rdmsr(MSR_IA32_PMC0);

        if (ud_uops_issued_any != current_ud_uops_issued_any)
        {
            ud_uops_issued_any = current_ud_uops_issued_any;
            execute_ud();
        }

        measuring_ud_uops_issued = false;

        printf(L"UD issued micro-ops count: 0x%lx\r\n", ud_uops_issued_any);
        execute_current_instruction();
    }
    

    if (context->exception_number == EXCEPTION_PAGE_FAULT && (context->error_code & PF_INSTRUCTION_FETCH) && (frame->cs & 3) == 3)
    {
        uint64_t cr2;
        __asm__("mov %0, cr2":"=r"(cr2));

        if (cr2 == (size_t)user_code_page + 0x1000 && frame->rip != cr2)
        {
            increment_instruction_length_and_retry_exec();
        }
    }

    if (context->exception_number == EXCEPTION_INVALID_OPCODE)
    {
        uint64_t uops_issued_any = rdmsr(MSR_IA32_PMC0);

        if (last_uops_issued_any != uops_issued_any)
        {
            last_uops_issued_any = uops_issued_any;
            execute_current_instruction();
        }

        last_uops_issued_any = (uint64_t)-1;

        if (uops_issued_any != ud_uops_issued_any)
        {
            print(L"#UD -");
            for (size_t i = 0; i < cur_instruction_length; i++)
            {
                printf(L" %hx", instruction_bytes[i]);
            }

            printf(L" - microcoded precondition failed (UOPS_ISSUED.ANY = 0x%lx)\r\n", uops_issued_any);
        }
    }

    if (cur_instruction_length == last_instruction_length)
    {
        while (instruction_bytes[cur_byte_index] == 0xff)
        {
            instruction_bytes[cur_byte_index] = 0;
            if (cur_byte_index == 0)
            {
                print(L"Done\r\n");
                while(1);
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


    instruction_bytes[cur_byte_index]++;

    execute_current_instruction();
}
