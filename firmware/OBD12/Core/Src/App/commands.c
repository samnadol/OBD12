/*
 * commands.c
 *
 * Request handlers for the host protocol (see Comms/protocol.h), plus the
 * application callbacks of the OBD and CAN layers.
 */

#include "App/commands.h"

#include <string.h>

#include "App/power.h"
#include "App/sched.h"
#include "App/system.h"
#include "BLE/App.h"
#include "BLE/BLE_Device.h"
#include "CAN/CAN.h"
#include "CAN/OBD.h"
#include "Comms/comms.h"

#define RESET_DRAIN_TIMEOUT_MS 200
#define LIVE_MIN_INTERVAL_MS 50

static uint32_t last_request_tick[TRANSPORT_COUNT];

// GET_SUPPORTED_PIDS requests waiting for the discovery, one per transport
static struct
{
	bool waiting;
	uint8_t seq;
} pids_waiter[TRANSPORT_COUNT];

static bool reset_pending;
static uint32_t reset_requested_tick;

// OBD services that only read data; anything else can change the vehicle state
static bool obd_service_is_read_only(uint8_t sid)
{
	switch (sid)
	{
	case OBD_SERVICE_DATA_LIVE:
	case OBD_SERVICE_DATA_FREEZE:
	case OBD_SERVICE_DTC_STORED:
	case OBD_SERVICE_TEST_O2:
	case OBD_SERVICE_TEST_OTHER:
	case OBD_SERVICE_DTC_PENDING:
	case OBD_SERVICE_INFO_VEHICLE:
	case OBD_SERVICE_DTC_PERMANENT:
		return true;
	default:
		return false;
	}
}

static bool require_auth(transport_id_t t, uint8_t seq, uint8_t id)
{
	if (comms_authorized(t))
		return true;

	// BLE: ask the central to pair; the host retries once the link is encrypted
	if (t == TRANSPORT_BLE)
		BLE_RequestSecurity();

	comms_error(t, seq, id, ERR_NOT_AUTHORIZED);
	return false;
}

static bool can_usable(void)
{
	can_state_t state = CAN_GetState();
	return state != CAN_STATE_BUS_OFF && state != CAN_STATE_NOT_STARTED;
}

static void handle_get_info(transport_id_t t, uint8_t seq)
{
	proto_info_t info = { 0 };
	char vin[17];
	uint8_t bitmap[OBD_PID_BITMAP_LEN];

	info.proto_version = PROTO_VERSION;
	info.fw_major = FW_VERSION_MAJOR;
	info.fw_minor = FW_VERSION_MINOR;
	info.fw_patch = FW_VERSION_PATCH;
	info.hwconf = system_hwconf();
	info.reset_cause = system_reset_cause();
	info.last_fault = system_last_fault();
	info.last_fault_info = system_last_fault_info();
	info.can_state = CAN_GetState();

	// the passkey is derived from the UID, so neither leaves the device over an
	// unauthenticated link
	if (comms_authorized(t))
	{
		uint32_t uid[3];
		system_uid(uid);
		memcpy(info.uid, uid, sizeof(uid));
	}
	if (t == TRANSPORT_USB)
		info.ble_passkey = system_ble_passkey();

	for (int i = 0; i < BDADDR_SIZE; i++)
		info.ble_mac[i] = bdaddr[BDADDR_SIZE - 1 - i];

	if (BLE_Available())
		info.flags |= INFO_FLAG_BLE_AVAILABLE;
	if (BLE_Connected())
		info.flags |= INFO_FLAG_BLE_CONNECTED;
	if (comms_authorized(t))
		info.flags |= INFO_FLAG_AUTHORIZED;
	if (OBD_GetVin(vin))
		info.flags |= INFO_FLAG_VIN_KNOWN;
	if (OBD_GetSupportedPids(bitmap))
		info.flags |= INFO_FLAG_PIDS_KNOWN;

	comms_reply(t, seq, CMD_GET_INFO, &info, sizeof(info));
}

