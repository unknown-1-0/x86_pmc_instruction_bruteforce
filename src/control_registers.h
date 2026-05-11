#ifndef _CONTROL_REGISTERS_H_
#define _CONTROL_REGISTERS_H_

#include <stdint.h>

#define CR0_WP (1ULL<<16)

#define CR4_LA57 (1ULL<<12)

static inline __attribute__((always_inline)) uint64_t read_cr0(void)
{
    uint64_t value = 0;
    __asm__("mov %0, cr0":"=r"(value)::"flags");
    return value;
}

static inline __attribute__((always_inline)) void write_cr0(uint64_t value)
{
    __asm__("mov cr0, %0"::"r"(value):"flags");
}

static inline __attribute__((always_inline)) uint64_t read_cr2(void)
{
    uint64_t value = 0;
    __asm__("mov %0, cr2":"=r"(value)::"flags");
    return value;
}

static inline __attribute__((always_inline)) void write_cr2(uint64_t value)
{
    __asm__("mov cr2, %0"::"r"(value):"flags");
}

static inline __attribute__((always_inline)) uint64_t read_cr3(void)
{
    uint64_t value = 0;
    __asm__("mov %0, cr3":"=r"(value)::"flags");
    return value;
}

static inline __attribute__((always_inline)) void write_cr3(uint64_t value)
{
    __asm__("mov cr3, %0"::"r"(value):"flags");
}

static inline __attribute__((always_inline)) uint64_t read_cr4(void)
{
    uint64_t value = 0;
    __asm__("mov %0, cr4":"=r"(value)::"flags");
    return value;
}

static inline __attribute__((always_inline)) void write_cr4(uint64_t value)
{
    __asm__("mov cr4, %0"::"r"(value):"flags");
}

static inline __attribute__((always_inline)) uint64_t read_cr8(void)
{
    uint64_t value = 0;
    __asm__("mov %0, cr8":"=r"(value)::"flags");
    return value;
}

static inline __attribute__((always_inline)) void write_cr8(uint64_t value)
{
    __asm__("mov cr8, %0"::"r"(value):"flags");
}

#endif
