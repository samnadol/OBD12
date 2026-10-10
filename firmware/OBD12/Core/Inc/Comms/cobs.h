/*
 * cobs.h
 *
 * Consistent Overhead Byte Stuffing. Encoded data contains no 0x00 bytes, so
 * 0x00 can delimit frames on a byte stream. The delimiter is not part of the
 * encoded data; callers append / strip it.
 */

#ifndef INC_COMMS_COBS_H_
#define INC_COMMS_COBS_H_

#include <stddef.h>
#include <stdint.h>

#define COBS_MAX_ENCODED(len) ((len) + ((len) / 254) + 1)

// out must hold COBS_MAX_ENCODED(len) bytes; returns the encoded length
size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out);

// out must hold len bytes; returns the decoded length, or 0 if the input is malformed
size_t cobs_decode(const uint8_t *in, size_t len, uint8_t *out);

#endif /* INC_COMMS_COBS_H_ */
