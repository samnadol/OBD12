/*
 * Write.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include <BLE/BLE_Services/NordicUART/RX.h>
#include <BLE/BLE_Services/NordicUART.h>

#include "UART/Uart.h"

#define UUID_CHAR_NORDICUART_RX { \
    0x9E, 0xCA, 0xDC, 0x24, \
    0x0E, 0xE5, 0xA9, 0xE0, \
    0x93, 0xF3, 0xA3, 0xB5, \
    0x02, 0x00, 0x40, 0x6E  \
}

uint16_t Handle_Char_NordicUART_RX;

enum
{
	WRITE_SUCCESS 			= 0x00,
	WRITE_NOT_PERMITTED 	= 0x03,
};

tBleStatus ServiceNordicUART_CharacteristicRX_Process(uint16_t connection_handle, uint16_t attribute_handle, uint8_t *data, size_t length) {
	UART_BLE_RX(data, length);

	aci_gatt_write_resp(connection_handle, attribute_handle, WRITE_SUCCESS, 0x00, length, data);
	return BLE_STATUS_SUCCESS;
}

tBleStatus ServiceNordicUART_CharacteristicRX_Add() {
	uint8_t ret;

	Char_UUID_t char_uuid = { .Char_UUID_128 = UUID_CHAR_NORDICUART_RX };
	ret = aci_gatt_add_char(Handle_Serv_NordicUART, UUID_TYPE_128,
			&char_uuid, MAX_COMMAND_LENGTH,
			CHAR_PROP_WRITE, ATTR_PERMISSION_NONE,
			GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP, 16, 0,
			&Handle_Char_NordicUART_RX);

	if (ret != BLE_STATUS_SUCCESS)
		goto fail;

	PRINT_DBG("ServiceNordicUART_CharacteristicRX_Add() success, handle %02x\r\n", Handle_Char_NordicUART_RX);
	return BLE_STATUS_SUCCESS;

	fail:
	PRINT_DBG("ServiceNordicUART_CharacteristicRX_Add() FAILED\r\n");
	return BLE_STATUS_ERROR;
}
