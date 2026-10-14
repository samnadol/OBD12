/*
 * OBD.c
 *
 *  Created on: Aug 1, 2025
 *      Author: samna
 *
 * One request is in flight at a time. A broadcast request (0x7DF) collects
 * every response that arrives within the response window; a physically
 * addressed request finishes at the first response from its ECU. An ECU that
 * answers "response pending" (NRC 0x78) extends the window.
 */

#include "CAN/OBD.h"

#include <string.h>

#include "_Debug.h"
#include "App/sched.h"
#include "CAN/CAN.h"
#include "CAN/ISOTP.h"

#define QUEUE_SIZE 8
#define VIN_LENGTH 17

#define RESPONSE_SID(sid) ((uint8_t) ((sid) + 0x40))
#define IS_PHYSICAL(dest) ((dest) != OBD_ID_BROADCAST)

static obd_request_t queue[QUEUE_SIZE];
static uint8_t queue_head;
static uint8_t queue_count;

static struct
{
	bool busy;
	obd_request_t req;
	uint32_t start_tick;
	uint32_t window_ms;
	uint8_t responses;
	uint16_t result_len;
	uint8_t result[OBD_RESULT_MAX];
} active;

static char vin[VIN_LENGTH];
static bool vin_valid;

static struct
{
	bool enabled;
	uint16_t interval_ms;
	uint8_t pids[OBD_LIVE_MAX_PIDS];
	uint8_t count;

	bool in_cycle;
	bool request_pending;
	uint8_t next;
	uint32_t cycle_start;
	uint16_t len;
	uint8_t buf[OBD_RESULT_MAX];
} live;

static void try_parse_vin(const uint8_t *msg, uint16_t len)
{
	// 49 02 <number of data items> <17 ASCII characters>
	if (len < 2 + VIN_LENGTH || msg[0] != RESPONSE_SID(OBD_SERVICE_INFO_VEHICLE) || msg[1] != OBD_S9_PID_VIN)
		return;

	const uint8_t *chars = msg + len - VIN_LENGTH;
	for (int i = 0; i < VIN_LENGTH; i++)
		if (chars[i] < 0x20 || chars[i] > 0x7E)
			return;

	memcpy(vin, chars, VIN_LENGTH);
	if (!vin_valid)
		PRINT_DBG("VIN: %.17s\r\n", vin);
	vin_valid = true;
}

static void live_collect(const uint8_t *records, uint16_t records_len, uint8_t pid)
{
	// keep the first positive answer for this PID
	uint16_t pos = 0;
	while (pos + 3 <= records_len)
	{
		uint8_t len = records[pos + 2];
		const uint8_t *msg = &records[pos + 3];

		if (len >= 2 && msg[0] == RESPONSE_SID(OBD_SERVICE_DATA_LIVE) && msg[1] == pid)
		{
			uint8_t data_len = len - 2;
			if (live.len + 2 + data_len <= sizeof(live.buf))
			{
				live.buf[live.len++] = pid;
				live.buf[live.len++] = data_len;
				memcpy(&live.buf[live.len], msg + 2, data_len);
				live.len += data_len;
			}
			return;
		}

		pos += 3 + len;
	}
}

static void finish(obd_status_t status)
{
	active.busy = false;

	switch (active.req.kind)
	{
	case OBD_KIND_HOST:
	case OBD_KIND_HOST_VIN:
		OBD_OnRequestDone(&active.req, status, active.result, active.result_len);
		break;
	case OBD_KIND_LIVE:
		live.request_pending = false;
		if (live.enabled && live.in_cycle && status == OBD_STATUS_OK)
			live_collect(active.result, active.result_len, active.req.data[1]);
		break;
	case OBD_KIND_VIN:
		if (!vin_valid)
			PRINT_DBG("VIN not available (status %u)\r\n", status);
		break;
	}
}

