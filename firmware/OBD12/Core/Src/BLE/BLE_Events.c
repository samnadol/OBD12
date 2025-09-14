#include <BLE/App.h>
#include <BLE/BLE_Device.h>
#include <BLE/BLE_Events.h>
#include <BLE/BLE_Services/NordicUART.h>
#include <BLE/BLE_Services/Status.h>
#include <stdio.h>
#include <stdlib.h>

#include "bluenrg1_aci.h"
#include "bluenrg1_hci_le.h"
#include "bluenrg1_gatt_aci.h"

extern volatile uint8_t BLE_ENABLE_CONNECTION_FLAG;
extern volatile uint8_t BLE_CONNECTED;
extern volatile uint8_t BLE_PAIRING;
extern volatile uint8_t BLE_PAIRED;
extern __IO uint16_t BLE_CONNECTION_HANDLE;
extern uint32_t start_time;

void BLE_ReadRequest(uint16_t handle) {
	tBleStatus ret;

	if (handle == Handle_Char_Status_Power + 1)
		ServiceStatus_CharacteristicPower_Update();

	if (BLE_CONNECTION_HANDLE != 0) {
		ret = aci_gatt_allow_read(BLE_CONNECTION_HANDLE);
		if (ret != BLE_STATUS_SUCCESS) {
			PRINT_DBG("aci_gatt_allow_read() failed: 0x%02x\r\n", ret);
		}
	}
}

void BLE_AttributeRequest(uint16_t Connection_Handle, uint16_t attr_handle, uint16_t Offset, uint8_t data_length, uint8_t *att_data) {
	if (attr_handle == (Handle_Char_Status_Power + EVENT_SUBSCRIBE_CHANGE)) {
		Subscription_Status_Power = (att_data[0] == 1);
	} else if (attr_handle == (Handle_Char_NordicUART_TX + EVENT_SUBSCRIBE_CHANGE)) {
		Subscription_NordicUART_TX = (att_data[0] == 1);
	} else {
		PRINT_DBG("Got unknown modification to GATT Attribute %02x\r\n", attr_handle);
	}
}

void BLE_WriteRequest(uint16_t connection_handle, uint16_t attr_handle, uint8_t data_length, uint8_t *data)
{
	if (attr_handle == (Handle_Char_NordicUART_RX + EVENT_WRITE)) {
		ServiceNordicUART_CharacteristicRX_Process(connection_handle, attr_handle, data, data_length);
	} else {
		PRINT_DBG("Got unknown write request to GATT Attribute %02x\r\n", attr_handle);
	}
}

// fired when read request received
void aci_gatt_read_permit_req_event(uint16_t Connection_Handle, uint16_t Attribute_Handle, uint16_t Offset) {
	BLE_ReadRequest(Attribute_Handle);
}

// fired when attribute changes value
void aci_gatt_attribute_modified_event(uint16_t Connection_Handle, uint16_t Attribute_Handle, uint16_t Offset, uint16_t Attr_Data_Length, uint8_t Attr_Data[]) {
	BLE_AttributeRequest(Connection_Handle, Attribute_Handle, Offset, Attr_Data_Length, Attr_Data);
}

void aci_gatt_write_permit_req_event(uint16_t Connection_Handle, uint16_t Attribute_Handle, uint8_t Data_Length, uint8_t Data[]) {
	BLE_WriteRequest(Connection_Handle, Attribute_Handle, Data_Length, Data);
}

// new connection is created
void hci_le_connection_complete_event(uint8_t Status,
		uint16_t Connection_Handle, uint8_t Role, uint8_t Peer_Address_Type,
		uint8_t Peer_Address[6], uint16_t Conn_Interval, uint16_t Conn_Latency,
		uint16_t Supervision_Timeout, uint8_t Master_Clock_Accuracy) {
	BLE_CONNECTED = TRUE;

#if (!BLE_SECURE_PAIRING)
	BLE_PAIRING = TRUE;
	BLE_PAIRED = TRUE;
#endif

	BLE_CONNECTION_HANDLE = Connection_Handle;

	PRINT_DBG("Connected\r\n");
//	HAL_GPIO_WritePin(STATUS_LED_BANK[1], STATUS_LED_PIN[1], GPIO_PIN_SET);
}

// connection closed
void hci_disconnection_complete_event(uint8_t Status,
		uint16_t Connection_Handle, uint8_t Reason) {
	BLE_CONNECTED = FALSE;
	BLE_PAIRING = FALSE;
	BLE_PAIRED = FALSE;

	BLE_ENABLE_CONNECTION_FLAG = TRUE;
	BLE_CONNECTION_HANDLE = 0;

	PRINT_DBG("Disconnected (0x%02x)\r\n", Reason);
//	HAL_GPIO_WritePin(STATUS_LED_BANK[1], STATUS_LED_PIN[1], GPIO_PIN_RESET);
}

// fired when passkey required for pairing
void aci_gap_pass_key_req_event(uint16_t Connection_Handle) {
	uint8_t ret;

	ret = aci_gap_pass_key_resp(BLE_CONNECTION_HANDLE, BLE_CONNECTION_PASSKEY);
	if (ret != BLE_STATUS_SUCCESS)
		PRINT_DBG("aci_gap_pass_key_resp failed:0x%02x\r\n", ret);
}

// fired when pairing with passkey is compete
void aci_gap_pairing_complete_event(uint16_t connection_handle, uint8_t status, uint8_t reason) {
	if (status == 0x02) { // pairing failed status
		PRINT_DBG("aci_gap_pairing_complete_event FAILED, status: 0x%02x, reason 0x%02x\r\n", status, reason);
	} else {
		BLE_PAIRED = TRUE;
	}
}

// process all events
void BLE_ProcessUserEvent(void *pData) {
	uint32_t i;

	hci_spi_pckt *hci_pckt = (hci_spi_pckt*) pData;
	if (hci_pckt->type == HCI_EVENT_PKT) {
		hci_event_pckt *event_pckt = (hci_event_pckt*) hci_pckt->data;

		if (event_pckt->evt == EVT_LE_META_EVENT) {
			evt_le_meta_event *evt = (void*) event_pckt->data;
			for (i = 0; i < (sizeof(hci_le_meta_events_table) / sizeof(hci_le_meta_events_table_type)); i++) {
				if (evt->subevent == hci_le_meta_events_table[i].evt_code) {
					hci_le_meta_events_table[i].process((void*) evt->data);
				}
			}
		} else if (event_pckt->evt == EVT_VENDOR) {
			evt_blue_aci *blue_evt = (void*) event_pckt->data;
			for (i = 0; i < (sizeof(hci_vendor_specific_events_table) / sizeof(hci_vendor_specific_events_table_type)); i++) {
				if (blue_evt->ecode == hci_vendor_specific_events_table[i].evt_code) {
					hci_vendor_specific_events_table[i].process((void*) blue_evt->data);
				}
			}
		} else {
			for (i = 0; i < (sizeof(hci_events_table) / sizeof(hci_events_table_type)); i++) {
				if (event_pckt->evt == hci_events_table[i].evt_code) {
					hci_events_table[i].process((void*) event_pckt->data);
				}
			}
		}
	}
}

