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
 *
 * Live polling asks for each PID on its own on the broadcast address until it
 * has seen an answer, and learns from it which ECU answers and how many data
 * bytes the PID has. Learned PIDs are then requested up to six at a time
 * (SAE J1979), physically addressed to that ECU, which answers within a few
 * ms instead of waiting out the broadcast window. A batch that goes wrong
 * sends its PIDs back to single broadcast requests; after a few failures a PID
 * stays there.
 */

#include "CAN/OBD.h"

#include <string.h>

#include "_Debug.h"
#include "App/sched.h"
#include "CAN/CAN.h"
#include "CAN/ISOTP.h"

#define QUEUE_SIZE 8
#define VIN_LENGTH 17
#define PID_RANGE_LAST 0xE0     // PIDs 0xE1-0xFF; its "next range" bit would be PID 0x100

#define LIVE_BATCH_MAX 6        // PIDs per mode 01 request, SAE J1979
#define LIVE_BATCH_FAILURES_MAX 3

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
	bool known;
	bool running;
	bool request_pending;
	uint8_t range;              // PID 0x00, 0x20, ... being asked for
	uint8_t bitmap[OBD_PID_BITMAP_LEN];
} pids;

static struct
{
	bool enabled;
	uint16_t interval_ms;
	uint8_t pids[OBD_LIVE_MAX_PIDS];
	uint8_t count;

	bool in_cycle;
	bool request_pending;
	uint32_t polled;            // bit i: pids[i] was requested in this cycle
	uint32_t cycle_start;
	uint16_t len;
	uint8_t buf[OBD_RESULT_MAX];
} live;

// what live polling has learned about each mode 01 PID from single answers;
// kept while powered, since the vehicle does not change without a power cycle
static struct
{
	uint16_t ecu;               // response ID of the ECU that answers, 0 = not learned yet
	uint8_t len;                // data bytes after the PID
	uint8_t failures;           // batched requests that went wrong
} pid_info[256];

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

static bool live_batchable(uint8_t pid)
{
	return pid_info[pid].ecu != 0 && pid_info[pid].failures < LIVE_BATCH_FAILURES_MAX;
}

static void live_append(uint8_t pid, const uint8_t *data, uint8_t data_len)
{
	if (live.len + 2 + data_len > sizeof(live.buf))
		return;

	live.buf[live.len++] = pid;
	live.buf[live.len++] = data_len;
	memcpy(&live.buf[live.len], data, data_len);
	live.len += data_len;
}

// 41 <pid> <data> <pid> <data> ..., with the data lengths learned before;
// bit j set for each pids[j] in the answer, 0 unless the whole answer parses
static uint32_t live_parse_batch(const uint8_t *msg, uint16_t len, const uint8_t *pids, uint8_t count)
{
	uint32_t found = 0;
	uint16_t pos = 1;
	while (pos < len)
	{
		int j = 0;
		while (j < count && (pids[j] != msg[pos] || (found & (1u << j))))
			j++;
		if (j == count)
			return 0; // a PID we did not ask for, or a length we got wrong

		uint8_t data_len = pid_info[msg[pos]].len;
		if (pos + 1 + data_len > len)
			return 0;

		found |= 1u << j;
		pos += 1 + data_len;
	}
	return found;
}

static void live_collect(obd_status_t status, const uint8_t *records, uint16_t records_len)
{
	const obd_request_t *req = &active.req;
	const uint8_t *pids = &req->data[1];
	uint8_t count = req->len - 1;
	uint32_t found = 0;

	uint16_t pos = 0;
	while (status == OBD_STATUS_OK && pos + 3 <= records_len)
	{
		uint16_t source = records[pos] | ((uint16_t) records[pos + 1] << 8);
		uint8_t len = records[pos + 2];
		const uint8_t *msg = &records[pos + 3];
		pos += 3 + len;

		if (len < 2 || msg[0] != RESPONSE_SID(OBD_SERVICE_DATA_LIVE))
			continue;

		if (!IS_PHYSICAL(req->dest))
		{
			// single PID: keep the first positive answer, and learn from it
			if (msg[1] != pids[0])
				continue;
			pid_info[pids[0]].ecu = source;
			pid_info[pids[0]].len = len - 2;
			live_append(pids[0], msg + 2, len - 2);
			return;
		}

		found = live_parse_batch(msg, len, pids, count);
		for (uint16_t p = 1; found && p < len; p += 1 + pid_info[msg[p]].len)
			live_append(msg[p], msg + p + 1, pid_info[msg[p]].len);
		break;
	}

	// a batched PID that did not come back is asked for on its own again, in
	// this cycle, which also relearns its ECU and length
	if (IS_PHYSICAL(req->dest))
	{
		for (int j = 0; j < count; j++)
		{
			if (found & (1u << j))
				continue;
			pid_info[pids[j]].ecu = 0;
			if (pid_info[pids[j]].failures < LIVE_BATCH_FAILURES_MAX)
				pid_info[pids[j]].failures++;
			for (int i = 0; i < live.count; i++)
				if (live.pids[i] == pids[j])
					live.polled &= ~(1u << i);
		}
	}
}

