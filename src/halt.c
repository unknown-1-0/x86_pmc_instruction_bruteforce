#include <halt.h>

void __attribute__((noreturn)) halt(void)
{
    while (1)
    {
        __asm__("cli\n"
                "hlt\n");
    }
}
