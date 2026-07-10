#ifndef _STRING_H_
#define _STRING_H_

#include <stddef.h>

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
int strcmp(const char* s1, const char* s2);
size_t strlen(const char* s);
char* strncat(char* dst, const char* src, size_t size);
int strcat_s(char* dst, size_t dst_size, const char* src);

#endif
