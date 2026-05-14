#ifndef _EXCEPTION_CONTEXT_H_
#define _EXCEPTION_CONTEXT_H_

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

#define EXCEPTIONS_WITH_ERROR_CODE_MASK ((1ULL<<8) | (1ULL<<10) | (1ULL<<11) | (1ULL<<12) | (1ULL<<13) | (1ULL<<14) | (1ULL<<17) | (1ULL<<21))
#endif
