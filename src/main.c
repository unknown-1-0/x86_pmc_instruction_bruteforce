#include <control_registers.h>
#include <efi.h>
#include <disasm.h>
#include <halt.h>
#include <msr.h>
#include <print.h>
#include <remap.h>
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
extern uint8_t* user_code_page_rw;
extern uint8_t* user_code_page_for_exec;
uint8_t* xsave_state_area_allocated_ptr = NULL;
uint8_t* xsave_state_area_ptr = NULL;

void init_perf_counters(void);
void execute_ud(void);
void dump_bruteforce_config(void);
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
    remap_init(SystemTable);
    disasm_init();

    EFI_PHYSICAL_ADDRESS kernel_stack_pages = 0;
    EFI_STATUS status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, KERNEL_STACK_PAGES, &kernel_stack_pages);

    if (status != EFI_SUCCESS)
    {
        printf(L"Could not allocate memory for kernel stack, status = 0x%lx\r\n", (uint64_t)status);
        return status;
    }

    set_tss_rsp0(kernel_stack_pages+0x1000*KERNEL_STACK_PAGES);

    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 2, (EFI_PHYSICAL_ADDRESS*)&user_code_page_rw);
    if (status != EFI_SUCCESS)
    {
        printf(L"Could not allocate pages for user code, status = 0x%lx\r\n", (uint64_t)status);
        SystemTable->BootServices->FreePages(kernel_stack_pages, 2);
        return status;
    }

    SystemTable->BootServices->SetMem(user_code_page_rw, 2*0x1000, 0);

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
        halt();
    }

    // Resets XSTATE_BV field by the way
    SystemTable->BootServices->SetMem(xsave_state_area_allocated_ptr, supported_features_size+0x40, 0);

    xsave_state_area_ptr = (uint8_t*)(((size_t)xsave_state_area_allocated_ptr + 0x40 - 1) & ~0x3fULL);

    __asm__("xrstor [%0]"::"r"(xsave_state_area_ptr):"memory");

    status = open_save_file(ImageHandle, SystemTable);
    if (status != EFI_SUCCESS)
    {
        printf(L"Could not open an output file, status = 0x%lx\r\n", (uint64_t)status);
        halt();;
    }

    __asm__("cli");
    write_cr8(0xf);

    printf(L"User code page @ 0x%lx, remapping as user-accessible\r\n", user_code_page_rw);
    status = remap_page((size_t)user_code_page_rw, (size_t)user_code_page_rw, PROT_READ | PROT_WRITE);
    if (status != EFI_SUCCESS)
    {
        printf(L"Could not remap the read-write version of the user code page, status = 0x%lx\r\n", status);
        halt();
    }

    user_code_page_for_exec = user_code_page_rw + 0x1000;
    status = remap_page((size_t)user_code_page_for_exec, (size_t)user_code_page_rw, PROT_READ | PROT_EXEC | PROT_USER);
    if (status != EFI_SUCCESS)
    {
        printf(L"Could not remap the exec-only version of the user code page, status = 0x%lx\r\n", status);
        halt();
    }
    setup_gdt();
    load_segments(0x8, 0x10, 0x28);
    setup_idt();

    wrmsr(MSR_IA32_EFER, rdmsr(MSR_IA32_EFER) | EFER_NXE);

    init_perf_counters();
    dump_bruteforce_config();
    execute_ud();
    __builtin_unreachable();
}
