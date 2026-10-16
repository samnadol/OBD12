/*
 * BLE_Events.c
 *
 * Overrides the weak event callbacks of the BlueNRG-2 middleware. They are
 * all called from hci_user_evt_proc() in the main loop.
 */

#include "BLE/BLE_Events.h"

#include "_Debug.h"
#include "App/system.h"
#include "BLE/App.h"
#include "BLE/BLE_Services/NordicUART.h"
#include "BLE/BLE_Services/Status.h"
#include "Comms/transport_ble.h"

#include "bluenrg1_aci.h"
#include "bluenrg1_events.h"
#include "bluenrg1_hci_le.h"

#define CCCD_NOTIFY 0x01

void aci_gatt_read_permit_req_event(uint16_t Connection_Handle, uint16_t Attribute_Handle, uint16_t Offset) {
	if (Attribute_Handle == Handle_Char_Status_Power + EVENT_WRITE)
		ServiceStatus_CharacteristicPower_Update();

	tBleStatus ret = aci_gatt_allow_read(Connection_Handle);
	if (ret != BLE_STATUS_SUCCESS)
		PRINT_DBG("aci_gatt_allow_read() failed: 0x%02x\r\n", ret);
}

void aci_gatt_attribute_modified_event(uint16_t Connection_Handle, uint16_t Attribute_Handle, uint16_t Offset, uint16_t Attr_Data_Length, uint8_t Attr_Data[]) {
	if (Attribute_Handle == Handle_Char_NordicUART_RX + EVENT_WRITE) {
		transport_ble_on_rx(Attr_Data, Attr_Data_Length);
	} else if (Attribute_Handle == Handle_Char_NordicUART_TX + EVENT_SUBSCRIBE_CHANGE) {
		transport_ble_on_tx_subscription(Attr_Data_Length > 0 && (Attr_Data[0] & CCCD_NOTIFY));
	} else if (Attribute_Handle == Handle_Char_Status_Power + EVENT_SUBSCRIBE_CHANGE) {
		Subscription_Status_Power = (Attr_Data_Length > 0 && (Attr_Data[0] & CCCD_NOTIFY));
	} else {
		PRINT_DBG("Unknown modification of GATT attribute 0x%04x\r\n", Attribute_Handle);
	}
}

void aci_att_exchange_mtu_resp_event(uint16_t Connection_Handle, uint16_t Server_RX_MTU) {
	transport_ble_on_mtu(Server_RX_MTU);
}

void aci_gatt_tx_pool_available_event(uint16_t Connection_Handle, uint16_t Available_Buffers) {
	transport_ble_on_tx_pool_available();
}

void hci_le_connection_complete_event(uint8_t Status,
		uint16_t Connection_Handle, uint8_t Role, uint8_t Peer_Address_Type,
		uint8_t Peer_Address[6], uint16_t Conn_Interval, uint16_t Conn_Latency,
		uint16_t Supervision_Timeout, uint8_t Master_Clock_Accuracy) {
	if (Status != BLE_STATUS_SUCCESS) {
		PRINT_DBG("BLE connection failed: 0x%02x\r\n", Status);
		return;
	}

	BLE_OnConnected(Connection_Handle);
}

void hci_disconnection_complete_event(uint8_t Status, uint16_t Connection_Handle, uint8_t Reason) {
	BLE_OnDisconnected(Reason);
}

void hci_encryption_change_event(uint8_t Status, uint16_t Connection_Handle, uint8_t Encryption_Enabled) {
	bool encrypted = (Status == BLE_STATUS_SUCCESS && Encryption_Enabled);
	transport_ble_on_encryption(encrypted);

	PRINT_DBG("BLE link encryption %s\r\n", encrypted ? "on" : "off");
}

// only used if the stack asks for the passkey instead of using the fixed one
void aci_gap_pass_key_req_event(uint16_t Connection_Handle) {
	tBleStatus ret = aci_gap_pass_key_resp(Connection_Handle, system_ble_passkey());
	if (ret != BLE_STATUS_SUCCESS)
		PRINT_DBG("aci_gap_pass_key_resp() failed: 0x%02x\r\n", ret);
}

void aci_gap_pairing_complete_event(uint16_t Connection_Handle, uint8_t Status, uint8_t Reason) {
	if (Status != 0x00)
		PRINT_DBG("BLE pairing failed, status 0x%02x, reason 0x%02x\r\n", Status, Reason);
	else
		PRINT_DBG("BLE pairing complete\r\n");
}

// the central lost its bond with us (e.g. "forget device" on the phone)
void aci_gap_bond_lost_event(void) {
	tBleStatus ret = aci_gap_allow_rebond(BLE_ConnectionHandle());
	if (ret != BLE_STATUS_SUCCESS)
		PRINT_DBG("aci_gap_allow_rebond() failed: 0x%02x\r\n", ret);
}

void BLE_ProcessUserEvent(void *pData) {
	hci_spi_pckt *hci_pckt = (hci_spi_pckt*) pData;
	if (hci_pckt->type != HCI_EVENT_PKT)
		return;

	hci_event_pckt *event_pckt = (hci_event_pckt*) hci_pckt->data;

	if (event_pckt->evt == EVT_LE_META_EVENT) {
		evt_le_meta_event *evt = (void*) event_pckt->data;
		for (uint32_t i = 0; i < (sizeof(hci_le_meta_events_table) / sizeof(hci_le_meta_events_table_type)); i++) {
			if (evt->subevent == hci_le_meta_events_table[i].evt_code) {
				hci_le_meta_events_table[i].process((void*) evt->data);
				break;
			}
		}
	} else if (event_pckt->evt == EVT_VENDOR) {
		evt_blue_aci *blue_evt = (void*) event_pckt->data;
		for (uint32_t i = 0; i < (sizeof(hci_vendor_specific_events_table) / sizeof(hci_vendor_specific_events_table_type)); i++) {
			if (blue_evt->ecode == hci_vendor_specific_events_table[i].evt_code) {
				hci_vendor_specific_events_table[i].process((void*) blue_evt->data);
				break;
			}
		}
	} else {
		for (uint32_t i = 0; i < (sizeof(hci_events_table) / sizeof(hci_events_table_type)); i++) {
			if (event_pckt->evt == hci_events_table[i].evt_code) {
				hci_events_table[i].process((void*) event_pckt->data);
				break;
			}
		}
	}
}
