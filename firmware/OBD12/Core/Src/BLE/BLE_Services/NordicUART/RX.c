/*
 * RX.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 *
 * Writes (with or without response) are reported through
 * aci_gatt_attribute_modified_event and forwarded to transport_ble_on_rx().
 */

#include <BLE/BLE_Services/NordicUART/RX.h>
#include <BLE/BLE_Services/NordicUART.h>

#include "Comms/transport_ble.h"

#define UUID_CHAR_NORDICUART_RX { \
    0x9E, 0xCA, 0xDC, 0x24, \
    0x0E, 0xE5, 0xA9, 0xE0, \
    0x93, 0xF3, 0xA3, 0xB5, \
    0x02, 0x00, 0x40, 0x6E  \
}

uint16_t Handle_Char_NordicUART_RX;

tBleStatus ServiceNordicUART_CharacteristicRX_Add(void) {
	tBleStatus ret;

	Char_UUID_t char_uuid = { .Char_UUID_128 = UUID_CHAR_NORDICUART_RX };
	ret = aci_gatt_add_char(Handle_Serv_NordicUART, UUID_TYPE_128,
			&char_uuid, BLE_RX_WRITE_MAX,
			CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RESP, ATTR_PERMISSION_NONE,
			GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1,
			&Handle_Char_NordicUART_RX);

	if (ret != BLE_STATUS_SUCCESS) {
		PRINT_DBG("ServiceNordicUART_CharacteristicRX_Add() failed: 0x%02x\r\n", ret);
		return ret;
	}

	PRINT_DBG("ServiceNordicUART_CharacteristicRX_Add() success, handle %02x\r\n", Handle_Char_NordicUART_RX);
	return BLE_STATUS_SUCCESS;
}
