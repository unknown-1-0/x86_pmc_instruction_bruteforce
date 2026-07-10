#include <efi.h>
#include <halt.h>
#include <print.h>
#include <string.h>
#include <string_init.h>

static EFI_BOOT_SERVICES* BootServices = NULL;

void string_init(EFI_SYSTEM_TABLE* SystemTable)
{
    BootServices = SystemTable->BootServices;
}

void* memcpy(void* dest, const void* src, size_t n)
{
    // Apparently, UEFI spec authors forgot to make src const...
    BootServices->CopyMem(dest, (void*)src, n);
    return dest;
}

void* memset(void* s, int c, size_t n)
{
    BootServices->SetMem(s, n, c);
    return s;
}

int memcmp(const void* s1, const void* s2, size_t n)
{
    if (!n)
    {
        return 0;
    }

    unsigned char* s1_ptr = (unsigned char*)s1;
    unsigned char* s2_ptr = (unsigned char*)s2;

    while (n && *s1_ptr == *s2_ptr)
    {
        s1_ptr++;
        s2_ptr++;
        n--;
    }

    return (int)*s1_ptr - (int)*s2_ptr;
}

int strcmp(const char* s1, const char* s2)
{
    while (*s1 && *s1 == *s2)
    {
        s1++;
        s2++;
    }

    return (int)*s1 - (int)*s2;
}

size_t strlen(const char* s)
{
    size_t len = 0;

    while (*(s++))
    {
        len++;
    }

    return len;
}

char* strncat(char* dst, const char* src, size_t size)
{
    char* cur_dst = dst;

    while(*cur_dst)
    {
        cur_dst++;
    }

    for (size_t i = 0; i < size && *src; i++)
    {
        *cur_dst++ = *src++;
    }

    *cur_dst = 0;

    return dst;
}

int strcat_s(char* dst, size_t dest_size, const char* src)
{
    if (!dst)
    {
        print(L"strcat_s: dst is NULL!!!\r\n");
        halt();
    }

    if (!src)
    {
        printf(L"strcat_s: src is NULL!!!\r\n");
        halt();
    }

    if (!dest_size)
    {
        printf(L"strcat_s: dest_size == 0!!!\r\n");
        halt();
    }

    char* cur_dst = dst;
    while (dest_size && *cur_dst)
    {
        cur_dst++;
        dest_size--;
    }

    if (!dest_size)
    {
        print(L"strcat_s: BUFFER OVERFLOWED!!!\r\n");
        halt();
    }

    while (dest_size && *src)
    {
        *(cur_dst++) = *(src++);
        dest_size--;
    }

    if (!dest_size && *src)
    {
        print(L"strcat_s: BUFFER OVERFLOW PREVENTED!!!\r\n");
        *dst = 0;
        return 1;
    }

    *cur_dst = 0;
    return 0;    
}
