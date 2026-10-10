/*
 * crc16.h
 *
 * CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
 * crc16("123456789") == 0x29B1.
 */

#ifndef INC_COMMS_CRC16_H_
#define INC_COMMS_CRC16_H_

#include <stddef.h>
#include <stdint.h>

uint16_t crc16(const uint8_t *data, size_t len);

#endif /* INC_COMMS_CRC16_H_ */
