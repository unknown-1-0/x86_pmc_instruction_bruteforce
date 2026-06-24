#include <control_registers.h>
#include <efi.h>
#include <disasm.h>
#include <halt.h>
#include <msr.h>
#include "print.h"
#include <stdbool.h>
#include <stdint.h>
#include <string_init.h>
#include <system_tables_setup.h>

#define MSR_IA32_PMC0 0xc1

#define MSR_IA32_PERFEVTSEL0 0x186
#define PERFEVTSEL_ENABLE (1ULL<<22)
#define PERFEVTSEL_OS (1ULL<<17)
#define PERFEVTSEL_USER (1ULL<<16)

#define MSR_IA32_MCG_CTL 0x17b

#define MSR_IA32_EFER 0xc0000080
#define EFER_NXE (1ULL<<11)
void load_segments(uint16_t code_segment, uint16_t stack_segment, uint16_t task_segment);
extern uint8_t* user_code_page;
uint8_t* xsave_state_area_allocated_ptr = NULL;
uint8_t* xsave_state_area_ptr = NULL;

void init_perf_counters(void);
void execute_ud(void);
EFI_STATUS open_save_file(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable);
#define KERNEL_STACK_PAGES 2

#define PAGE_PRESENT (1ULL<<0)
#define PAGE_WRITABLE (1ULL<<1)
#define PAGE_USER (1ULL<<2)
#define PAGE_ACCESSED (1ULL<<5)
#define PAGE_DIRTY (1ULL<<6)
#define PAGE_SIZE (1ULL<<7)
#define PAGE_NOEXECUTE (1ULL<<63)

#define CR4_MCE (1ULL<<6)
#define CPUID_01_EDX_MCE (1ULL<<7)
#define CPUID_01_EDX_MCA (1ULL<<14)
#define MSR_IA32_MCG_CAP 0x179
#define MCG_CAP_REPORTING_BANKS_COUNT_MASK 0xfULL
#define MCG_CAP_MCG_CTL_P (1ULL<<8)
#define MSR_IA32_MC0_CTL 0x400
#define MSR_IA32_MC0_STATUS 0x401

#define CR4_OSFXSR (1ULL<<9)
#define CR4_OSXMMEXCPT (1ULL<<10)
#define CR4_OSXSAVE (1ULL<<18)
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
    print_init(SystemTable);
    string_init(SystemTable);
    disasm_init();

    EFI_PHYSICAL_ADDRESS kernel_stack_pages = 0;
    EFI_STATUS status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, KERNEL_STACK_PAGES, &kernel_stack_pages);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not allocate memory for kernel stack, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    set_tss_rsp0(kernel_stack_pages+0x1000*KERNEL_STACK_PAGES);

    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)&user_code_page);
    if (status != EFI_SUCCESS)
    {
        printf(L"Could not allocate a page for user code, status = 0x%lx\r\n", (uint64_t)status);
        SystemTable->BootServices->FreePages(kernel_stack_pages, 2);
        return status;
    }

    SystemTable->BootServices->SetMem(user_code_page, 0x1000, 0);

    uint64_t cr4 = read_cr4() | CR4_OSXSAVE | CR4_OSXMMEXCPT | CR4_OSFXSR;

    {
        uint32_t edx = 0;
        __asm__("cpuid":"=d"(edx):"a"(1):"rbx","rcx");
        if (edx & CPUID_01_EDX_MCE)
        {
            cr4 |= CR4_MCE;

            if (edx & CPUID_01_EDX_MCA)
            {
                uint64_t mcg_cap = rdmsr(MSR_IA32_MCG_CAP);
                if (mcg_cap & MCG_CAP_MCG_CTL_P)
                {
                    wrmsr(MSR_IA32_MCG_CTL, ~0ULL);
                }

                // We do not support very old CPUs that alias MC0_CTL to EBL_CR_POWERON MSR
                for (unsigned int i = 0; i < (mcg_cap & MCG_CAP_REPORTING_BANKS_COUNT_MASK); i++)
                {
                    wrmsr(MSR_IA32_MC0_CTL + i * 4, ~0ULL);
                    wrmsr(MSR_IA32_MC0_STATUS + i * 4, 0);
                }
            }
        }
    }

    write_cr4(cr4);

    uint32_t xcr0_lower = 0xd, xcr0_upper = 0;
    uint32_t supported_features_size = 0;
    __asm__("cpuid":"+a"(xcr0_lower),"=d"(xcr0_upper),"+c"(supported_features_size)::"rbx");
    __asm__("xsetbv"::"a"(xcr0_lower),"d"(xcr0_upper),"c"(0));
    status = SystemTable->BootServices->AllocatePool(EfiLoaderData, supported_features_size+0x40, (VOID**)&xsave_state_area_allocated_ptr);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not allocate memory for XSAVE state area, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    // Resets XSTATE_BV field by the way
    SystemTable->BootServices->SetMem(xsave_state_area_allocated_ptr, supported_features_size+0x40, 0);

    xsave_state_area_ptr = (uint8_t*)(((size_t)xsave_state_area_allocated_ptr + 0x40 - 1) & ~0x3fULL);

    __asm__("xrstor [%0]"::"r"(xsave_state_area_ptr):"memory");

    status = open_save_file(ImageHandle, SystemTable);
    if (status != EFI_SUCCESS)
    {
        printf(L"Could not open an output file, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    __asm__("cli");
    write_cr8(0xf);

    printf(L"User code page @ 0x%lx, remapping as user-accessible\r\n", user_code_page);

    write_cr0(read_cr0() & ~CR0_WP);

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

        printf(L"0x%lx[0x%x] -> 0x%lx\r\n", cur_page_table, page_table_index, page_table_entry);

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

        printf(L"New 0x%x page tables are at 0x%lx\r\n", page_tables_count, (uint64_t)new_page_tables);

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
            printf(L"===[ Page level 0x%x (%s) ]===\r\n", page_level, page_level_table_names[page_level]);
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

    setup_gdt();
    load_segments(0x8, 0x10, 0x28);
    setup_idt();

    wrmsr(MSR_IA32_EFER, rdmsr(MSR_IA32_EFER) | EFER_NXE);

    init_perf_counters();
    execute_ud();
    __builtin_unreachable();
}
