/*
 * ISOTP.c
 *
 *  Created on: Aug 18, 2025
 *      Author: samna
 */

#include "CAN/ISOTP.h"

#include <string.h>

#include "_Debug.h"
#include "App/sched.h"
#include "CAN/CAN.h"

#define ECU_COUNT (CAN_ID_RESPONSE_LAST - CAN_ID_RESPONSE_FIRST + 1)

// a response ID is the request ID + 8
#define REQUEST_ID_FOR(source) ((uint16_t) ((source) - 8))

#define FC_CONTINUE_TO_SEND 0x0
#define FC_OVERFLOW 0x2

typedef struct
{
	bool active;
	uint16_t expected;
	uint16_t received;
	uint8_t next_sn;
	uint32_t last_frame_tick;
	uint8_t buf[ISOTP_MAX_MESSAGE];
} isotp_session_t;

static isotp_session_t sessions[ECU_COUNT];

static void send_flow_control(uint32_t source, uint8_t flow_status)
{
	// block size 0 (send everything), separation time 0
	uint8_t frame[8] = { (CAN_PCI_FC << 4) | flow_status, 0, 0 };
	CAN_Tx(REQUEST_ID_FOR(source), frame);
}

static void on_single_frame(uint32_t source, const uint8_t *data, uint8_t dlc)
{
	uint8_t len = data[0] & 0x0F;
	if (len == 0 || len > 7 || len > dlc - 1)
	{
		PRINT_DBG("ISO-TP 0x%03lx: bad single frame length %u\r\n", source, len);
		return;
	}

	ISOTP_OnMessage(source, data + 1, len);
}

static void on_first_frame(isotp_session_t *s, uint32_t source, const uint8_t *data, uint8_t dlc)
{
	uint16_t len = ((uint16_t) (data[0] & 0x0F) << 8) | data[1];

	if (len < 8 || dlc < 8)
	{
		PRINT_DBG("ISO-TP 0x%03lx: bad first frame (len %u, dlc %u)\r\n", source, len, dlc);
		s->active = false;
		return;
	}

	if (len > ISOTP_MAX_MESSAGE)
	{
		PRINT_DBG("ISO-TP 0x%03lx: message too long (%u)\r\n", source, len);
		s->active = false;
		send_flow_control(source, FC_OVERFLOW);
		return;
	}

	s->active = true;
	s->expected = len;
	s->received = 6;
	s->next_sn = 1;
	s->last_frame_tick = HAL_GetTick();
	memcpy(s->buf, data + 2, 6);

	send_flow_control(source, FC_CONTINUE_TO_SEND);
}

static void on_consecutive_frame(isotp_session_t *s, uint32_t source, const uint8_t *data, uint8_t dlc)
{
	if (!s->active)
		return; // not ours, or the session was aborted

	uint8_t sn = data[0] & 0x0F;
	if (sn != s->next_sn)
	{
		PRINT_DBG("ISO-TP 0x%03lx: expected SN %u, got %u, aborting\r\n", source, s->next_sn, sn);
		s->active = false;
		return;
	}

	uint16_t remaining = s->expected - s->received;
	uint16_t chunk = dlc - 1;
	if (chunk > remaining)
		chunk = remaining;

	memcpy(s->buf + s->received, data + 1, chunk);
	s->received += chunk;
	s->next_sn = (s->next_sn + 1) & 0x0F;
	s->last_frame_tick = HAL_GetTick();

	if (s->received == s->expected)
	{
		s->active = false;
		ISOTP_OnMessage(source, s->buf, s->received);
	}
}

void ISOTP_OnFrame(uint32_t source, const uint8_t *data, uint8_t dlc)
{
	if (source < CAN_ID_RESPONSE_FIRST || source > CAN_ID_RESPONSE_LAST || dlc < 2)
		return;

	isotp_session_t *s = &sessions[source - CAN_ID_RESPONSE_FIRST];

	switch (data[0] >> 4)
	{
	case CAN_PCI_SF:
		on_single_frame(source, data, dlc);
		break;
	case CAN_PCI_FF:
		on_first_frame(s, source, data, dlc);
		break;
	case CAN_PCI_CF:
		on_consecutive_frame(s, source, data, dlc);
		break;
	case CAN_PCI_FC:
		break; // we never send multi-frame requests
	default:
		PRINT_DBG("ISO-TP 0x%03lx: unknown PCI type 0x%x\r\n", source, data[0] >> 4);
		break;
	}
}

void ISOTP_Poll(void)
{
	for (int i = 0; i < ECU_COUNT; i++)
	{
		if (sessions[i].active && elapsed_ms(sessions[i].last_frame_tick, ISOTP_N_CR_TIMEOUT_MS))
		{
			PRINT_DBG("ISO-TP 0x%03x: timed out waiting for consecutive frame\r\n", CAN_ID_RESPONSE_FIRST + i);
			sessions[i].active = false;
		}
	}
}

bool ISOTP_Busy(void)
{
	for (int i = 0; i < ECU_COUNT; i++)
		if (sessions[i].active)
			return true;
	return false;
}

bool ISOTP_SendSingle(uint16_t dest, const uint8_t *data, uint8_t len)
{
	if (len == 0 || len > 7)
		return false;

	uint8_t frame[8] = { 0 };
	frame[0] = (CAN_PCI_SF << 4) | len;
	memcpy(frame + 1, data, len);
	return CAN_Tx(dest, frame);
}
