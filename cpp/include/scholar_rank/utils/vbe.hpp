#ifndef VBE_HPP
#define VBE_HPP

#include <cstdlib>

constexpr int BUFFER_LIMIT = 8;

size_t vbe_encode(unsigned long long num, unsigned char buffer[]);

unsigned long long vbe_decode(unsigned char buffer[]);

#endif