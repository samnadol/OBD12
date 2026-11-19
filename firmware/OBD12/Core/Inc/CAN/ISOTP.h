/*
 * ISOTP.h
 *
 * ISO 15765-2 (ISO-TP) for diagnostic requests: single-frame requests, and
 * single- or multi-frame responses with flow control. Up to ISOTP_SESSIONS
 * ECUs can be in the middle of a multi-frame message at once; statically
 * allocated.
 *
 * Responses to the request in flight are received as ISOTP_SetLink()
 * describes: with extended addressing (one address byte before the PCI in
 * every frame, ISO 15765-2 extended/mixed addressing), the padding byte, and
 * where flow control goes. Other frames use normal addressing, with flow
 * control to the ISO 15765-4 request ID of the sender.
 *
 * ISO 15765-4 addressing, with the tester at 0xF1:
 *   11-bit: request 0x7E0-0x7E7, response = request + 8, functional 0x7DF
 *   29-bit: request 0x18DA<ecu>F1, response 0x18DAF1<ecu>, functional 0x18DB33F1
 */

#ifndef INC_CAN_ISOTP_H_
#define INC_CAN_ISOTP_H_

#include <stdbool.h>
#include <stdint.h>

#include "CAN/CAN.h"

#define ISOTP_MAX_MESSAGE 256
#define ISOTP_N_CR_TIMEOUT_MS 1000
#define ISOTP_SESSIONS 8

#define ISOTP_ID_FUNCTIONAL_11 0x7DFu
#define ISOTP_ID_FUNCTIONAL_29 (CAN_ID_EXT | 0x18DB33F1u)
#define ISOTP_ID_REQUEST_29    (CAN_ID_EXT | 0x18DA00F1u)   // + (ECU address << 8)

typedef enum
{
	CAN_PCI_SF = 0x0,
	CAN_PCI_FF = 0x1,
	CAN_PCI_CF = 0x2,
	CAN_PCI_FC = 0x3,
} CAN_PCI_TYPES;

typedef struct
{
	uint32_t rx_id;             // responses come from IDs matching rx_id ...
	uint32_t rx_mask;           // ... on these bits (the CAN_ID_EXT bit always has to match)
	uint32_t fc_id;             // flow control goes here; 0 = ISOTP_RequestIdFor(sender)
	bool ext_addr;              // extended addressing
	uint8_t tx_ext;             // address byte in the frames we send
	uint8_t rx_ext;             // address byte expected in the frames we receive
	uint8_t pad;                // unused bytes of the frames we send
} isotp_link_t;

// the ID an ECU answering on `response` takes requests on (and flow control)
uint32_t ISOTP_RequestIdFor(uint32_t response);
// the ID an ECU taking requests on `request` answers on
uint32_t ISOTP_ResponseIdFor(uint32_t request);

// true if CAN ID `id` matches rx_id / rx_mask of `link`
bool ISOTP_LinkMatches(const isotp_link_t *link, uint32_t id);

// how responses to the request in flight are received (NULL: normal addressing)
void ISOTP_SetLink(const isotp_link_t *link);

void ISOTP_OnFrame(uint32_t source, const uint8_t *data, uint8_t dlc);

// checks reassembly timeouts; call regularly
void ISOTP_Poll(void);

// true while any ECU is in the middle of a multi-frame message
bool ISOTP_Busy(void);

// largest request ISOTP_SendSingle() takes: 7 bytes, 6 with extended addressing
uint8_t ISOTP_SingleMax(bool ext_addr);

// sends a single-frame request to `dest`, with the link's addressing and padding
bool ISOTP_SendSingle(uint32_t dest, const uint8_t *data, uint8_t len);

// a complete message has been received from ECU `source` (implemented by OBD.c)
void ISOTP_OnMessage(uint32_t source, const uint8_t *msg, uint16_t len);

#endif /* INC_CAN_ISOTP_H_ */
