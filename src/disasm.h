#ifndef _DISASM_H_
#define _DISASM_H_

#include <stddef.h>
#include <stdint.h>
void disasm_init(void);
uint64_t disasm_get_instruction_length(const uint8_t* bytes, size_t size);

#endif
