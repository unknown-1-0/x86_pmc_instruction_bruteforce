#include <control_registers.h>
#include <efi.h>
#include <print.h>
#include <remap.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static EFI_BOOT_SERVICES* BootServices= NULL;
void remap_init(EFI_SYSTEM_TABLE* EFISystemTable)
{
    BootServices = EFISystemTable->BootServices;
}

static EFI_STATUS allocate_zeroed_page(void** out_page_addr)
{
    EFI_STATUS status = BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, (EFI_PHYSICAL_ADDRESS*)out_page_addr);

    if (status == EFI_SUCCESS)
    {
        memset(*out_page_addr, 0, 0x1000);
    }
    return status;
}

#define PAGE_PRESENT (1ULL<<0)
#define PAGE_WRITABLE (1ULL<<1)
#define PAGE_USER (1ULL<<2)
#define PAGE_SIZE (1ULL<<7)
#define PAGE_NOEXECUTE (1ULL<<63)

enum page_table_levels
{
    PML5 = 0,
    PML4,
    PDPT,
    PD,
    PT,
    PAGE_TABLE_LEVELS_COUNT
};

EFI_STATUS remap_page(size_t virt_addr, size_t phys_addr, unsigned int prot)
{
    uint64_t* cur_page_table = (uint64_t*)(read_cr3() & ~0xfffULL);

    uint64_t orig_cr0 = read_cr0();
    write_cr0(orig_cr0 & ~CR0_WP);

    size_t i = (read_cr4() & CR4_LA57) ? PML5 : PML4;

    uint16_t pt_indexes[PAGE_TABLE_LEVELS_COUNT] = {
        (virt_addr >> 48) & 0x1ffULL, // PML5
        (virt_addr >> 39) & 0x1ffULL, // PML4
        (virt_addr >> 30) & 0x1ffULL, // PDPT
        (virt_addr >> 21) & 0x1ffULL, // PD
        (virt_addr >> 12) & 0x1ffULL, // PT
    };

    uint64_t pte_prot = PAGE_PRESENT;
    if (prot & PROT_WRITE)
        pte_prot |= PAGE_WRITABLE;

    if (prot & PROT_USER)
        pte_prot |= PAGE_USER;

    if (!(prot & PROT_EXEC))
        pte_prot |= PAGE_NOEXECUTE;

    while (i < PT)
    {
        uint64_t cur_pte = cur_page_table[pt_indexes[i]];

#ifdef TRACE_REMAP
        printf(L"0x%lx[0x%x] -> 0x%lx\r\n", cur_page_table, pt_indexes[i], cur_pte);
#endif

        if (!(cur_pte & PAGE_PRESENT) || (cur_pte & PAGE_SIZE))
        {
            uint64_t* new_page_table = 0;
            EFI_STATUS status = allocate_zeroed_page((void**)&new_page_table);

            if (status != EFI_SUCCESS)
            {
                return status;
            }

            if (cur_pte & PAGE_SIZE)
            {
                if (i == PD)
                {
                    cur_pte &= ~PAGE_SIZE;
                }
                size_t next_level_i = i + 1;
                size_t next_level_start_bit = 12 + 9 * (PT - next_level_i);
                size_t offset_to_add = 1ULL << next_level_start_bit;
                for (size_t j = 0; j < 0x200; j++)
                {
                    new_page_table[j] = cur_pte + offset_to_add * j;
                }
            }

            cur_pte = (uint64_t)new_page_table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
            cur_page_table[pt_indexes[i]] = cur_pte;
        }

        if ((cur_pte & pte_prot) != (pte_prot & ~PAGE_NOEXECUTE))
        {
            cur_page_table[pt_indexes[i]] = (cur_pte | pte_prot) & ~PAGE_NOEXECUTE;
        }

        cur_page_table = (uint64_t*)(cur_pte & ~(0xfffULL | PAGE_NOEXECUTE));
        i++;
    }
#ifdef TRACE_REMAP
    printf(L"PT @ 0x%lx\r\n", cur_page_table);
#endif
    cur_page_table[pt_indexes[PT]] = phys_addr | pte_prot;
#ifdef TRACE_REMAP
    printf(L"PT[%x] -> 0x%lx\r\n", pt_indexes[PT], cur_page_table[pt_indexes[PT]]);
#endif
    // Flush TLB
    write_cr3(read_cr3());
    write_cr0(orig_cr0);
    return EFI_SUCCESS;
}
