#include <stdint.h>
#include "system_tables_setup.h"

#define GDT_CODE_OR_DATA_SEGMENT (1ULL<<12)
#define GDT_DPL_3 (3ULL<<13)
#define GDT_PRESENT (1ULL<<15)
// 11 is execute/read, accessed code segment
#define GDT_16BIT_CODE_SEGMENT (GDT_CODE_OR_DATA_SEGMENT | (11ULL<<8))
#define GDT_64BIT_CODE_SEGMENT ((1ULL<<21) | GDT_CODE_OR_DATA_SEGMENT | (11ULL<<8))
#define GDT_32BIT_CODE_SEGMENT ((1ULL<<22) | GDT_CODE_OR_DATA_SEGMENT | (11ULL<<8))
#define GDT_32BIT_SEGMENT (1ULL<<22)
#define GDT_GRANULARITY (1ULL<<23)
#define GDT_32BIT_DATA_SEGMENT (GDT_CODE_OR_DATA_SEGMENT | (3ULL<<8))
#define GDT_TSS_AVAILABLE (9ULL<<8)

#if MODE == 64
#define GDT_USER_CODE_SEGMENT GDT_64BIT_CODE_SEGMENT
#elif MODE == 32
#define GDT_USER_CODE_SEGMENT GDT_32BIT_CODE_SEGMENT
#elif MODE == 16
#define GDT_USER_CODE_SEGMENT GDT_16BIT_CODE_SEGMENT
#else
#error Unknown CPU mode
#endif

#pragma pack(push, 1)
struct tss
{
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base_address;
};
#pragma pack(pop)

_Static_assert(sizeof(struct tss) == 104, "");


static struct tss tss = { .io_map_base_address = sizeof(tss) - 1 };

static uint32_t gdt[][2] = {
    { 0, 0 },
    { 0xffff, (0xfULL << 16) | GDT_PRESENT | GDT_64BIT_CODE_SEGMENT | GDT_GRANULARITY },
    { 0xffff, (0xfULL << 16) | GDT_PRESENT | GDT_32BIT_DATA_SEGMENT | GDT_GRANULARITY },
    { 0xffff, (0xfULL << 16) | GDT_PRESENT | GDT_USER_CODE_SEGMENT  | GDT_GRANULARITY | GDT_DPL_3 },
    { 0xffff, (0xfULL << 16) | GDT_PRESENT | GDT_32BIT_DATA_SEGMENT | GDT_GRANULARITY | GDT_DPL_3 },
    { 0, 0 },
    { 0, 0 }
};

void setup_gdt(void)
{
    size_t tss_base = (size_t)&tss;

    gdt[5][0] = ((sizeof(tss) - 1) & 0xffffULL) | ((tss_base & 0xffffULL) << 16);

    uint32_t gdt_entry1 = GDT_PRESENT | GDT_TSS_AVAILABLE;
    gdt_entry1 |= (tss_base >> 16) & 0xffULL;
    gdt_entry1 |= tss_base & 0xff000000ULL;

    gdt[5][1] = gdt_entry1;
    gdt[6][0] = tss_base >> 32;
    gdt[6][1] = 0;


    uint8_t gdtr[sizeof(size_t)+sizeof(uint16_t)];

    *(uint16_t*)gdtr = sizeof(gdt) - 1;
    *(size_t*)(gdtr+2) = (size_t)&gdt;

    __asm__ volatile("lgdt %0"::"m"(gdtr));
}

void set_tss_rsp0(uint64_t new_value)
{
    tss.rsp0 = new_value;
}

#define IDT_PRESENT (1ULL<<15)
#define IDT_DPL_3 (3ULL<<13)
#define IDT_TRAP_GATE (0xfULL << 8)
#pragma pack(push, 1)
struct idt_entry
{
    uint16_t offset_low;
    uint16_t segment_selector;
    uint16_t flags;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};
#pragma pack(pop)

_Static_assert(sizeof(struct idt_entry) == 0x10, "");

void handle_divide_error(void);
void handle_debug_exception(void);
void handle_nmi(void);
void handle_breakpoint(void);
void handle_overflow(void);
void handle_bound_range_exceeded(void);
void handle_invalid_opcode(void);
void handle_device_not_available(void);
void handle_double_fault(void);
void handle_reserved9(void);
void handle_invalid_tss(void);
void handle_segment_not_present(void);
void handle_stack_fault(void);
void handle_general_protection_fault(void);
void handle_page_fault(void);
void handle_reserved15(void);
void handle_x87_fpu_fp_error(void);
void handle_alignment_check(void);
void handle_machine_check(void);
void handle_simd_fp_exception(void);
void handle_virtualization_exception(void);
void handle_control_protection_exception(void);

static void (*exception_handlers[])(void) = {
    handle_divide_error,
    handle_debug_exception,
    handle_nmi,
    handle_breakpoint,
    handle_overflow,
    handle_bound_range_exceeded,
    handle_invalid_opcode,
    handle_device_not_available,
    handle_double_fault,
    handle_reserved9,
    handle_invalid_tss,
    handle_segment_not_present,
    handle_stack_fault,
    handle_general_protection_fault,
    handle_page_fault,
    handle_reserved15,
    handle_x87_fpu_fp_error,
    handle_alignment_check,
    handle_machine_check,
    handle_simd_fp_exception,
    handle_virtualization_exception,
    handle_control_protection_exception
};

static struct idt_entry idt[sizeof(exception_handlers)/sizeof(exception_handlers[0])] = {0};
void setup_idt(void)
{
    for (unsigned int i = 0; i < sizeof(exception_handlers)/sizeof(exception_handlers[0]); i++)
    {
        size_t ptr = (size_t)exception_handlers[i];

        idt[i].offset_low = ptr & 0xffffULL;
        idt[i].segment_selector = 0x8;

        uint16_t flags = IDT_PRESENT | IDT_TRAP_GATE;
        // #BP
        if (i == 3)
        {
            flags |= IDT_DPL_3;
        }

        idt[i].flags = flags;
        idt[i].offset_middle = (ptr >> 16) & 0xffffULL;
        idt[i].offset_high = ptr >> 32;
    }

    uint8_t idtr[sizeof(size_t)+sizeof(uint16_t)];

    *(uint16_t*)idtr = sizeof(idt) - 1;
    *(size_t*)(idtr+sizeof(uint16_t)) = (size_t)&idt;

    __asm__ volatile("lidt %0"::"m"(idtr));
}
