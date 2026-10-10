/*
 * CAN.h
 *
 * FDCAN1 driver (classic CAN, 500 kbit/s, 11-bit IDs). Only OBD-II response
 * IDs (0x7E8-0x7EF) pass the hardware filter. Received frames are queued by
 * the RX interrupt and handed to ISO-TP from CAN_Process() in the main loop.
 */

#ifndef CAN_H_
#define CAN_H_

#include <stdbool.h>
#include <stdint.h>

#include "stm32g4xx_hal.h"

#define CAN_ID_RESPONSE_FIRST 0x7E8
#define CAN_ID_RESPONSE_LAST  0x7EF

typedef enum
{
	CAN_STATE_OK = 0,
	CAN_STATE_ERROR_WARNING = 1,
	CAN_STATE_ERROR_PASSIVE = 2,
	CAN_STATE_BUS_OFF = 3,
	CAN_STATE_NOT_STARTED = 4,
} can_state_t;

typedef struct
{
	uint32_t id;
	uint8_t dlc;
	uint8_t data[8];
} can_frame_t;

typedef struct
{
	uint32_t rx_frames;
	uint32_t rx_dropped;    // RX ring or hardware FIFO overflow
	uint32_t tx_frames;
	uint32_t tx_failed;
	uint32_t bus_off_count;
	uint32_t transceiver_faults;  // rising edges on the TCAN337 FAULT pin
} can_stats_t;

void CAN_Config(FDCAN_HandleTypeDef *hfdcan);
void CAN_Process(void);

// sends an 8-byte classic CAN data frame (callers pad unused bytes)
bool CAN_Tx(uint16_t identifier, const uint8_t data[8]);

// cancels frames still waiting for an ACK. Automatic retransmission would
// otherwise repeat an unanswered frame indefinitely (e.g. on a sleeping bus,
// where it could keep waking ECUs).
void CAN_AbortPendingTx(void);

can_state_t CAN_GetState(void);
const can_stats_t *CAN_GetStats(void);

// called when CAN_GetState() changes (implemented by the application)
void CAN_OnStateChange(can_state_t state);

#endif /* CAN_H_ */
