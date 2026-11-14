// ==========================================
// Byte Manipulation & Endianness Utilities
// ==========================================

#ifndef BYTE_UTILS_H
#define BYTE_UTILS_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Reconstructs a 64-bit unsigned integer from an 8-byte Big-Endian buffer.
 *
 * @param buffer Pointer to a byte array of at least 8 bytes.
 * @return uint64_t The reconstructed value in host byte order.
 */
uint64_t bytes_to_uint64_be(const uint8_t *buffer);

/**
 * @brief Serializes a 64-bit unsigned integer into an 8-byte Big-Endian buffer.
 *
 * @param value The 64-bit value to serialize.
 * @param out_buffer Pointer to a destination byte array of at least 8 bytes.
 */
void uint64_to_bytes_be(uint64_t value, uint8_t *out_buffer);

#endif // BYTE_UTILS_H
