#include <halt.h>
#include <print.h>
#include <stdint.h>
#include <stdlib.h>

uint64_t __security_cookie = 0xbadf00d;
uint64_t __security_check_cookie = 0xbadc0de;

void __attribute__((noreturn)) abort(void)
{
    print(L"abort() was called\r\n");
    halt();
}
