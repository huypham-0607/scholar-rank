#include "scholar_rank/utils/vbe.hpp"

#include <stdexcept>

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

unsigned long long vbe_decode(unsigned char buffer[]) {
    int decoded = 0;
    int idx = 0;
    while (idx < BUFFER_LIMIT) {
        if (buffer[idx] >= 128) {
            decoded += buffer[idx] - 128;
            break;
        }
        decoded += buffer[idx];

        ++idx;
    }

    if (idx == BUFFER_LIMIT) throw std::runtime_error("Buffer limit exceeded for Variable Byte Decoder.");

    return decoded;
}