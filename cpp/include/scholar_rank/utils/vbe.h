#ifndef VBE_H
#define VBE_H

#include <cstdlib>
#include <cstdio>

constexpr int BUFFER_LIMIT = 8;

size_t vbe_encode(unsigned long long num, unsigned char buffer[]);

unsigned long long vbe_decode(const unsigned char buffer[]);

bool read_vbe(FILE* const fp, unsigned char buffer[]);

#endif