/*
 * Write.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include <BLE/BLE_Services/Command/Write.h>
#include <BLE/BLE_Services/Command.h>

#define UUID_CHAR_COMMAND_WRITE { 0x00,0x00,0x00,0x01,0x00,0x02,0x55,0xE1,0x8F,0x00,0x00,0x00,0x00,0xA5,0xC2,0x1B }

uint16_t Handle_Char_Command_Write;

enum
{
	WRITE_SUCCESS 			= 0x00,
	WRITE_NOT_PERMITTED 	= 0x03,
};

tBleStatus ServiceCommand_CharacteristricWrite_Process(uint16_t connection_handle, uint16_t attribute_handle, uint8_t *data, size_t length) {
	printf("Got command (%x): %s\r\n", length, data);

	aci_gatt_write_resp(connection_handle, attribute_handle, WRITE_SUCCESS, 0x00, length, data);
	return BLE_STATUS_SUCCESS;
}

tBleStatus ServiceCommand_CharacteristicWrite_Add() {
	uint8_t ret;

	Char_UUID_t char_uuid = { .Char_UUID_128 = UUID_CHAR_COMMAND_WRITE };
	ret = aci_gatt_add_char(Handle_Serv_Command, UUID_TYPE_128,
			&char_uuid, MAX_COMMAND_LENGTH,
			CHAR_PROP_WRITE, ATTR_PERMISSION_NONE,
			GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP, 16, 0,
			&Handle_Char_Command_Write);

	if (ret != BLE_STATUS_SUCCESS)
		goto fail;

	PRINT_DBG("ServiceCommand_CharacteristicWrite_Add() success, handle %02x\r\n", Handle_Char_Command_Write);
	return BLE_STATUS_SUCCESS;

	fail: return BLE_STATUS_ERROR;
}
