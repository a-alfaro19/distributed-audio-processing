// ==========================================
// Byte Manipulation & Endianness Utilities
// ==========================================

#include "common/byte_utils.h"

uint64_t bytes_to_uint64_be(const uint8_t *buffer)
{
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(uint64_t); ++i)
    {
        value = (value << 8) | buffer[i];
    }
    return value;
}

void uint64_to_bytes_be(uint64_t value, uint8_t *out_buffer)
{
    for (size_t i = 0; i < sizeof(uint64_t); ++i)
    {
        out_buffer[i] = (uint8_t)(value >> (8 * (sizeof(uint64_t) - 1 - i)));
    }
}
