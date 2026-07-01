#ifndef _SAVE_FILE_H_
#define _SAVE_FILE_H_

#include <efi.h>
#include <exception_context.h>
#include <stdbool.h>
#include <stdint.h>

enum instruction_type
{
    NOT_INTERESTING = 0,
    KERNEL_EXCEPTION,
    KERNEL_PAGE_FAULT,
    MACHINE_CHECK,
    UNDOCUMENTED_UD,
    UNDOCUMENTED_NOT_UD,
    VEX_MALFORMED_BUT_ACCEPTED,
    NOP_WITH_SIDE_EFFECTS,
    XED_LENGTH_MISMATCH
};

EFI_STATUS open_save_file(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable);
EFI_STATUS save_data(void* data, size_t size);
EFI_STATUS save_uint64(uint64_t value);
EFI_STATUS save_uint64_array(const uint64_t* values, size_t element_count);
EFI_STATUS save_instruction_data(struct context* context,
                                 enum instruction_type instruction_type,
                                 uint8_t* instruction_bytes, size_t length,
                                 bool extra_data_present, uint64_t extra_data,
                                 const uint64_t* perf_counters_values,
                                 size_t perf_counters_count);
EFI_STATUS flush_save_file(void);
UINT64 get_save_file_position(void);
void close_save_file(void);

#endif