void ISOTP_OnMessage(uint32_t source, const uint8_t *msg, uint16_t len)
{
	try_parse_vin(msg, len);

	if (!active.busy || len == 0)
		return; // unsolicited or late

	if (IS_PHYSICAL(active.req.dest) && source != active.req.dest + 8u)
		return;

	uint8_t sid = active.req.data[0];
	bool negative = (msg[0] == OBD_NEGATIVE_RESPONSE);

	if (negative)
	{
		if (len < 3 || msg[1] != sid)
			return;

		if (msg[2] == OBD_NRC_RESPONSE_PENDING)
		{
			active.start_tick = HAL_GetTick();
			active.window_ms = OBD_RESPONSE_PENDING_MS;
			return;
		}
	}
	else if (msg[0] != RESPONSE_SID(sid))
	{
		return; // answer to a different (earlier) request
	}

	uint16_t needed = 3 + len;
	if (len <= 255 && active.result_len + needed <= sizeof(active.result))
	{
		uint8_t *rec = &active.result[active.result_len];
		rec[0] = (uint8_t) (source & 0xFF);
		rec[1] = (uint8_t) (source >> 8);
		rec[2] = (uint8_t) len;
		memcpy(rec + 3, msg, len);
		active.result_len += needed;
	}
	active.responses++;

	if (IS_PHYSICAL(active.req.dest))
		finish(OBD_STATUS_OK);
}

static void start_next_request(void)
{
	if (active.busy || queue_count == 0)
		return;

	active.req = queue[queue_head];
	queue_head = (queue_head + 1) % QUEUE_SIZE;
	queue_count--;

	active.busy = true;
	active.responses = 0;
	active.result_len = 0;
	active.start_tick = HAL_GetTick();
	active.window_ms = OBD_RESPONSE_WINDOW_MS;

	if (!ISOTP_SendSingle(active.req.dest, active.req.data, active.req.len))
		finish(OBD_STATUS_TX_FAILED);
}

static void live_process(void)
{
	if (!live.enabled || live.count == 0)
		return;

	if (!live.in_cycle)
	{
		if (!elapsed_ms(live.cycle_start, live.interval_ms))
			return;

		live.in_cycle = true;
		live.cycle_start = HAL_GetTick();
		live.next = 0;
		live.len = 0;
	}

	if (live.request_pending)
		return;

	if (live.next < live.count)
	{
		obd_request_t req = {
			.dest = OBD_ID_BROADCAST,
			.len = 2,
			.data = { OBD_SERVICE_DATA_LIVE, live.pids[live.next] },
			.kind = OBD_KIND_LIVE,
		};

		if (OBD_Submit(&req))
		{
			live.request_pending = true;
			live.next++;
		}
		return;
	}

	live.in_cycle = false;
	if (live.len > 0)
		OBD_OnLiveData(live.buf, live.len);
}

void OBD_Init(void)
{
	obd_request_t req = {
		.dest = OBD_ID_BROADCAST,
		.len = 2,
		.data = { OBD_SERVICE_INFO_VEHICLE, OBD_S9_PID_VIN },
		.kind = OBD_KIND_VIN,
	};
	OBD_Submit(&req);
}

void OBD_Process(void)
{
	// a multi-frame answer still being received holds the window open
	// (bounded by the ISO-TP N_Cr timeout)
	if (active.busy && elapsed_ms(active.start_tick, active.window_ms) && !ISOTP_Busy())
	{
		// nobody answered, so the request may still be retransmitting unacknowledged
		if (active.responses == 0)
			CAN_AbortPendingTx();

		finish(active.responses ? OBD_STATUS_OK : OBD_STATUS_TIMEOUT);
	}

	live_process();
	start_next_request();
}

bool OBD_Submit(const obd_request_t *req)
{
	if (queue_count == QUEUE_SIZE || req->len == 0 || req->len > 7)
		return false;

	queue[(queue_head + queue_count) % QUEUE_SIZE] = *req;
	queue_count++;
	return true;
}

bool OBD_GetVin(char out[17])
{
	if (!vin_valid)
		return false;

	memcpy(out, vin, VIN_LENGTH);
	return true;
}

void OBD_LiveStart(uint16_t interval_ms, const uint8_t *pids, uint8_t count)
{
	if (count > OBD_LIVE_MAX_PIDS)
		count = OBD_LIVE_MAX_PIDS;

	memcpy(live.pids, pids, count);
	live.count = count;
	live.interval_ms = interval_ms;
	live.enabled = true;
	live.in_cycle = false;
	live.cycle_start = HAL_GetTick() - interval_ms; // start right away
}

void OBD_LiveStop(void)
{
	live.enabled = false;
	live.in_cycle = false;
}

bool OBD_LiveActive(void)
{
	return live.enabled;
}
