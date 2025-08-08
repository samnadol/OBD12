/*
 * ModelNumber.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include "BLE_Services/DeviceInformation.h"
#include "BLE_Services/DeviceInformation/ModelNumber.h"

#define UUID_CHAR_DEVICEINFORMATION_MODELNUMBER 0x2A24
#define ModelNumber "OBD12"

uint16_t Handle_Char_DeviceInformation_ModelNumber;

tBleStatus ServiceDeviceInformation_CharacteristicModelNumber_Update() {
	tBleStatus ret;

	ret = aci_gatt_update_char_value(Handle_Serv_DeviceInformation,
			Handle_Char_DeviceInformation_ModelNumber, 0, strlen(ModelNumber),
			(uint8_t*) &ModelNumber);

	if (ret != BLE_STATUS_SUCCESS) {
		PRINT_DBG("Error while updating DeviceInformation ModelNumber: 0x%04X\r\n", ret);
		return BLE_STATUS_ERROR;
	}

	return BLE_STATUS_SUCCESS;
}

tBleStatus ServiceDeviceInformation_CharacteristicModelNumber_Add() {
	uint8_t ret;

	Char_UUID_t char_uuid = { .Char_UUID_16 = UUID_CHAR_DEVICEINFORMATION_MODELNUMBER };
	ret = aci_gatt_add_char(Handle_Serv_DeviceInformation, UUID_TYPE_16,
			&char_uuid, strlen(ModelNumber),
			CHAR_PROP_READ, ATTR_PERMISSION_NONE,
			GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP, 16, 0,
			&Handle_Char_DeviceInformation_ModelNumber);

	if (ret != BLE_STATUS_SUCCESS)
		goto fail;

	ServiceDeviceInformation_CharacteristicModelNumber_Update();
	return BLE_STATUS_SUCCESS;

	fail: return BLE_STATUS_ERROR;
}
