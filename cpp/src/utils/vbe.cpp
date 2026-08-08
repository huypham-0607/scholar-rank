#include "scholar_rank/utils/vbe.h"

#include <stdexcept>
#include <cstdio>

size_t vbe_encode(unsigned long long num, unsigned char buffer[]) {
    size_t idx = 0;
    while (idx < BUFFER_LIMIT) {
        buffer[idx] = num % 128;
        ++idx;

        num /= 128;
        if (num == 0) break;
    }

    if (num != 0) throw std::runtime_error("Buffer limit exceeded for Variable Byte Encoder.");
    buffer[idx - 1] += 128;
    return idx;
}

unsigned long long vbe_decode(const unsigned char buffer[]) {
    unsigned long long decoded = 0;
    int idx = 0;
    while (idx < BUFFER_LIMIT) {
        if (buffer[idx] >= 128) {
            decoded += (1LL << (idx*7))*(buffer[idx] - 128);
            break;
        }
        decoded += (1LL << (idx*7)) * buffer[idx];

        ++idx;
    }

    if (idx == BUFFER_LIMIT) throw std::runtime_error("Buffer limit exceeded for Variable Byte Decoder.");

    return decoded;
}

/**
 * @brief Read VBE from binary stream
 * 
 * Returns true when read a value VBE (a valid set end bit), false otherwise.
 * 
 * @param fp 
 * @param buffer 
 * @return true 
 * @return false 
 */

bool read_vbe(FILE* const fp, unsigned char buffer[]) {
    int idx = 0;
    while (idx < BUFFER_LIMIT) {
        size_t arg_count = fread(&buffer[idx], sizeof(unsigned char), 1, fp);
        if (arg_count == 0) {
            return false;
        }
        if (buffer[idx] >= 128) {
            return true;
        }
        ++idx;
    }

    // Buffer overflow
    return false;
}