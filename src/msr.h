#ifndef _MSR_H_
#define _MSR_H_

static inline uint64_t __attribute__((always_inline)) rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__("rdmsr":"=d"(high),"=a"(low):"c"(msr));

    return ((uint64_t)high << 32) | low;
}

static inline void __attribute__((always_inline)) wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xffFFffFFULL;
    uint32_t high = value >> 32;
    __asm__("wrmsr"::"c"(msr),"d"(high),"a"(low));
}

#endif
