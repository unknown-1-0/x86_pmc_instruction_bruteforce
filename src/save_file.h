#ifndef _SAVE_FILE_H_
#define _SAVE_FILE_H_

#include <efi.h>
#include "exception_context.h"

EFI_STATUS open_save_file(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable);
EFI_STATUS save_instruction_data(struct context* context, uint8_t* instruction_bytes, size_t length, uint64_t extra_data);
EFI_STATUS flush_save_file(void);
UINT64 get_save_file_position(void);
void close_save_file(void);

#endif
