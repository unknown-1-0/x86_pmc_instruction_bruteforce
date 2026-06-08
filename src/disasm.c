#include <stddef.h>
#include <stdint.h>
#include <xed-chip-enum.h>
#include <xed-decoded-inst-api.h>
#include <xed-init.h>
#include <xed-interface.h>

void disasm_init(void)
{
    xed_tables_init();
}

uint64_t disasm_get_instruction_length(const uint8_t* bytes, size_t size)
{
    xed_decoded_inst_t xedd = {0};
    xed_decoded_inst_set_mode(&xedd, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
    xed_decoded_inst_set_input_chip(&xedd, XED_CHIP_SKYLAKE);

    if (xed_ild_decode(&xedd, bytes, size) != XED_ERROR_NONE)
    {
        return 0;
    }

    return xed_decoded_inst_get_length(&xedd);
}
