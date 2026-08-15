#ifndef _DISASM_H_
#define _DISASM_H_

#include <stddef.h>
#include <stdint.h>
#include <xed-chip-enum.h>
void disasm_init(xed_chip_enum_t xed_chip);
uint64_t disasm_get_instruction_length(const uint8_t* bytes, size_t size);

#endif
