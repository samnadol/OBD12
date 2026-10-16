/*
 * App.c
 *
 * BlueNRG-2 application state machine.
 */

#include "BLE/App.h"

#include "_Debug.h"
#include "BLE/BLE_Device.h"
#include "BLE/BLE_Events.h"
#include "Comms/transport_ble.h"

#include "hci.h"
#include "bluenrg1_aci.h"

static struct
{
	bool available;
	bool connected;
	bool advertise_pending;
	bool security_pending;
	uint16_t connection_handle;
} ble;

void MX_BlueNRG_2_Init(void) {
	hci_init(BLE_ProcessUserEvent, NULL);

	if (BLE_Device_Init() != BLE_STATUS_SUCCESS) {
		PRINT_DBG("BLE init failed, continuing without BLE\r\n");
		return;
	}

	ble.available = true;
	ble.advertise_pending = true;
}

void MX_BlueNRG_2_Process(void) {
	if (!ble.available)
		return;

	hci_user_evt_proc();

	if (ble.advertise_pending && !ble.connected) {
		ble.advertise_pending = false;
		BLE_Device_SetDiscoverable();
	}

	if (ble.security_pending && ble.connected) {
		ble.security_pending = false;

		tBleStatus ret = aci_gap_slave_security_req(ble.connection_handle);
		if (ret != BLE_STATUS_SUCCESS)
			PRINT_DBG("aci_gap_slave_security_req() failed: 0x%02x\r\n", ret);
	}

	transport_ble_process();
}

bool BLE_Available(void) {
	return ble.available;
}

bool BLE_Connected(void) {
	return ble.connected;
}

void BLE_RequestSecurity(void) {
	ble.security_pending = true;
}

tBleStatus BLE_ClearBonds(void) {
	if (!ble.available)
		return BLE_STATUS_ERROR;
	return aci_gap_clear_security_db();
}

void BLE_OnConnected(uint16_t connection_handle) {
	ble.connected = true;
	ble.connection_handle = connection_handle;
	ble.security_pending = BLE_PAIR_ON_CONNECT;
	transport_ble_on_connect();

	PRINT_DBG("BLE connected\r\n");
}

void BLE_OnDisconnected(uint8_t reason) {
	ble.connected = false;
	ble.connection_handle = 0;
	ble.security_pending = false;
	ble.advertise_pending = true;
	transport_ble_on_disconnect();

	PRINT_DBG("BLE disconnected (0x%02x)\r\n", reason);
}

uint16_t BLE_ConnectionHandle(void) {
	return ble.connection_handle;
}
