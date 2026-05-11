#ifndef _SYSTEM_TABLES_SETUP_H_
#define _SYSTEM_TABLES_SETUP_H_

#include <stdint.h>

void setup_gdt(void);
void set_tss_rsp0(uint64_t new_value);
void setup_idt(void);

#endif
