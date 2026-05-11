#include "print.h"
#include <stdbool.h>
#include <stdint.h>

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

#define EXCEPTION_PAGE_FAULT 14
#define PF_INSTRUCTION_FETCH (1ULL<<4)
static const uint32_t exceptions_with_error_code_mask = 
    (1ULL<<8) | (1ULL<<10) | (1ULL<<11) | (1ULL<<12) | (1ULL<<13) | (1ULL<<14) | (1ULL<<17) | (1ULL<<21);

//extern uint8_t* user_code_page;
//extern size_t current_instruction_length;

void __attribute__((noreturn)) execute_instruction(void);


void handle_exception(struct context* context)
{
    __asm__("push 0\n"
            "popfq\n":::"memory","flags");

    bool has_error_code = ((1ULL << context->exception_number) & exceptions_with_error_code_mask) != 0;
    struct iretq_frame* frame = has_error_code ? &context->iretq_frame_with_error_code : &context->iretq_frame_no_error_code;
/*
    if (context->exception_number == EXCEPTION_PAGE_FAULT && (context->error_code & PF_INSTRUCTION_FETCH) && (frame->cs & 3) == 3)
    {
        uint64_t cr2;
        __asm__("mov %0, cr2":"=r"(cr2));

        if (cr2 == (size_t)user_code_page + 0x1000)
        {
            current_instruction_length++;
            //return execute_instruction();
        }
    }
*/
    printf(L"Exception %x\r\n", context->exception_number);
    if (has_error_code)
    {
        printf(L"Error code: %x\r\n", context->error_code);
    }

    printf(L"CS:RIP = %x:%x RFLAGS=%x\r\n", frame->cs, frame->rip, frame->rflags);
    printf(L"SS:RSP = %x:%x\r\n", frame->ss, frame->rsp);

    while (1);
    //return execute_instruction();
}