// ORs every ECU's answer for this range into the bitmap; false if none answered
static bool pids_collect(const uint8_t *records, uint16_t records_len, uint8_t range)
{
	bool answered = false;
	uint16_t pos = 0;
	while (pos + 3 <= records_len)
	{
		uint8_t len = records[pos + 2];
		const uint8_t *msg = &records[pos + 3];

		// 41 <range> A B C D
		if (len >= 6 && msg[0] == RESPONSE_SID(OBD_SERVICE_DATA_LIVE) && msg[1] == range)
		{
			for (int i = 0; i < 4; i++)
				pids.bitmap[range / 8 + i] |= msg[2 + i];
			answered = true;
		}

		pos += 3 + len;
	}
	return answered;
}

static void pids_finish(obd_status_t status)
{
	pids.running = false;
	pids.known = (status == OBD_STATUS_OK);

	if (pids.known)
	{
		unsigned count = 0;
		for (int p = 1; p <= 0xFF; p++)
			if (pids.bitmap[(p - 1) / 8] & (0x80 >> ((p - 1) % 8)))
				count++;
		PRINT_DBG("Supported PIDs: %u\r\n", count);
	}
	else
	{
		PRINT_DBG("Supported PIDs not available (status %u)\r\n", status);
	}

	OBD_OnPidsDiscovered(status);
}

static void pids_step(obd_status_t status, const uint8_t *records, uint16_t records_len)
{
	uint8_t range = pids.range;
	bool answered = (status == OBD_STATUS_OK) && pids_collect(records, records_len, range);

	if (range == 0x00 && !answered)
	{
		pids_finish(status == OBD_STATUS_TX_FAILED ? OBD_STATUS_TX_FAILED : OBD_STATUS_TIMEOUT);
		return;
	}

	// the last bit of each range says whether the next range is supported; a
	// range nobody answers ends the discovery with what is known so far
	if (answered && range < PID_RANGE_LAST && (pids.bitmap[range / 8 + 3] & 0x01))
	{
		pids.range = range + 0x20;
		return;
	}

	pids_finish(OBD_STATUS_OK);
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
		if (live.enabled && live.in_cycle)
			live_collect(status, active.result, active.result_len);
		break;
	case OBD_KIND_VIN:
		if (!vin_valid)
			PRINT_DBG("VIN not available (status %u)\r\n", status);
		break;
	case OBD_KIND_PIDS:
		pids.request_pending = false;
		pids_step(status, active.result, active.result_len);
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

static void pids_process(void)
{
	if (!pids.running || pids.request_pending)
		return;

	obd_request_t req = {
		.dest = OBD_ID_BROADCAST,
		.len = 2,
		.data = { OBD_SERVICE_DATA_LIVE, pids.range },
		.kind = OBD_KIND_PIDS,
	};

	if (OBD_Submit(&req))
		pids.request_pending = true;
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
		live.polled = 0;
		live.len = 0;
	}

	if (live.request_pending)
		return;

	int first = 0;
	while (first < live.count && (live.polled & (1u << first)))
		first++;

	if (first == live.count)
	{
		live.in_cycle = false;
		if (live.len > 0)
			OBD_OnLiveData(live.buf, live.len);
		return;
	}

	uint8_t pid = live.pids[first];
	obd_request_t req = {
		.dest = OBD_ID_BROADCAST,
		.len = 2,
		.data = { OBD_SERVICE_DATA_LIVE, pid },
		.kind = OBD_KIND_LIVE,
	};
	uint32_t picked = 1u << first;

	if (live_batchable(pid))
	{
		// this PID and the next unpolled ones from the same ECU, physically addressed
		uint16_t ecu = pid_info[pid].ecu;
		req.dest = ecu - 8;
		for (int i = first + 1; i < live.count && req.len < 1 + LIVE_BATCH_MAX; i++)
		{
			uint8_t p = live.pids[i];
			if (!(live.polled & (1u << i)) && live_batchable(p) && pid_info[p].ecu == ecu)
			{
				req.data[req.len++] = p;
				picked |= 1u << i;
			}
		}
	}

	if (OBD_Submit(&req))
	{
		live.request_pending = true;
		live.polled |= picked;
	}
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

	OBD_DiscoverPids();
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

	pids_process();
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

void OBD_DiscoverPids(void)
{
	if (pids.known || pids.running)
		return;

	memset(pids.bitmap, 0, sizeof(pids.bitmap));
	pids.range = 0x00;
	pids.request_pending = false;
	pids.running = true;
}

bool OBD_GetSupportedPids(uint8_t bitmap[OBD_PID_BITMAP_LEN])
{
	if (!pids.known)
		return false;

	memcpy(bitmap, pids.bitmap, OBD_PID_BITMAP_LEN);
	return true;
}

void OBD_LiveStart(uint16_t interval_ms, const uint8_t *pids, uint8_t count)
{
	// without duplicates, so a batch never asks for a PID twice
	live.count = 0;
	for (int i = 0; i < count && live.count < OBD_LIVE_MAX_PIDS; i++)
	{
		bool seen = false;
		for (int j = 0; j < live.count; j++)
			seen |= (live.pids[j] == pids[i]);
		if (!seen)
			live.pids[live.count++] = pids[i];
	}

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
