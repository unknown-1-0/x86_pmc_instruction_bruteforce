#include <efi.h>
#include <print.h>
#include <stdarg.h>
#include <stdint.h>

static SIMPLE_TEXT_OUTPUT_INTERFACE* ConOut = NULL;

void print(CHAR16* string)
{
    ConOut->OutputString(ConOut, string);
}

void print_init(EFI_SYSTEM_TABLE* SystemTable)
{
    ConOut = SystemTable->ConOut;

    UINTN rows = 0;
    UINTN cols = 0;

    uint64_t max_area = 0;
    UINTN selected_mode = 0;
    UINTN current_mode = 0;

    while (1)
    {
        if (ConOut->QueryMode(ConOut, current_mode, &rows, &cols) != EFI_SUCCESS)
        {
            if (current_mode == 1)
            {
                current_mode++;
                continue;
            }

            break;
        }

        uint64_t area = (uint64_t)rows * (uint64_t)cols;

        if (area > max_area)
        {
            max_area = area;
            selected_mode = current_mode;
            printf(L"Candidate text mode: 0x%x (rows: 0x%x, cols: 0x%x, area: 0x%x)\r\n", (uint64_t)selected_mode, (uint64_t)rows, (uint64_t)cols, area);
        }

        current_mode++;
    }

    printf(L"Setting text mode 0x%x\r\n", selected_mode);
    // Silently continue if we can't set the mode
    if (ConOut->SetMode(ConOut, selected_mode) != EFI_SUCCESS)
    {
        print(L"Could not set the new text mode!\r\n");
    }
}
void vsnprintf(CHAR16* out_buffer, size_t out_length, CHAR16* fmt, va_list varargs)
{
    if (out_length == 0)
    {
        return;
    }

    size_t fmt_i = 0;
    size_t out_i = 0;

    size_t digit_index = 7;

    while (out_i < out_length - 1)
    {
        switch (fmt[fmt_i])
        {
        case L'\0':
            out_buffer[out_i] = L'\0';
            return;
        case L'%':
            digit_index = 7;
            fmt_i++;
        recheck_fmt:
            switch (fmt[fmt_i])
            {
            case L'\0':
                out_buffer[out_i] = L'\0';
                return;
            case L'S':
            case L's':
                {
                    CHAR16* str = va_arg(varargs, CHAR16*);
                    size_t i = 0;

                    while (str[i] != L'\0' && out_i < out_length - 1)
                    {
                        out_buffer[out_i] = str[i];
                        out_i++;
                        i++;
                    }
                }
                break;
            case L'l':
            case L'L':
                digit_index = 15;
                fmt_i++;
                goto recheck_fmt;
            case L'h':
            case L'H':
                digit_index = 1;
                fmt_i++;
                goto recheck_fmt;
            case L'x':
            case L'X':
                {
                    uint64_t value = 0;

                    switch (digit_index)
                    {
                    case 15:
                        value = va_arg(varargs, uint64_t);
                        break;
                    case 7:
                        value = va_arg(varargs, uint32_t);
                        break;
                    case 1:
                        // uint8_t can be promoted to int, so uint8_t as a type here is UB
                        value = va_arg(varargs, uint32_t);
                        break;
                    default:
                        value = 0xbadf00d;
                        break;
                    }
                    
                    CHAR16* hex_chars = L"0123456789ABCDEF";

                    size_t digit = digit_index;

                    while (out_i < out_length - 1)
                    {
                        out_buffer[out_i] = hex_chars[
                            (value >> (digit * 4)) & 0xf
                        ];

                        out_i++;
                        if (digit == 0)
                        {
                            break;
                        }

                        digit--;
                    }
                }
                break;
            default:
                out_buffer[out_i] = fmt[fmt_i];
                out_i++;
                break;
            }
            fmt_i++;
            break;
        default:
            out_buffer[out_i] = fmt[fmt_i];
            out_i++;
            fmt_i++;
            break;
        }
    }
    out_buffer[out_i] = L'\0';
}

void snprintf(CHAR16* out_buffer, size_t length, CHAR16* fmt, ...)
{
    va_list varargs;

    va_start(varargs, fmt);

    vsnprintf(out_buffer, length, fmt, varargs);

    va_end(varargs);
}

void printf(CHAR16* fmt, ...)
{
    va_list varargs;

    va_start(varargs, fmt);

    CHAR16 buffer[0x400];

    vsnprintf(buffer, sizeof(buffer)/sizeof(buffer[0]), fmt, varargs);

    print(buffer);

    va_end(varargs);
}
