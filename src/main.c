#include "control_registers.h"
#include <efi.h>
#include "msr.h"
#include "print.h"
#include <stdbool.h>
#include <stdint.h>
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


static inline void __attribute__((noreturn)) halt(void)
{
    while(1)
    {
        __asm__("cli");
        __asm__("hlt");
    }
}


#define MSR_IA32_EFER 0xc0000080
#define EFER_NXE (1ULL<<11)
void load_segments(uint16_t code_segment, uint16_t stack_segment, uint16_t task_segment);
extern uint8_t* user_code_page;

#define KERNEL_STACK_PAGES 2

#define PAGE_PRESENT (1ULL<<0)
#define PAGE_WRITABLE (1ULL<<1)
#define PAGE_USER (1ULL<<2)
#define PAGE_ACCESSED (1ULL<<5)
#define PAGE_DIRTY (1ULL<<6)
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

    printf(L"User code page @ 0x%x, remapping as user-accessible\r\n", user_code_page);

    uint64_t old_cr0 = read_cr0();
    write_cr0(old_cr0 & ~CR0_WP);


    uint64_t* cur_page_table = (uint64_t*)(read_cr3() & ~0xfff);
    // Page level values:
    // 0 - PML5
    // 1 - PML4
    // 2 - PDPT
    // 3 - PD
    // 4 - PT
    unsigned int page_level = (read_cr4() & CR4_LA57) ? 0 : 1;
    unsigned int shift_amount = 0;
    unsigned int page_table_index = 0;
    uint64_t page_table_entry = 0;


    bool encountered_page_size_bit = false;
    for (; page_level < 5; page_level++)
    {
        shift_amount = 48 - 9*page_level;
        page_table_index = ((size_t)user_code_page >> shift_amount) & 0x1ffULL;

        page_table_entry = cur_page_table[page_table_index];

        printf(L"0x%x[0x%x] -> 0x%x\r\n", cur_page_table, (uint64_t)page_table_index, page_table_entry);

        if (page_table_entry & PAGE_SIZE)
        {
            // Special handling is required in this case
            encountered_page_size_bit = true;
            break;
        }

        page_table_entry |= PAGE_USER;
        page_table_entry &= ~PAGE_NOEXECUTE;

        cur_page_table[page_table_index] = page_table_entry;

        __asm__("invlpg [%0]"::"r"(page_table_entry & ~0xfffULL));

        cur_page_table = (uint64_t*)(page_table_entry & ~0xfffULL);
    }

    if (encountered_page_size_bit)
    {
        print(L"Encountered page size bit, allocating new page tables\r\n");

        // PD (page_level == 3): one new page table
        // PDPT (page_level == 2): one new page directory and a new page table for it (2 tables)
        unsigned int page_tables_count = 4 - page_level;

        const CHAR16* page_level_table_names[] = {
            L"PML5", L"PML4", L"PDPT", L"PD", L"PT"
        };
        if (page_level != 2 && page_level != 3)
        {
            printf(L"BUG: Somehow got here while processing %sE!\r\n", page_level_table_names[page_level]);
            halt();
        }

        uint64_t* new_page_tables = NULL;
        status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, page_tables_count, (EFI_PHYSICAL_ADDRESS*)&new_page_tables);

        if (status != EFI_SUCCESS)
        {
            printf(L"Could not allocate pages for new page tables, status = 0x%x\r\n", (uint64_t)status);
            halt();
        }

        printf(L"New 0x%x page tables are at 0x%x\r\n", (uint64_t)page_tables_count, (uint64_t)new_page_tables);

        // new_page_tables is a pointer to the NEXT layer of page tables
        page_level++;

        uint64_t* cur_new_page_table = new_page_tables;

        uint64_t firmware_page_table_flags = page_table_entry & ((0xfff ^ (PAGE_SIZE | PAGE_DIRTY | PAGE_ACCESSED)) | PAGE_NOEXECUTE);
        for (; page_level < 5; page_level++)
        {
            shift_amount = 48 - 9*page_level;

            unsigned int new_page_table_index = ((size_t)user_code_page >> shift_amount) & 0x1ffULL;

            uint64_t start_phys_addr = ((size_t)user_code_page >> (shift_amount+9)) << (shift_amount+9);

#ifdef _TRACE_PAGE_TABLE_ALLOC
            printf(L"===[ Page level 0x%x (%s) ]===\r\n", (uint64_t)page_level, page_level_table_names[page_level]);
            printf(L"Starting from phys addr 0x%x\r\n", start_phys_addr);
#endif
            for (unsigned int i = 0; i < 0x1000/sizeof(uint64_t); i++)
            {
                uint64_t new_page_table_entry = (start_phys_addr + (i << shift_amount));

                if (i == new_page_table_index)
                {
                    if (page_level != 4) // Not a PTE
                    {
                        new_page_table_entry = (size_t)cur_new_page_table + 0x1000;
                    }
                    new_page_table_entry |= PAGE_USER;
                }
                else
                {
                    new_page_table_entry |= firmware_page_table_flags;
                    if (page_level != 4) // Not a PTE
                    {
                        new_page_table_entry |= PAGE_SIZE;
                    }
                }

                new_page_table_entry |= PAGE_PRESENT;
#ifdef _TRACE_PAGE_TABLE_ALLOC
                printf(L"0x%x[0x%x] -> 0x%x", cur_new_page_table, (uint64_t)i, new_page_table_entry);
                if (i == new_page_table_index)
                {
                    print(L"!!!");
                }
                print(L"\r\n");
#endif
                cur_new_page_table[i] = new_page_table_entry;
            }

            cur_new_page_table = (uint64_t*)((size_t)cur_new_page_table + 0x1000);
        }

        cur_page_table[page_table_index] = (size_t)new_page_tables | PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT;        
    }

    write_cr3(read_cr3());
    write_cr0(old_cr0);

    setup_gdt();
    load_segments(0x8, 0x10, 0x28);
    setup_idt();

    wrmsr(MSR_IA32_EFER, rdmsr(MSR_IA32_EFER) | EFER_NXE);


    __asm__("ud2");

    halt();
    __builtin_unreachable();
}
