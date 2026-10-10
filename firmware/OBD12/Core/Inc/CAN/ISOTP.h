/*
 * ISOTP.h
 *
 * ISO 15765-2 (ISO-TP) receive side for OBD-II responses: single frames and
 * multi-frame reassembly with flow control. One session per responding ECU
 * (0x7E8-0x7EF); statically allocated.
 */

#ifndef INC_CAN_ISOTP_H_
#define INC_CAN_ISOTP_H_

#include <stdbool.h>
#include <stdint.h>

#define ISOTP_MAX_MESSAGE 128
#define ISOTP_N_CR_TIMEOUT_MS 1000

typedef enum
{
	CAN_PCI_SF = 0x0,
	CAN_PCI_FF = 0x1,
	CAN_PCI_CF = 0x2,
	CAN_PCI_FC = 0x3,
} CAN_PCI_TYPES;

void ISOTP_OnFrame(uint32_t source, const uint8_t *data, uint8_t dlc);

// checks reassembly timeouts; call regularly
void ISOTP_Poll(void);

// true while any ECU is in the middle of a multi-frame message
bool ISOTP_Busy(void);

// sends a single-frame request (len <= 7) to the given ID
bool ISOTP_SendSingle(uint16_t dest, const uint8_t *data, uint8_t len);

// a complete message has been received from ECU `source` (implemented by OBD.c)
void ISOTP_OnMessage(uint32_t source, const uint8_t *msg, uint16_t len);

#endif /* INC_CAN_ISOTP_H_ */