static void handle_obd_request(transport_id_t t, uint8_t seq, const uint8_t *p, uint16_t len)
{
	if (len < 3 || len > 9)
	{
		comms_error(t, seq, CMD_OBD_REQUEST, ERR_BAD_REQUEST);
		return;
	}

	uint16_t dest = p[0] | ((uint16_t) p[1] << 8);
	if (dest != OBD_ID_BROADCAST && (dest < OBD_ID_ENGINE || dest > OBD_ID_ENGINE + 7))
	{
		comms_error(t, seq, CMD_OBD_REQUEST, ERR_BAD_REQUEST);
		return;
	}

	if (!obd_service_is_read_only(p[2]) && !require_auth(t, seq, CMD_OBD_REQUEST))
		return;

	if (!can_usable())
	{
		comms_error(t, seq, CMD_OBD_REQUEST, ERR_CAN);
		return;
	}

	obd_request_t req = {
		.dest = dest,
		.len = (uint8_t) (len - 2),
		.kind = OBD_KIND_HOST,
		.transport = t,
		.seq = seq,
	};
	memcpy(req.data, p + 2, req.len);

	if (!OBD_Submit(&req))
		comms_error(t, seq, CMD_OBD_REQUEST, ERR_BUSY);
}

static void handle_get_vin(transport_id_t t, uint8_t seq)
{
	char vin[17];
	if (OBD_GetVin(vin))
	{
		comms_reply(t, seq, CMD_GET_VIN, vin, sizeof(vin));
		return;
	}

	if (!can_usable())
	{
		comms_error(t, seq, CMD_GET_VIN, ERR_CAN);
		return;
	}

	obd_request_t req = {
		.dest = OBD_ID_BROADCAST,
		.len = 2,
		.data = { OBD_SERVICE_INFO_VEHICLE, OBD_S9_PID_VIN },
		.kind = OBD_KIND_HOST_VIN,
		.transport = t,
		.seq = seq,
	};

	if (!OBD_Submit(&req))
		comms_error(t, seq, CMD_GET_VIN, ERR_BUSY);
}

static void handle_get_supported_pids(transport_id_t t, uint8_t seq)
{
	uint8_t bitmap[OBD_PID_BITMAP_LEN];
	if (OBD_GetSupportedPids(bitmap))
	{
		comms_reply(t, seq, CMD_GET_SUPPORTED_PIDS, bitmap, sizeof(bitmap));
		return;
	}

	if (!can_usable())
	{
		comms_error(t, seq, CMD_GET_SUPPORTED_PIDS, ERR_CAN);
		return;
	}

	if (pids_waiter[t].waiting)
	{
		comms_error(t, seq, CMD_GET_SUPPORTED_PIDS, ERR_BUSY);
		return;
	}

	// answered by OBD_OnPidsDiscovered(); a discovery that already runs (e.g.
	// the one at startup) answers this request too
	pids_waiter[t].waiting = true;
	pids_waiter[t].seq = seq;
	OBD_DiscoverPids();
}

static void handle_live_start(transport_id_t t, uint8_t seq, const uint8_t *p, uint16_t len)
{
	if (len < 3 || len > 2 + OBD_LIVE_MAX_PIDS)
	{
		comms_error(t, seq, CMD_LIVE_START, ERR_BAD_REQUEST);
		return;
	}

	uint16_t interval = p[0] | ((uint16_t) p[1] << 8);
	if (interval < LIVE_MIN_INTERVAL_MS)
		interval = LIVE_MIN_INTERVAL_MS;

	// one polling configuration is shared by all hosts; the latest one wins
	OBD_LiveStart(interval, p + 2, (uint8_t) (len - 2));
	comms_set_topics(t, comms_topics(t) | TOPIC_LIVE);
	comms_reply(t, seq, CMD_LIVE_START, NULL, 0);
}

static void live_unsubscribe(transport_id_t t)
{
	comms_set_topics(t, comms_topics(t) & ~TOPIC_LIVE);
	if (!comms_any_subscribed(TOPIC_LIVE))
		OBD_LiveStop();
}

