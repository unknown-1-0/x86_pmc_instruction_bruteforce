#ifndef _PRINT_H_
#define _PRINT_H_

#include <efi.h>
void print_init(EFI_SYSTEM_TABLE* SystemTable);
void print(CHAR16* string);
void snprintf(CHAR16* out_buffer, size_t length, CHAR16* fmt, ...);
void printf(CHAR16* fmt, ...);

#endif
