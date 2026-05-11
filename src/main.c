#include <efi.h>
#include "print.h"
#include "msr.h"

// Interesting Skylake events
// From https://github.com/intel/perfmon/blob/main/SKL/events/skylake_core.json
// Format: bits 15-8: UMASK, bits 7-0: EVENT_CODE
#define UOPS_EXECUTED_THREAD 0x0181
#define UOPS_EXECUTED_CORE 0x02b1
#define UOPS_EXECUTED_X87 0x10b1
#define UOPS_ISSUED_ANY 0x010e
#define UOPS_ISSUED_VECTOR_WIDTH_MISMATCH 0x020e
#define UOPS_ISSUED_SLOW_LEA 0x200e

#define UOPS_DISPATCHED_PORT_0 0x01a1
#define UOPS_DISPATCHED_PORT_1 0x02a1
#define UOPS_DISPATCHED_PORT_2 0x04a1
/// and so on... umask = (1<<port_num)

#define UOPS_RETIRED_RETIRE_SLOTS 0x02c2
#define UOPS_RETIRED_MACRO_FUSED 0x04c2

#define IDQ_MS_UOPS 0x3079
#define IDQ_DBS_UOPS 0x0879
#define IDQ_MITE_UOPS 0x0479

#define OTHER_ASSISTS_ANY 0x3fc1
#define FP_ASSIST_ANY 0x1eca

#define DSB2MITE_SWITCHES_COUNT 0x01ab

#define INST_DECODED_DECODERS 0x0155

#define INST_RETIRED_ANY_P 0x00c0
#define INST_RETIRED_ANY 0x0100
#define INST_RETIRED_NOP 0x02c0

#define BR_INST_RETIRED_NOT_TAKEN 0x10c4

#define MEM_INST_RETIRED_ALL_LOADS 0x81d0
#define MEM_INST_RETIRED_ALL_STORES 0x82d0
#define MEM_INST_RETIRED_ANY 0x83d0

#define MEM_INST_RETIRED_SPLIT_LOADS 0x41d0
#define MEM_INST_RETIRED_SPLIT_STORES 0x42d0

#define MSR_IA32_PMC0 0xc1

#define MSR_IA32_PERFEVTSEL0 0x186
#define PERFEVTSEL_ENABLE (1ULL<<22)
#define PERFEVTSEL_OS (1ULL<<17)
#define PERFEVTSEL_USER (1ULL<<16)

#define GDT_CODE_OR_DATA_SEGMENT (1ULL<<12)
#define GDT_DPL_3 (3ULL<<13)
#define GDT_PRESENT (1ULL<<15)
#define GDT_64BIT_CODE_SEGMENT ((1ULL<<21) | GDT_CODE_OR_DATA_SEGMENT | (11ULL<<8))
#define GDT_32BIT_SEGMENT (1ULL<<22)
#define GDT_GRANULARITY (1ULL<<23)
#define GDT_32BIT_DATA_SEGMENT (GDT_CODE_OR_DATA_SEGMENT | (3ULL<<8))
#define GDT_TSS_AVAILABLE (9ULL<<8)

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


struct tss tss = { .io_map_base_address = sizeof(tss) - 1 };

uint32_t gdt[][2] = {
    { 0, 0 },
    { 0xffff, (0xfULL << 16) | GDT_PRESENT | GDT_64BIT_CODE_SEGMENT | GDT_GRANULARITY },
    { 0xffff, (0xfULL << 16) | GDT_PRESENT | GDT_32BIT_DATA_SEGMENT | GDT_GRANULARITY },
    { 0xffff, (0xfULL << 16) | GDT_PRESENT | GDT_64BIT_CODE_SEGMENT | GDT_GRANULARITY | GDT_DPL_3 },
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

void (*exception_handlers[])(void) = {
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

struct idt_entry idt[sizeof(exception_handlers)/sizeof(exception_handlers[0])] = {0};
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

#define MSR_IA32_EFER 0xc0000080
#define EFER_NXE (1ULL<<11)
void load_segments(uint16_t code_segment, uint16_t stack_segment, uint16_t task_segment);
extern uint8_t* user_code_page;

#define CR0_WP (1ULL<<16)

#define CR4_LA57 (1ULL<<12)
#define KERNEL_STACK_PAGES 2

#define PAGE_PRESENT (1ULL<<1)
#define PAGE_USER (1ULL<<2)
#define PAGE_SIZE (1ULL<<7)
#define PAGE_NOEXECUTE (1ULL<<63)
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
    (void)ImageHandle;

    print_init(SystemTable);

    EFI_PHYSICAL_ADDRESS kernel_stack_pages = 0;
    EFI_STATUS status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, KERNEL_STACK_PAGES, &kernel_stack_pages);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not allocate memory for kernel stack, status = 0x%x\r\n", (uint64_t)status);
        return status;
    }
    tss.rsp0 = kernel_stack_pages+0x1000*KERNEL_STACK_PAGES;

    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&user_code_page);
    if (status != EFI_SUCCESS)
    {
        printf(L"Could not allocate a page for user code, status = 0x%x\r\n", (uint64_t)status);
        SystemTable->BootServices->FreePages(kernel_stack_pages, 2);
        return status;
    }

    SystemTable->BootServices->SetMem(user_code_page, 0x1000, 0);


    __asm__("cli");
    __asm__("mov cr8, %0"::"r"(0xfULL):"flags");

    printf(L"User code page @ 0x%x\r\n", user_code_page);


    setup_gdt();
    load_segments(0x8, 0x10, 0x28);

    setup_idt();

    wrmsr(MSR_IA32_EFER, rdmsr(MSR_IA32_EFER) | EFER_NXE);



    __asm__("ud2");

    while(1);
}
