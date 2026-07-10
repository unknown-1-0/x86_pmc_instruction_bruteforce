#include <print.h>
#include <stdint.h>

uint64_t __stack_chk_guard = 0xdeadbeefbadc0de;

void __stack_chk_fail(void)
{
    print(L"*** Stack smashing detected ***\r\n");

    while (1)
    {
        __asm__ volatile("cli\n"
                         "hlt\n");
    }
}
