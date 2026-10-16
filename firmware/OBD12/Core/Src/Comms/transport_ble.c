/*
 * transport_ble.c
 */

#include "Comms/transport_ble.h"

#include "App/sched.h"
#include "Comms/comms.h"
#include "BLE/BLE_Services/NordicUART.h"

#include "bluenrg1_types.h"
#include "ble_status.h"

#define ATT_MTU_DEFAULT 23

// fallback in case the TX pool event is missed
#define BUFFER_RETRY_MS 100

static bool connected;
static bool tx_subscribed;
static bool waiting_for_buffers;
static uint32_t waiting_since;
static uint16_t att_mtu = ATT_MTU_DEFAULT;

static void update_link(void)
{
	comms_set_link(TRANSPORT_BLE, connected && tx_subscribed);
}

void transport_ble_on_connect(void)
{
	connected = true;
	tx_subscribed = false;
	waiting_for_buffers = false;
	att_mtu = ATT_MTU_DEFAULT;
	comms_set_authorized(TRANSPORT_BLE, false);
	update_link();
}

void transport_ble_on_disconnect(void)
{
	connected = false;
	tx_subscribed = false;
	comms_set_authorized(TRANSPORT_BLE, false);
	comms_set_topics(TRANSPORT_BLE, TOPIC_CAN_STATUS); // subscriptions don't outlive the connection
	update_link();
}

void transport_ble_on_mtu(uint16_t mtu)
{
	att_mtu = mtu;
}

void transport_ble_on_tx_subscription(bool enabled)
{
	tx_subscribed = enabled;
	update_link();
}

void transport_ble_on_rx(const uint8_t *data, uint16_t len)
{
	if (connected)
		comms_rx(TRANSPORT_BLE, data, len);
}

void transport_ble_on_tx_pool_available(void)
{
	waiting_for_buffers = false;
}

void transport_ble_on_encryption(bool encrypted)
{
	// pairing requires MITM protection (passkey), so an encrypted link is an
	// authenticated one
	comms_set_authorized(TRANSPORT_BLE, encrypted);
}

void transport_ble_process(void)
{
	if (!comms_link_up(TRANSPORT_BLE))
		return;

	if (waiting_for_buffers && !elapsed_ms(waiting_since, BUFFER_RETRY_MS))
		return;
	waiting_for_buffers = false;

	ring_t *tx = comms_tx_ring(TRANSPORT_BLE);
	uint16_t chunk_max = att_mtu - 3;
	if (chunk_max > BLE_TX_CHUNK_MAX)
		chunk_max = BLE_TX_CHUNK_MAX;

	uint8_t chunk[BLE_TX_CHUNK_MAX];
	uint16_t len;

	while ((len = ring_peek(tx, chunk, chunk_max)) > 0)
	{
		tBleStatus ret = ServiceNordicUART_CharacteristicTX_Update(chunk, len);

		if (ret == BLE_STATUS_INSUFFICIENT_RESOURCES)
		{
			// retried after aci_gatt_tx_pool_available_event
			waiting_for_buffers = true;
			waiting_since = HAL_GetTick();
			return;
		}

		// on other errors the chunk is dropped; the host resynchronises on the
		// next frame delimiter
		ring_skip(tx, len);
	}
}
