/*
 * Write.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include <BLE/BLE_Services/NordicUART/RX.h>
#include <BLE/BLE_Services/NordicUART.h>

#define UUID_CHAR_NORDICUART_TX { \
    0x9E, 0xCA, 0xDC, 0x24, \
    0x0E, 0xE5, 0xA9, 0xE0, \
    0x93, 0xF3, 0xA3, 0xB5, \
    0x03, 0x00, 0x40, 0x6E  \
}

uint16_t Handle_Char_NordicUART_TX;
__IO uint8_t Subscription_NordicUART_TX;

tBleStatus ServiceNordicUART_CharacteristicTX_Update(uint8_t *data, size_t data_len) {
	tBleStatus ret;

	ret = aci_gatt_update_char_value(Handle_Serv_NordicUART, Handle_Char_NordicUART_TX, 0, data_len, data);

	if (ret != BLE_STATUS_SUCCESS) {
		PRINT_DBG("Error while updating status power characteristic: 0x%04X\r\n", ret);
		return BLE_STATUS_ERROR;
	}

	return BLE_STATUS_SUCCESS;
}

tBleStatus ServiceNordicUART_CharacteristicTX_Add() {
	uint8_t ret;

	Char_UUID_t char_uuid = { .Char_UUID_128 = UUID_CHAR_NORDICUART_TX };
	ret = aci_gatt_add_char(Handle_Serv_NordicUART, UUID_TYPE_128, &char_uuid,
			MAX_COMMAND_LENGTH,
			CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE,
			GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP, 16, 1,
			&Handle_Char_NordicUART_TX);

	if (ret != BLE_STATUS_SUCCESS)
		goto fail;

	PRINT_DBG(
			"ServiceNordicUART_CharacteristicTX_Add() success, handle %02x\r\n",
			Handle_Char_NordicUART_TX);
	return BLE_STATUS_SUCCESS;

	fail:
	PRINT_DBG("ServiceNordicUART_CharacteristicTX_Add() FAILED\r\n");
	return BLE_STATUS_ERROR;
}
