#include <stddef.h>
#include <stdint.h>
#include <xed-chip-enum.h>
#include <xed-decoded-inst-api.h>
#include <xed-init.h>
#include <xed-interface.h>

static xed_chip_enum_t disasm_xed_chip = XED_CHIP_INVALID;

void disasm_init(xed_chip_enum_t xed_chip)
{
    xed_tables_init();
    disasm_xed_chip = xed_chip;
}

uint64_t disasm_get_instruction_length(const uint8_t* bytes, size_t size)
{
    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero(&xedd);
    xed_decoded_inst_set_mode(&xedd,
#if CPU_MODE == 64
            XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b
#elif CPU_MODE == 32
            XED_MACHINE_MODE_LONG_COMPAT_32, XED_ADDRESS_WIDTH_32b
#elif CPU_MODE == 16
            XED_MACHINE_MODE_LONG_COMPAT_16, XED_ADDRESS_WIDTH_32b
#else
#error Unknown CPU mode
#endif
    );

    xed_decoded_inst_set_input_chip(&xedd, disasm_xed_chip);

    if (xed_decode(&xedd, bytes, size) != XED_ERROR_NONE)
    {
        return 0;
    }

    return xed_decoded_inst_get_length(&xedd);
}
