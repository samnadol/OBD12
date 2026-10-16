/*
 * TX.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include <BLE/BLE_Services/NordicUART/TX.h>
#include <BLE/BLE_Services/NordicUART.h>

#include "Comms/transport_ble.h"

#define UUID_CHAR_NORDICUART_TX { \
    0x9E, 0xCA, 0xDC, 0x24, \
    0x0E, 0xE5, 0xA9, 0xE0, \
    0x93, 0xF3, 0xA3, 0xB5, \
    0x03, 0x00, 0x40, 0x6E  \
}

uint16_t Handle_Char_NordicUART_TX;

tBleStatus ServiceNordicUART_CharacteristicTX_Update(const uint8_t *data, uint16_t data_len) {
	// no logging here: log output may itself be on its way out through this function
	return aci_gatt_update_char_value(Handle_Serv_NordicUART, Handle_Char_NordicUART_TX, 0, (uint8_t) data_len, (uint8_t*) data);
}

tBleStatus ServiceNordicUART_CharacteristicTX_Add(void) {
	tBleStatus ret;

	Char_UUID_t char_uuid = { .Char_UUID_128 = UUID_CHAR_NORDICUART_TX };
	ret = aci_gatt_add_char(Handle_Serv_NordicUART, UUID_TYPE_128, &char_uuid,
			BLE_TX_CHUNK_MAX,
			CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE,
			GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1,
			&Handle_Char_NordicUART_TX);

	if (ret != BLE_STATUS_SUCCESS) {
		PRINT_DBG("ServiceNordicUART_CharacteristicTX_Add() failed: 0x%02x\r\n", ret);
		return ret;
	}

	PRINT_DBG("ServiceNordicUART_CharacteristicTX_Add() success, handle %02x\r\n", Handle_Char_NordicUART_TX);
	return BLE_STATUS_SUCCESS;
}
