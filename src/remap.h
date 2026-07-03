#ifndef _REMAP_H_
#define _REMAP_H_

#include <efi.h>
#include <stddef.h>

void remap_init(EFI_SYSTEM_TABLE* EFISystemTable);

#define PROT_READ (1ULL<<0)
#define PROT_WRITE (1ULL<<1)
#define PROT_EXEC (1ULL<<2)
#define PROT_USER (1ULL<<3)

EFI_STATUS remap_page(size_t virt_addr, size_t phys_addr, unsigned int prot);

#endif