void commands_handle(transport_id_t t, uint8_t seq, uint8_t id, const uint8_t *p, uint16_t len)
{
	last_request_tick[t] = HAL_GetTick();

	switch (id)
	{
	case CMD_PING:
		comms_reply(t, seq, id, p, len);
		break;

	case CMD_GET_INFO:
		handle_get_info(t, seq);
		break;

	case CMD_GET_POWER:
	{
		proto_power_t power;
		power_read(&power);
		comms_reply(t, seq, id, &power, sizeof(power));
		break;
	}

	case CMD_SET_EVENTS:
		if (len != 1)
		{
			comms_error(t, seq, id, ERR_BAD_REQUEST);
			break;
		}
		comms_set_topics(t, (comms_topics(t) & TOPIC_LIVE) | (p[0] & ~TOPIC_LIVE));
		comms_reply(t, seq, id, NULL, 0);
		break;

	case CMD_GET_CAN_STATS:
	{
		const can_stats_t *s = CAN_GetStats();
		proto_can_stats_t stats = {
			.rx_frames = s->rx_frames,
			.rx_dropped = s->rx_dropped,
			.tx_frames = s->tx_frames,
			.tx_failed = s->tx_failed,
			.bus_off_count = s->bus_off_count,
			.transceiver_faults = s->transceiver_faults,
		};
		comms_reply(t, seq, id, &stats, sizeof(stats));
		break;
	}

	case CMD_RESET:
		if (!require_auth(t, seq, id))
			break;
		comms_reply(t, seq, id, NULL, 0);
		reset_pending = true;
		reset_requested_tick = HAL_GetTick();
		break;

	case CMD_BLE_CLEAR_BONDS:
		if (!require_auth(t, seq, id))
			break;
		if (BLE_ClearBonds() == BLE_STATUS_SUCCESS)
			comms_reply(t, seq, id, NULL, 0);
		else
			comms_error(t, seq, id, ERR_UNAVAILABLE);
		break;

	case CMD_OBD_REQUEST:
		handle_obd_request(t, seq, p, len);
		break;

	case CMD_GET_VIN:
		handle_get_vin(t, seq);
		break;

	case CMD_LIVE_START:
		handle_live_start(t, seq, p, len);
		break;

	case CMD_LIVE_STOP:
		live_unsubscribe(t);
		comms_reply(t, seq, id, NULL, 0);
		break;

	case CMD_GET_SUPPORTED_PIDS:
		handle_get_supported_pids(t, seq);
		break;

	default:
		comms_error(t, seq, id, ERR_UNKNOWN_CMD);
		break;
	}
}

void commands_process(void)
{
	for (int t = 0; t < TRANSPORT_COUNT; t++)
	{
		if ((comms_topics((transport_id_t) t) & TOPIC_LIVE) && elapsed_ms(last_request_tick[t], LIVE_LEASE_MS))
			live_unsubscribe((transport_id_t) t);
	}

	if (reset_pending)
	{
		bool drained = ring_count(comms_tx_ring(TRANSPORT_USB)) == 0 && ring_count(comms_tx_ring(TRANSPORT_BLE)) == 0;
		if (drained || elapsed_ms(reset_requested_tick, RESET_DRAIN_TIMEOUT_MS))
		{
			HAL_Delay(5); // last UART byte
			NVIC_SystemReset();
		}
	}
}

void OBD_OnRequestDone(const obd_request_t *req, obd_status_t status, const uint8_t *records, uint16_t records_len)
{
	transport_id_t t = (transport_id_t) req->transport;
	uint8_t id = (req->kind == OBD_KIND_HOST_VIN) ? CMD_GET_VIN : CMD_OBD_REQUEST;

	if (status == OBD_STATUS_TX_FAILED)
	{
		comms_error(t, req->seq, id, ERR_CAN);
		return;
	}

	if (req->kind == OBD_KIND_HOST_VIN)
	{
		char vin[17];
		if (OBD_GetVin(vin))
			comms_reply(t, req->seq, id, vin, sizeof(vin));
		else
			comms_error(t, req->seq, id, ERR_TIMEOUT);
		return;
	}

	if (status == OBD_STATUS_TIMEOUT)
		comms_error(t, req->seq, id, ERR_TIMEOUT);
	else
		comms_reply(t, req->seq, id, records, records_len);
}

void OBD_OnPidsDiscovered(obd_status_t status)
{
	uint8_t bitmap[OBD_PID_BITMAP_LEN];
	bool known = OBD_GetSupportedPids(bitmap);

	for (int t = 0; t < TRANSPORT_COUNT; t++)
	{
		if (!pids_waiter[t].waiting)
			continue;
		pids_waiter[t].waiting = false;

		if (known)
			comms_reply((transport_id_t) t, pids_waiter[t].seq, CMD_GET_SUPPORTED_PIDS, bitmap, sizeof(bitmap));
		else
			comms_error((transport_id_t) t, pids_waiter[t].seq, CMD_GET_SUPPORTED_PIDS,
					status == OBD_STATUS_TX_FAILED ? ERR_CAN : ERR_TIMEOUT);
	}
}

void OBD_OnLiveData(const uint8_t *records, uint16_t records_len)
{
	// subscribers drop out on BLE disconnect; stop polling when nobody listens
	if (!comms_any_subscribed(TOPIC_LIVE))
	{
		OBD_LiveStop();
		return;
	}

	comms_event(TOPIC_LIVE, EVT_LIVE_DATA, records, records_len);
}

void CAN_OnStateChange(can_state_t state)
{
	uint8_t s = (uint8_t) state;
	comms_event(TOPIC_CAN_STATUS, EVT_CAN_STATUS, &s, 1);
}
