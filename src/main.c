#include "control_registers.h"
#include <efi.h>
#include "msr.h"
#include "print.h"
#include "system_tables_setup.h"

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



#define MSR_IA32_EFER 0xc0000080
#define EFER_NXE (1ULL<<11)
void load_segments(uint16_t code_segment, uint16_t stack_segment, uint16_t task_segment);
extern uint8_t* user_code_page;

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

    set_tss_rsp0(kernel_stack_pages+0x1000*KERNEL_STACK_PAGES);

    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&user_code_page);
    if (status != EFI_SUCCESS)
    {
        printf(L"Could not allocate a page for user code, status = 0x%x\r\n", (uint64_t)status);
        SystemTable->BootServices->FreePages(kernel_stack_pages, 2);
        return status;
    }

    SystemTable->BootServices->SetMem(user_code_page, 0x1000, 0);

    __asm__("cli");
    write_cr8(0xf);

    printf(L"User code page @ 0x%x\r\n", user_code_page);

    setup_gdt();
    load_segments(0x8, 0x10, 0x28);

    setup_idt();

    wrmsr(MSR_IA32_EFER, rdmsr(MSR_IA32_EFER) | EFER_NXE);

    __asm__("ud2");

    while(1);
}
