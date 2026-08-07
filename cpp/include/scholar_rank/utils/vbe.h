#ifndef VBE_H
#define VBE_H

#include <cstdlib>

constexpr int BUFFER_LIMIT = 8;

size_t vbe_encode(unsigned long long num, unsigned char buffer[]);

unsigned long long vbe_decode(const unsigned char buffer[]);

#endif